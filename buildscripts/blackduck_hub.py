#!/usr/bin/env python3
"""Utility script to run Black Duck scans and query Black Duck database."""

import argparse
import datetime
import functools
import io
import json
import logging
import os
import subprocess
import sys
import tempfile
import textwrap
import time
import warnings

from abc import ABCMeta, abstractmethod
from typing import Dict, List, Optional

import urllib3.util.retry as urllib3_retry
import requests
import yaml

from blackduck.HubRestApi import HubInstance

try:
    import requests.packages.urllib3.exceptions as urllib3_exceptions  #pylint: disable=ungrouped-imports
except ImportError:
    # Versions of the requests package prior to 1.2.0 did not vendor the urllib3 package.
    urllib3_exceptions = None

LOGGER = logging.getLogger(__name__)

############################################################################

# Name of project to upload to and query about
BLACKDUCK_PROJECT = "mongodb/mongo"

# Version of project to query about
# Black Duck automatically determines the version based on branch
BLACKDUCK_PROJECT_VERSION = "master"

# Timeout to wait for a Black Duck scan to complete
BLACKDUCK_TIMEOUT_SECS = 600

# Black Duck hub api uses this file to get settings
BLACKDUCK_RESTCONFIG = ".restconfig.json"

############################################################################

# Build Logger constants

BUILD_LOGGER_CREATE_BUILD_ENDPOINT = "/build"
BUILD_LOGGER_APPEND_GLOBAL_LOGS_ENDPOINT = "/build/%(build_id)s"
BUILD_LOGGER_CREATE_TEST_ENDPOINT = "/build/%(build_id)s/test"
BUILD_LOGGER_APPEND_TEST_LOGS_ENDPOINT = "/build/%(build_id)s/test/%(test_id)s"

BUILD_LOGGER_DEFAULT_URL = "https://logkeeper.mongodb.org"
BUILD_LOGGER_TIMEOUT_SECS = 65

LOCAL_REPORTS_DIR = "bd_reports"

############################################################################

THIRD_PARTY_COMPONENTS_FILE = "etc/third_party_components.yml"

############################################################################


def default_if_none(value, default):
    """Set default if value is 'None'."""
    return value if value is not None else default


# Derived from buildscripts/resmokelib/logging/handlers.py
class HTTPHandler(object):
    """A class which sends data to a web server using POST requests."""

    def __init__(self, url_root, username, password, should_retry=False):
        """Initialize the handler with the necessary authentication credentials."""

        self.auth_handler = requests.auth.HTTPBasicAuth(username, password)

        self.session = requests.Session()

        if should_retry:
            retry_status = [500, 502, 503, 504]  # Retry for these statuses.
            retry = urllib3_retry.Retry(
                backoff_factor=0.1,  # Enable backoff starting at 0.1s.
                method_whitelist=False,  # Support all HTTP verbs.
                status_forcelist=retry_status)

            adapter = requests.adapters.HTTPAdapter(max_retries=retry)
            self.session.mount('http://', adapter)
            self.session.mount('https://', adapter)

        self.url_root = url_root

    def _make_url(self, endpoint):
        return "%s/%s/" % (self.url_root.rstrip("/"), endpoint.strip("/"))

    def post(self, endpoint, data=None, headers=None, timeout_secs=BUILD_LOGGER_TIMEOUT_SECS):
        """Send a POST request to the specified endpoint with the supplied data.

        Return the response, either as a string or a JSON object based
        on the content type.
        """

        data = default_if_none(data, [])
        data = json.dumps(data)

        headers = default_if_none(headers, {})
        headers["Content-Type"] = "application/json; charset=utf-8"

        url = self._make_url(endpoint)

        LOGGER.info("POSTING to %s", url)

        with warnings.catch_warnings():
            if urllib3_exceptions is not None:
                try:
                    warnings.simplefilter("ignore", urllib3_exceptions.InsecurePlatformWarning)
                except AttributeError:
                    # Versions of urllib3 prior to 1.10.3 didn't define InsecurePlatformWarning.
                    # Versions of requests prior to 2.6.0 didn't have a vendored copy of urllib3
                    # that defined InsecurePlatformWarning.
                    pass

                try:
                    warnings.simplefilter("ignore", urllib3_exceptions.InsecureRequestWarning)
                except AttributeError:
                    # Versions of urllib3 prior to 1.9 didn't define InsecureRequestWarning.
                    # Versions of requests prior to 2.4.0 didn't have a vendored copy of urllib3
                    # that defined InsecureRequestWarning.
                    pass

            response = self.session.post(url, data=data, headers=headers, timeout=timeout_secs,
                                         auth=self.auth_handler, verify=True)

        response.raise_for_status()

        if not response.encoding:
            response.encoding = "utf-8"

        headers = response.headers

        if headers["Content-Type"].startswith("application/json"):
            return response.json()

        return response.text


# Derived from buildscripts/resmokelib/logging/buildlogger.py
class BuildloggerServer(object):
    # pylint: disable=too-many-instance-attributes
    """A remote server to which build logs can be sent.

    It is used to retrieve handlers that can then be added to logger
    instances to send the log to the servers.
    """

    def __init__(self, username, password, task_id, builder, build_num, build_phase, url):
        # pylint: disable=too-many-arguments
        """Initialize BuildloggerServer."""
        self.username = username
        self.password = password
        self.builder = builder
        self.build_num = build_num
        self.build_phase = build_phase
        self.url = url
        self.task_id = task_id

        self.handler = HTTPHandler(url_root=self.url, username=self.username,
                                   password=self.password, should_retry=True)

    def new_build_id(self, suffix):
        """Return a new build id for sending global logs to."""
        builder = "%s_%s" % (self.builder, suffix)
        build_num = int(self.build_num)

        response = self.handler.post(
            BUILD_LOGGER_CREATE_BUILD_ENDPOINT, data={
                "builder": builder,
                "buildnum": build_num,
                "task_id": self.task_id,
            })

        return response["id"]

    def new_test_id(self, build_id, test_filename, test_command):
        """Return a new test id for sending test logs to."""
        endpoint = BUILD_LOGGER_CREATE_TEST_ENDPOINT % {"build_id": build_id}

        response = self.handler.post(
            endpoint, data={
                "test_filename": test_filename,
                "command": test_command,
                "phase": self.build_phase,
                "task_id": self.task_id,
            })

        return response["id"]

    def post_new_file(self, build_id, test_name, lines):
        """Post a new file to the build logger server."""
        test_id = self.new_test_id(build_id, test_name, "foo")
        endpoint = BUILD_LOGGER_APPEND_TEST_LOGS_ENDPOINT % {
            "build_id": build_id,
            "test_id": test_id,
        }

        dt = datetime.datetime.now().isoformat()

        dlines = [(dt, line) for line in lines]

        try:
            self.handler.post(endpoint, data=dlines)
        except requests.HTTPError as err:
            # Handle the "Request Entity Too Large" error, set the max size and retry.
            raise ValueError("Encountered an HTTP error: %s" % (err))
        except requests.RequestException as err:
            raise ValueError("Encountered a network error: %s" % (err))
        except:  # pylint: disable=bare-except
            raise ValueError("Encountered an error.")


def _to_dict(items, func):
    dm = {}

    for i in items:
        tuple1 = func(i)
        dm[tuple1[0]] = tuple1[1]

    return dm


def _compute_security_risk(security_risk_profile):
    counts = security_risk_profile["counts"]

    cm = _to_dict(counts, lambda i: (i["countType"], int(i["count"])))

    priorities = ['CRITICAL', 'HIGH', 'MEDIUM', 'LOW', 'OK', 'UNKNOWN']

    for priority in priorities:
        if cm[priority] > 0:
            return priority

    return "OK"

@functools.total_ordering
class VersionInfo:
    """Parse and break apart version strings so they can be compared."""
    
    # TODO - special case ESR - we only care about ESR releases, not regular FireFox releases
    def __init__(self, ver_str):
        self.ver_str = ver_str
        self.production_version = True

        # Abseil has an empty string for one version
        if self.ver_str == "":
            self.production_version = False
            return

        # Special case Intel's Decimal library since it is just too weird
        if ver_str == "v2.0 U1":
            self.ver_array = [2, 0]
            return

        # "git" is an abseil version
        # "unknown_version" comes from this script when components do not have versions
        # icu has cldr, release, snv, milestone, latest
        # zlib has alt and task
        # boost has ubuntu, fc, old-boost, start, ....
        # BlackDuck thinks boost 1.70.0 was released on 2007 which means we have to check hundreds of versions
        bad_keywords = ["unknown_version", "rc", "alpha", "beta", "git", "release", "cldr" , "svn", "cvs", 
                        "milestone", "latest", "alt", "task", "ubuntu", "fc", "old-boost", "start", "split", "unofficial", "(", "ctp", "before" , "review", "develop", "master", "filesystem", "geometry", "icl", "intrusive", "old", "optional", "super", "docs", "+b", "-b", "b1", ".0a" , "system", "html", "interprocess" ]
        if [bad for bad in bad_keywords if bad in self.ver_str]:
            self.production_version = False
            return

        # Clean the version information
        # Some versions start with 'v'. Some components have a mix of 'v' and not 'v' prefixed versions so trim the 'v'
        # MongoDB versions start with 'r'
        if ver_str[0] == 'v' or ver_str[0] == 'r':
            self.ver_str = ver_str[1:]

        # Git hashes are not valid versions
        if len(self.ver_str) == 40 and bytes.fromhex(self.ver_str):
            self.production_version = False
            return

        # Clean out Mozilla's suffix
        self.ver_str = self.ver_str.replace("esr", "")

        # Clean out GPerfTool's prefix
        self.ver_str = self.ver_str.replace("gperftools-", "")

        # Clean out Yaml Cpp's prefix
        self.ver_str = self.ver_str.replace("yaml-cpp-", "")

        # Clean out Boosts's prefix
        self.ver_str = self.ver_str.replace("boost-", "")
        # Clean out Boosts's prefix
        self.ver_str = self.ver_str.replace("asio-", "")

        # Some versions end with "-\d", change the "-" since it just means a patch release
        self.ver_str = self.ver_str.replace("-", ".")

        # Versions are generally a multi-part integer tuple
        self.ver_array = [int(part) for part in self.ver_str.split(".")]

    def __repr__(self):
        return self.__str__()

    def __str__(self):
        return ".".join([str(val) for val in self.ver_array])


    def __eq__(self, other):
        return (self.production_version, self.ver_array) == (other.production_version, other.ver_array)

    def __gt__(self, other):
        if self.production_version != other.production_version:
            return self.production_version

        return self.ver_array > other.ver_array


def test_version_info():
    VersionInfo("v2.0 U1")
    VersionInfo("v1.1")
    VersionInfo("0.4.2-1")
    VersionInfo("7.0.2")
    VersionInfo("gperftools-2.8")
    VersionInfo("v1.5-rc2")
    VersionInfo("r4.7.0-alpha")
    VersionInfo("r4.2.10")
    VersionInfo("2.0.0.1")
    VersionInfo("7.0.2-2")
    VersionInfo("git")
    VersionInfo("20200225.2")
    VersionInfo('release-68-alpha')
    VersionInfo('cldr/2020-09-22')
    VersionInfo('release-67-rc')
    VersionInfo('66.1~rc')
    VersionInfo('release-66-rc')
    VersionInfo('release-66-preview')
    VersionInfo('65.1')
    VersionInfo('release-65-rc')
    VersionInfo('64.2-rc')
    VersionInfo('release-64-rc2')
    VersionInfo('release-63-rc')
    VersionInfo('last-cvs-commit')
    VersionInfo('last-svn-commit')
    VersionInfo('release-62-rc')
    VersionInfo('cldr-32-beta2')
    VersionInfo('release-60-rc')
    VersionInfo('milestone-60-0-1')
    VersionInfo('release-59-rc')
    VersionInfo('milestone-59-0-1')
    VersionInfo('release-58-2-eclipse-20170118')
    VersionInfo('tools-release-58')
    VersionInfo('icu-latest')
    VersionInfo('icu4j-latest')
    VersionInfo('icu4j-release-58-1')
    VersionInfo('icu4j-release-58-rc')
    VersionInfo('icu-release-58-rc')
    VersionInfo('icu-milestone-58-0-1')
    VersionInfo('icu4j-milestone-58-0-1')

    VersionInfo('yaml-cpp-0.6.3')

    VersionInfo('gb-c8-task188949.100')
    VersionInfo('1.2.8-alt1.M80C.1')
    VersionInfo('1.2.8-alt2')
    VersionInfo('1.1.4-1')
    VersionInfo('1.2.11')


    assert VersionInfo('7.0.2.2') > VersionInfo('7.0.0.1')
    assert VersionInfo('7.0.2.2') > VersionInfo('7.0.2')
    assert VersionInfo('7.0.2.2') > VersionInfo('3.1')
    assert not VersionInfo('7.0.2.2') > VersionInfo('8.0.2')


    VersionInfo('v1.7.0')
    VersionInfo('v1.6.1')
    VersionInfo('boost-1.74.0.beta1')
    VersionInfo('4.3.0')
    VersionInfo('1.71.0.3')
    VersionInfo('1.71.0.0ubuntu4')
    VersionInfo('1.71.0.0ubuntu3')
    VersionInfo('4.2.0')
    VersionInfo('1.71.0.0ubuntu2')
    VersionInfo('optional-2020-04-08')
    VersionInfo('optional-2020-04-05')
    VersionInfo('1.71.0.2')
    VersionInfo('1.71.0.1')
    VersionInfo('1.71.0.0ubuntu1')
    VersionInfo('1.67.0.2+b1')
    VersionInfo('1.67.0.2ubuntu1')
    VersionInfo('4.1.0')
    VersionInfo('4.0.1')
    VersionInfo('boost-1.72.0')
    VersionInfo('1.67.0.2')
    VersionInfo('boost-1.72.0.beta1')
    VersionInfo('4.0.0')
    VersionInfo('v1.6.0')
    VersionInfo('boost-1.73.0')
    VersionInfo('boost-1.73.0.beta1')
    VersionInfo('1.70.0.b1')
    VersionInfo('boost-1.70.0.beta1')
    VersionInfo('boost-1.71.0.beta1')
    VersionInfo('e8d104f34d1dd97c619a01639fe7b56bce5b671d')
    VersionInfo('efc694d679263652520d965b94debbec69a17d85')
    VersionInfo('3f46081c59edd66ea93294d02a962e929c67b03e')
    VersionInfo('1d347079b1096f4f5a3a6ea85dfcaf5143b42802')
    VersionInfo('04f0847af4c1e32d1ad4e785c514b5a49f439acc')
    VersionInfo('81d60863b6382547cc0cb070cf1f13f82411c386')
    VersionInfo('9abce00f240f4d7b38c335c1f86262e7ea16018d')
    VersionInfo('d67200bd2a1f67135a4c677636546ec9615e21ea')
    VersionInfo('c6b59c05995e2e467c794f5f9673aba662109116')
    VersionInfo('866d546fd0d75105be76654a6bb3a1b4cdbb4087')
    VersionInfo('c22cc0812d7c3c615e57d5bd6ddbde4b1457ac57')
    VersionInfo('2f9b23ea4de479518781b59b4f57cd480e4308c0')
    VersionInfo('7dc1712f2df87f28bfd02ab26d3af25c7823eb2c')
    VersionInfo('8d02ed770bbd22c7573b625657d8cc44aa19351a')
    VersionInfo('7dbaa8541362bb2011f766cf16e6d1f0169af375')
    VersionInfo('cc5aefee30dc349360e8030464503c5641840e7b')
    VersionInfo('67102d043643c51104663c5b00e375c2f476cdf5')
    VersionInfo('622c7c4ee6046943164fc7bde49023369969de92')
    VersionInfo('dc794c0ab2b3b58bc566c0d24239ecef6988eee5')
    VersionInfo('8824e81d52489be9c8cb21d8f8477b03e675ce66')
    VersionInfo('9189a761b79fcd4be2f38158b9cad164bac22fa2')
    VersionInfo('3cc4107d01b152bc2417d434b6fa22925a34aecd')
    VersionInfo('40a0e4b89697cba0b1784867a29773e5f074e55b')
    VersionInfo('05dda09fd3b4312594144b5a8617ba6fc694a2d6')
    VersionInfo('ae4fde2e2ae88291d6d656137169ff4003d184a1')
    VersionInfo('dff5644a1198667127d4549ee271288123f54929')
    VersionInfo('5d3e39325e13a74645b60af33b16e8369f5ee61d')
    VersionInfo('28df5bc4821bd5d17ffc9e77c0f2b3c271e509b2')
    VersionInfo('f01148e421d2072294a6bc5faf70a5f6d4a709c2')
    VersionInfo('247e1824297d84cad9ecfca78779523d24f9604a')
    VersionInfo('1.67.0.1')
    VersionInfo('1.69.0b1')
    VersionInfo('optional-2018-11-08')
    VersionInfo('boost-1.69.0-beta1')
    VersionInfo('optional-2018-10-29')
    VersionInfo('before-wrapexcept')
    VersionInfo('before-1.69-merge')
    VersionInfo('6f72675bff276599ffdccb9681018d8b2e367bc2')
    VersionInfo('a4bcd091f266fd173b664688e5bd677698b45a73')
    VersionInfo('bc0c90a70f0435ee4ce65b97a68d864a7d627e35')
    VersionInfo('67915a7f8610247b7bac4ca41b03702b54f4933a')
    VersionInfo('1.68.0 VersionInfo(1) VersionInfo(2)')
    VersionInfo('1559df6b194cbba86653a2e2fefc2ea15e2cecd2')
    VersionInfo('1.68.0 VersionInfo(8)')
    VersionInfo('4980fe2a87ead3b2e8260aed6e9c8835266cd5d7')
    VersionInfo('e8607b3eea238e590eca93bfe498c21f470155c1')
    VersionInfo('6187d448df49db085d4d7ad63d3a5f479594c5cf')
    VersionInfo('36f969a4c5129d11a8913fcbd8d2732ba957315b')
    VersionInfo('1.68.0 VersionInfo(10)')
    VersionInfo('815268534fc04adf00c9f5bd6803328e20fe14c1')
    VersionInfo('1.67.0.0ubuntu1')
    VersionInfo('optional-2018-07-16')
    VersionInfo('v1.5.0')
    VersionInfo('optional-2018-07-03')
    VersionInfo('icl-2018-07-02')
    VersionInfo('1.67.0 VersionInfo(4)')
    VersionInfo('223b2cf3a5d633c5a4f11e07ce242f51bcf61a06')
    VersionInfo('1.67.0 VersionInfo(27)')
    VersionInfo('1.67.0 VersionInfo(28)')
    VersionInfo('1.67.0 VersionInfo(5)')
    VersionInfo('cb59c5cff1d044d875b4c5984a46efcffa6c9df5')
    VersionInfo('boost-1.74.0')
    VersionInfo('boost-1.67.0-beta1')
    VersionInfo('v1.4.0')
    VersionInfo('bfcbfe3c58064cd1ffabbce49a95c6c20351c96e')
    VersionInfo('1.66.0 VersionInfo(4)')
    VersionInfo('590ae13271ec08a8e169ea2b50624f7bd5e88cd9')
    VersionInfo('1.66.0 VersionInfo(8)')
    VersionInfo('1.66.0 VersionInfo(5)')
    VersionInfo('8f3aea2200fa45ed4c1829b3d3148432867dda87')
    VersionInfo('boost-1.66.0-beta1')
    VersionInfo('v1.3.0')
    VersionInfo('1.65.1.0ubuntu1')
    VersionInfo('1.65.1 VersionInfo(5)')
    VersionInfo('1.65.1 VersionInfo(3)')
    VersionInfo('2c5de25f2d1aea9316129027e6617649a0e09e59')
    VersionInfo('1.65.0-alt1')
    VersionInfo('gb-sisyphus-task187562.100')
    VersionInfo('1.65.0 VersionInfo(4)')
    VersionInfo('asio-1.10.10')
    VersionInfo('d53033eec64f806986f01641ee70bbf11c5db8cc')
    VersionInfo('boost-1.65.0-beta1')
    VersionInfo('v1.2.0')
    VersionInfo('super-project-bb754c0')
    VersionInfo('super-project-5ec478a570')
    VersionInfo('1.64.0 VersionInfo(5)')
    VersionInfo('1.64.0 VersionInfo(1)')
    VersionInfo('1.64.0 VersionInfo(9)')
    VersionInfo('82f588f24ed2640bca4bf3f31dfcb06703d9bc13')
    VersionInfo('1.64.0 VersionInfo(7)')
    VersionInfo('v1.63.0-0')
    VersionInfo('asio-1.10.9')
    VersionInfo('1.64.0 VersionInfo(10)')
    VersionInfo('v1.1.0')
    VersionInfo('1.57.0-vc140ctp6')
    VersionInfo('gb-sisyphus-task177096.100')
    VersionInfo('1.63.0-alt1')
    VersionInfo('v1.0.2')
    VersionInfo('v1.57.0-0')
    VersionInfo('1.63.0_b1')
    VersionInfo('1.63.0.beta.1')
    VersionInfo('boost-1.63.0-beta1')
    VersionInfo('1.62.0.1')
    VersionInfo('boost-1.64.0-beta1')
    VersionInfo('boost-1.64.0-beta2')
    VersionInfo('20120824')
    VersionInfo('asio-1.10.8')
    VersionInfo('1.62.0.beta.2')
    VersionInfo('1.62.0.beta.1')
    VersionInfo('boost-1.62.0-beta1')
    VersionInfo('v1.0.1')
    VersionInfo('1.61.0.2')
    VersionInfo('1.58.0.2')
    VersionInfo('1.61.0 VersionInfo(7)')
    VersionInfo('1.61.0 VersionInfo(3)')
    VersionInfo('gb-sisyphus-task164640.100')
    VersionInfo('1.58.0-alt4')
    VersionInfo('1.58.0.1ubuntu1')
    VersionInfo('1.61.0.beta.1')
    VersionInfo('1.58.0-alt3')
    VersionInfo('gb-sisyphus-task159698.11474')
    VersionInfo('v1.0.0')
    VersionInfo('1.58.0-alt2')
    VersionInfo('gb-sisyphus-task162305.100')
    VersionInfo('boost-1.61.0-beta1')
    VersionInfo('gb-sisyphus-task161801.100')
    VersionInfo('1.58.0-alt1.1.2')
    VersionInfo('1.58.0-alt1.1.1')
    VersionInfo('gb-sisyphus-task161271.300')
    VersionInfo('1.63.0')
    VersionInfo('optional-2016-02-22')
    VersionInfo('linux-binary')
    VersionInfo('gb-sisyphus-task158499.100')
    VersionInfo('1.58.0-alt1.1')
    VersionInfo('0.5')
    VersionInfo('asio-1.10.7')
    VersionInfo('1.60.0.beta.1')
    VersionInfo('boost-1.60.0-beta1')
    VersionInfo('icl-2015-11-22')
    VersionInfo('v0.6.0')
    VersionInfo('1.55.0.1')
    VersionInfo('v0.5.0')
    VersionInfo('0.4')
    VersionInfo('1.58.0.1')
    VersionInfo('1.58.0.0ubuntu1')
    VersionInfo('1.59.0-b1')
    VersionInfo('1.59.0.beta.1')
    VersionInfo('1.57.0.1')
    VersionInfo('boost-review')
    VersionInfo('v0.4.0')
    VersionInfo('')
    VersionInfo('v0.3.0')
    VersionInfo('1.58.0-alt1')
    VersionInfo('gb-sisyphus-task144530.40')
    VersionInfo('1.58.0-vs140rc')
    VersionInfo('v0.2.0')
    VersionInfo('1.58.0-vs140ctp60')
    VersionInfo('1.58.0-vs140ctp6')
    VersionInfo('1.58.0-rc2')
    VersionInfo('asio-1.10.6')
    VersionInfo('1.58.0.beta.1')
    VersionInfo('1.58.0-b1rc2')
    VersionInfo('1.58.0-b1rc1')
    VersionInfo('1.57.0-vc140ctp61')
    VersionInfo('1.57.0-vc140ctp60')
    VersionInfo('gb-sisyphus-task138333.100')
    VersionInfo('1.57.0-alt4')
    VersionInfo('1.57.0-rc1')
    VersionInfo('1.57.0.beta.1')
    VersionInfo('1.57.0-b1z')
    VersionInfo('1.57.0-b1y')
    VersionInfo('1.57.0-b10')
    VersionInfo('1.57.0-b1x')
    VersionInfo('1.57.0-b1rc1')
    VersionInfo('boost-1.57.0-beta1')
    VersionInfo('asio-1.10.5')
    VersionInfo('v0.2_docs')
    VersionInfo('1.51.0a')
    VersionInfo('geometry-1.56.0')
    VersionInfo('v0.1_docs')
    VersionInfo('1.56.0-rc3')
    VersionInfo('1.56.0-rc2')
    VersionInfo('1.56.0-rc1')
    VersionInfo('asio-1.10.4')
    VersionInfo('filesystem-2014-07-23')
    VersionInfo('1.56.0-b1')
    VersionInfo('system-2014-07-17')
    VersionInfo('geometry-1.56.0-beta1')
    VersionInfo('0.15')
    VersionInfo('1.56.0.beta.1')
    VersionInfo('boost-1.56.0-beta1')
    VersionInfo('v0.3')
    VersionInfo('asio-1.10.3')
    VersionInfo('system-2014-06-02')
    VersionInfo('1.55.0.2')
    VersionInfo('v0.2')
    VersionInfo('v3.0_html')
    VersionInfo('v3.0')
    VersionInfo('asio-1.10.2')
    VersionInfo('asio-1.10.1')
    VersionInfo('v2.4')
    VersionInfo('v2.3')
    VersionInfo('v0.1')
    VersionInfo('1.60.0')
    VersionInfo('1.62.0')
    VersionInfo('1.59.0')
    VersionInfo('1.58.0')
    VersionInfo('1.61.0')
    VersionInfo('intrusive-1.56.00.b0')
    VersionInfo('interprocess-1.56.00.b0')
    VersionInfo('1.55.0.16')
    VersionInfo('converted-develop')
    VersionInfo('converted-master')
    VersionInfo('1.55.0.15')
    VersionInfo('svn-trunk')
    VersionInfo('svn-release')
    VersionInfo('1.55.0.10')
    VersionInfo('old-boost-1.55.0')
    VersionInfo('1.55.0')
    VersionInfo('v1.0_docs')
    VersionInfo('1.54.0.157')
    VersionInfo('1.57.0')
    VersionInfo('1.56.0')
    VersionInfo('1.54.0.1ubuntu1')
    VersionInfo('1.55.0.beta.1')
    VersionInfo('svn-master')
    VersionInfo('svn-develop')
    VersionInfo('1.54.0.6')
    VersionInfo('1.54.0.3')
    VersionInfo('1.54.0.1')
    VersionInfo('1.54.0.2')
    VersionInfo('1.54.0.0')
    VersionInfo('1.54.0-unofficial')
    VersionInfo('1.54.0')
    VersionInfo('1.54.0.beta.1')
    VersionInfo('1.54.0_beta1')
    VersionInfo('1.53.0-alt3')
    VersionInfo('gb-sisyphus-task89971.400')
    VersionInfo('1.53.0-alt2.1')
    VersionInfo('1.53.0-alt2')
    VersionInfo('gb-sisyphus-task89550.100')
    VersionInfo('1.52.0-alt2')
    VersionInfo('gb-sisyphus-task89217.100')
    VersionInfo('1.53.0')
    VersionInfo('201104')
    VersionInfo('1.53.0.beta.1')
    VersionInfo('1.53.0beta1')
    VersionInfo('gb-sisyphus-task84525.200')
    VersionInfo('1.52.0-alt1')
    VersionInfo('1.52.0')
    VersionInfo('1.52.0.beta.1')
    VersionInfo('1.52.0beta1')
    VersionInfo('gb-sisyphus-task81524')
    VersionInfo('1.51.0-alt4')
    VersionInfo('v2.2')
    VersionInfo('1.51.0-alt3')
    VersionInfo('gb-sisyphus-task79056')
    VersionInfo('1.49.0-alt4')
    VersionInfo('gb-sisyphus-task78697')
    VersionInfo('1.51.0')
    VersionInfo('1.50.0')
    VersionInfo('1.49.0-alt3')
    VersionInfo('gb-sisyphus-task74294')
    VersionInfo('boost-1.50.0-beta1')
    VersionInfo('1.50.0beta1')
    VersionInfo('1.49.0-b1')
    VersionInfo('1.49.0-alt2')
    VersionInfo('1.49.0.1')
    VersionInfo('1.49.0-alt1')
    VersionInfo('1.49.0')
    VersionInfo('1.48.0-alt2')
    VersionInfo('boost-1.49.0-beta1')
    VersionInfo('1.49.0.beta.1')
    VersionInfo('1.48.0.2')
    VersionInfo('1.48.0-alt1')
    VersionInfo('1.48.0')
    VersionInfo('3.1.19')
    VersionInfo('boost-1.48.0-beta1')
    VersionInfo('1.48.0.beta.1')
    VersionInfo('v2.1')
    VersionInfo('1.47.0-alt2.1')
    VersionInfo('1.47.0-alt2')
    VersionInfo('1.47.0-b1')
    VersionInfo('1.47.0-alt1')
    VersionInfo('1.47.0')
    VersionInfo('boost-1.47.0-beta1')
    VersionInfo('1.47.0.beta.1')
    VersionInfo('0.0.1')
    VersionInfo('1.46.1-alt1')
    VersionInfo('1.46.1')
    VersionInfo('1.46.0-alt2')
    VersionInfo('1.46.0-alt1')
    VersionInfo('1.46.0')
    VersionInfo('boost-1.46.0-beta1')
    VersionInfo('1.46.0.beta.1')
    VersionInfo('1.45.0-alt6')
    VersionInfo('1.45.0-alt5')
    VersionInfo('1.45.0-alt3')
    VersionInfo('1.45.0-alt2')
    VersionInfo('1.45.0-alt1')
    VersionInfo('1.42.0-alt3')
    VersionInfo('1.45.0')
    VersionInfo('boost-1.45.0-beta1')
    VersionInfo('1.45.0.beta.1')
    VersionInfo('1.44.0')
    VersionInfo('boost-1.44.0-beta1')
    VersionInfo('1.44.0.beta.1')
    VersionInfo('boost-1_44_0-0_3_fc14')
    VersionInfo('boost-1_44_0-0_2_fc14')
    VersionInfo('boost-1_44_0-0_1_fc14')
    VersionInfo('boost-1_41_0-13_fc14')
    VersionInfo('boost-1_41_0-9_fc13')
    VersionInfo('boost-1_41_0-12_fc14')
    VersionInfo('boost-1_41_0-11_fc14')
    VersionInfo('boost-1_41_0-8_fc13')
    VersionInfo('boost-1_41_0-10_fc14')
    VersionInfo('1.43.0')
    VersionInfo('boost-1_41_0-9_fc14')
    VersionInfo('1.42.0.1')
    VersionInfo('1.43.0.beta1')
    VersionInfo('1.43.0.beta.1')
    VersionInfo('boost-1_41_0-8_fc14')
    VersionInfo('3.1.18')
    VersionInfo('boost-1_41_0-7_fc13')
    VersionInfo('boost-1_41_0-7_fc14')
    VersionInfo('1.42.0-alt2')
    VersionInfo('1.42.0-alt1')
    VersionInfo('1.41.0-alt1')
    VersionInfo('boost-1_41_0-6_fc13')
    VersionInfo('F-13-split')
    VersionInfo('F-13-start')
    VersionInfo('1.40.0-alt1')
    VersionInfo('1.42.0')
    VersionInfo('boost-1_37_0-9_fc11')
    VersionInfo('boost-1_41_0-4_fc13')
    VersionInfo('1.42.0.beta.1')
    VersionInfo('boost-1_41_0-3_fc13')
    VersionInfo('boost-1_41_0-2_fc13')
    VersionInfo('1.41.0')
    VersionInfo('boost-1_39_0-9_fc12')
    VersionInfo('boost-1_39_0-11_fc13')
    VersionInfo('boost-1_39_0-10_fc13')
    VersionInfo('1.39.0-alt3.1')
    VersionInfo('boost-1.41.0-beta1')
    VersionInfo('1.41.0.beta.1')
    VersionInfo('boost-1_39_0-8_fc12')
    VersionInfo('boost-1_39_0-9_fc13')
    VersionInfo('boost-1_37_0-8_fc11')
    VersionInfo('boost-1_39_0-7_fc12')
    VersionInfo('boost-1_39_0-8_fc13')
    VersionInfo('boost-1_39_0-7_fc13')
    VersionInfo('F-12-start')
    VersionInfo('boost-1_39_0-6_fc12')
    VersionInfo('F-12-split')
    VersionInfo('1.40.0')
    VersionInfo('boost-1_39_0-5_fc12')
    VersionInfo('1.40.0.beta.1')
    VersionInfo('boost-1_39_0-4_fc12')
    VersionInfo('boost-1_37_0-7_fc11')
    VersionInfo('boost-1_39_0-3_fc12')
    VersionInfo('1.39.0-alt3')
    VersionInfo('1.39.0-alt2')
    VersionInfo('1.39.0-alt1')
    VersionInfo('1.36.0-alt7')
    VersionInfo('boost-1_39_0-2_fc12')
    VersionInfo('1.36.0-alt6')
    VersionInfo('1.36.0-alt5')
    VersionInfo('boost-1_39_0-1_fc12')
    VersionInfo('boost-1_37_0-7_fc12')
    VersionInfo('1.39.0')
    VersionInfo('1.39.0 beta 1')
    VersionInfo('boost-1.39.0-beta1')
    VersionInfo('F-11-start')
    VersionInfo('boost-1_37_0-6_fc11')
    VersionInfo('F-11-split')
    VersionInfo('boost-1_37_0-5_fc11')
    VersionInfo('boost-1_37_0-4_fc11')
    VersionInfo('1.38.0')
    VersionInfo('1.38.0 beta 2')
    VersionInfo('boost-1_37_0-3_fc11')
    VersionInfo('boost-1_34_1-18_fc10')
    VersionInfo('boost-1_34_1-16_fc9')
    VersionInfo('boost-1_37_0-2_fc11')
    VersionInfo('boost-1_37_0-1_fc11')
    VersionInfo('3.1.17')
    VersionInfo('1.37.0')
    VersionInfo('1.37.0 beta 1')
    VersionInfo('boost-1.37.0-beta1')
    VersionInfo('F-10-split')
    VersionInfo('boost-1_34_1-17_fc10')
    VersionInfo('F-10-start')
    VersionInfo('1.36.0a')
    VersionInfo('1.36.0')
    VersionInfo('boost-1_36_0-0_1_beta1_fc10')
    VersionInfo('1.36.0 beta')
    VersionInfo('boost-1.36.0-beta1')
    VersionInfo('boost-1_34_1-16_fc10')
    VersionInfo('boost-1_34_1-15_fc9')
    VersionInfo('boost-1_34_1-15_fc10')
    VersionInfo('boost-1_34_1-14_fc9')
    VersionInfo('1.35.0 VersionInfo(1) VersionInfo(1)')
    VersionInfo('1.35.0 VersionInfo(1) VersionInfo(2)')
    VersionInfo('1.35.0')
    VersionInfo('boost-1.35.0-rc3')
    VersionInfo('F-9-start')
    VersionInfo('F-9-split')
    VersionInfo('boost-1_34_1-13_fc9')
    VersionInfo('1.34.1-alt1')
    VersionInfo('boost-1.35.0-rc1')
    VersionInfo('boost-1_34_1-12_fc9')
    VersionInfo('boost-1_34_1-11_fc9')
    VersionInfo('boost-1_34_1-10_fc9')
    VersionInfo('boost-1_34_1-9_fc9')
    VersionInfo('boost-1_34_1-8_fc9')
    VersionInfo('1.34.0-alt5.1')
    VersionInfo('1.34.0-alt4')
    VersionInfo('boost-1_33_1-15_fc7')
    VersionInfo('boost-1_34_1-7_fc8')
    VersionInfo('boost-1_34_1-7_fc9')
    VersionInfo('boost-1_34_1-6_fc8')
    VersionInfo('boost-1_34_1-6_fc9')
    VersionInfo('3.1.16')
    VersionInfo('boost-1_33_1-14_fc7')
    VersionInfo('1.68.0')



    ('60.4.0')
    ('60.4.0esr')
    ('60.5.0')
    ('60.5.0esr')
    ('60.5.1')
    ('60.5.1esr')
    ('60.5.2')
    ('60.5.2esr')
    ('60.6.0')
    ('60.6.0esr')
    ('60.6.1')
    ('60.6.1esr')
    ('60.6.2-esr')
    ('60.6.2')
    ('60.6.2esr')
    ('60.6.3-esr')
    ('60.6.3')
    ('60.6.3esr')
    ('60.7.0-esr')
    ('60.7.0')
    ('60.7.0esr')
    ('60.7.1')
    ('60.7.1esr')
    ('60.7.2')
    ('60.7.2esr')
    ('60.8.0')
    ('60.8.0esr')
    ('60.9.0')
    ('60.9.0esr')
    ('63.0.1')
    ('63.0.1+build4.1')
    ('63.0.3')
    ('63.0.3+build1')
    ('64.0.1')
    ('64.0.2')
    ('64.0.2+build1')
    ('64.0')
    ('64.0+build1')
    ('64.0+build3')
    ('64.0~b12')
    ('64.0b10')
    ('64.0b11')
    ('64.0b12')
    ('64.0b13')
    ('64.0b14')
    ('64.0b4')
    ('64.0b5')
    ('64.0b6')
    ('64.0b7')
    ('64.0b8')
    ('64.0b9')
    ('65.0-b10')
    ('65.0-b11')
    ('65.0-b12')
    ('65.0-b3')
    ('65.0-b4')
    ('65.0-b5')
    ('65.0-b6')
    ('65.0-b7')
    ('65.0-b8')
    ('65.0.1')
    ('65.0.1+build2')
    ('65.0.2')
    ('65.0')
    ('65.0+build2')
    ('65.0b9')
    ('66.0-b10')
    ('66.0-b11')
    ('66.0-b12')
    ('66.0-b13')
    ('66.0-b14')
    ('66.0-b3')
    ('66.0-b4')
    ('66.0-b5')
    ('66.0-b6')
    ('66.0-b7')
    ('66.0-b8')
    ('66.0-b9')
    ('66.0.1')
    ('66.0.1+build1')
    ('66.0.2')
    ('66.0.2+build1')
    ('66.0.3')
    ('66.0.3+build1')
    ('66.0.4')
    ('66.0.4+build3')
    ('66.0.5')
    ('66.0.5+build1')
    ('66.0')
    ('66.0+build2')
    ('66.0+build3')
    ('66.0b10')
    ('66.0b11')
    ('66.0b12')
    ('66.0b13')
    ('66.0b14')
    ('66.0b3')
    ('66.0b4')
    ('66.0b5')
    ('66.0b6')
    ('66.0b7')
    ('66.0b8')
    ('66.0b9')
    ('67.0-b10')
    ('67.0-b11')
    ('67.0-b12')
    ('67.0-b13')
    ('67.0-b15')
    ('67.0-b16')
    ('67.0-b17')
    ('67.0-b18')
    ('67.0-b19')
    ('67.0-b3')
    ('67.0-b4')
    ('67.0-b5')
    ('67.0-b6')
    ('67.0-b7')
    ('67.0-b8')
    ('67.0-b9')
    ('67.0.1')
    ('67.0.1+build1')
    ('67.0.2')
    ('67.0.2+build1')
    ('67.0.2+build2')
    ('67.0.3')
    ('67.0.3+build1')
    ('67.0.4')
    ('67.0.4+build1')
    ('67.0')
    ('67.0+build1')
    ('67.0+build2')
    ('67.0b-14')
    ('67.0b10')
    ('67.0b11')
    ('67.0b12')
    ('67.0b13')
    ('67.0b14')
    ('67.0b15')
    ('67.0b16')
    ('67.0b17')
    ('67.0b18')
    ('67.0b19')
    ('67.0b3')
    ('67.0b4')
    ('67.0b5')
    ('67.0b6')
    ('67.0b7')
    ('67.0b8')
    ('67.0b9')
    ('68.0-b10')
    ('68.0-b11')
    ('68.0-b12')
    ('68.0-b13')
    ('68.0-b14')
    ('68.0-b3')
    ('68.0-b4')
    ('68.0-b5')
    ('68.0-b6')
    ('68.0-b7')
    ('68.0-b8')
    ('68.0-b9')
    ('68.0.1')
    ('68.0.1+build1')
    ('68.0.1esr')
    ('68.0.2')
    ('68.0.2+build1')
    ('68.0.2esr')
    ('68.0')
    ('68.0+build1')
    ('68.0+build2')
    ('68.0+build3')
    ('68.0~b12')
    ('68.0~b6')
    ('68.0~b8')
    ('68.0~b9')
    ('68.0esr')
    ('68.1.0')
    ('68.1.0esr')
    ('68.10.0')
    ('68.10.0esr')
    ('68.10.1')
    ('68.11.0')
    ('68.11.0esr')
    ('68.12.0')
    ('68.12.0esr')
    ('68.2.0')
    ('68.2.0esr')
    ('68.3.0')
    ('68.3.0esr')
    ('68.4.0esr')
    ('68.4.1')
    ('68.4.1esr')
    ('68.4.2')
    ('68.4.2esr')
    ('68.5.0')
    ('68.5.0esr')
    ('68.6.0')
    ('68.6.0esr')
    ('68.6.1')
    ('68.6.1esr')
    ('68.7.0')
    ('68.7.0esr')
    ('68.8.0')
    ('68.8.0esr')
    ('68.9.0')
    ('68.9.0esr')
    ('69.0.1')
    ('69.0.1+build1')
    ('69.0.2')
    ('69.0.2+build1')
    ('69.0.3')
    ('69.0.3+build1')
    ('69.0')
    ('69.0+build2')
    ('69.0b10')
    ('69.0b11')
    ('69.0b12')
    ('69.0b13')
    ('69.0b14')
    ('69.0b15')
    ('69.0b16')
    ('69.0b3')
    ('69.0b4')
    ('69.0b5')
    ('69.0b6')
    ('69.0b7')
    ('69.0b8')
    ('69.0b9')
    ('70.0.1')
    ('70.0.1+build1')
    ('70.0')
    ('70.0+build2')
    ('70.0b10')
    ('70.0b11')
    ('70.0b12')
    ('70.0b13')
    ('70.0b14')
    ('70.0b3')
    ('70.0b4')
    ('70.0b5')
    ('70.0b6')
    ('70.0b7')
    ('70.0b8')
    ('70.0b9')
    ('71.0')
    ('71.0+build2')
    ('71.0+build5')
    ('71.0b10')
    ('71.0b11')
    ('71.0b12')
    ('71.0b3')
    ('71.0b4')
    ('71.0b5')
    ('71.0b6')
    ('71.0b7')
    ('71.0b8')
    ('71.0b9')
    ('72.0.1')
    ('72.0.1+build1')
    ('72.0.2')
    ('72.0.2+build1')
    ('72.0')
    ('72.0+build4')
    ('72.0b1')
    ('73.0.1')
    ('73.0.1+build1')
    ('73.0')
    ('73.0+build1')
    ('73.0+build2')
    ('73.0+build3')
    ('74.0.1')
    ('74.0.1+build1')
    ('74.0')
    ('74.0+build1')
    ('74.0+build2')
    ('74.0+build3')
    ('75.0')
    ('75.0+build1')
    ('75.0+build3')
    ('76.0.1')
    ('76.0.1+build1')
    ('76.0')
    ('76.0+build1')
    ('76.0+build2')
    ('77.0.1')
    ('77.0.1+build1')
    ('77.0')
    ('77.0+build1')
    ('77.0+build2')
    ('77.0+build3')
    ('77.0b1')
    ('78.0.1')
    ('78.0.1+build1')
    ('78.0.1esr')
    ('78.0.2')
    ('78.0.2+build2')
    ('78.0.2esr')
    ('78.0')
    ('78.0+build1')
    ('78.0+build2')
    ('78.0b1')
    ('78.0b2')
    ('78.0b3')
    ('78.0b4')
    ('78.0b5')
    ('78.0b6')
    ('78.0b7')
    ('78.0b8')
    ('78.0b9')
    ('78.0esr')
    ('78.1.0')
    ('78.1.0esr')
    ('78.2.0')
    ('78.2.0esr')
    ('78.3.0')
    ('78.3.0esr')
    ('78.3.1')
    ('79.0')
    ('79.0+build1')
    ('79.0b1')
    ('79.0b2')
    ('79.0b3')
    ('79.0b4')
    ('79.0b5')
    ('79.0b6')
    ('79.0b7')
    ('79.0b8')
    ('79.0b9')
    ('80.0.1')
    ('80.0.1+build1')
    ('80.0')
    ('80.0+build2')
    ('80.0b1')
    ('80.0b2')
    ('80.0b3')
    ('80.0b4')
    ('80.0b5')
    ('80.0b6')
    ('80.0b7')
    ('80.0b8')
    ('81.0.1')
    ('81.0.1+build1')
    ('81.0')
    ('81.0+build1')
    ('81.0+build2')
    ('81.0b1')
    ('81.0b2')
    ('81.0b3')
    ('81.0b4')
    ('81.0b5')
    ('81.0b6')
    ('81.0b7')
    ('81.0b8')
    ('81.0b9')
    ('82.0b1')
    ('82.0b2')
    ('imports/c7-alt/firefox-60.2.2-2.el7_5')
    ('imports/c7-alt/firefox-60.4.0-1.el7')
    ('imports/c7-alt/firefox-60.5.0-2.el7')
    ('imports/c7-alt/firefox-60.5.1-1.el7_6')
    ('imports/c7-alt/firefox-60.6.0-3.el7_6')
    ('imports/c7-alt/firefox-60.6.1-1.el7_6')
    ('imports/c7/firefox-60.2.2-2.el7_5')
    ('imports/c7/firefox-60.3.0-1.el7_5')
    ('imports/c7/firefox-60.4.0-1.el7')
    ('imports/c7/firefox-60.5.0-2.el7')
    ('imports/c7/firefox-60.5.1-1.el7_6')
    ('imports/c7/firefox-60.6.0-3.el7_6')
    ('imports/c7/firefox-60.6.1-1.el7_6')

class Component:
    """
    Black Duck Component description.

    Contains a subset of information about a component extracted from Black Duck for a given project and version
    """

    def __init__(self, name, version, licenses, policy_status, security_risk, newer_releases):
        # pylint: disable=too-many-arguments
        """Initialize Black Duck component."""
        self.name = name
        self.version = version
        self.licenses = licenses
        self.policy_status = policy_status
        self.security_risk = security_risk
        self.newer_releases = newer_releases

    @staticmethod
    def parse(hub, component):
        """Parse a Black Duck component from a dictionary."""
        name = component["componentName"]
        cversion = component.get("componentVersionName", "unknown_version")
        licenses = ",".join([a.get("spdxId", a["licenseDisplay"]) for a in component["licenses"]])

        policy_status = component["policyStatus"]
        security_risk = _compute_security_risk(component['securityRiskProfile'])

        newer_releases = component["activityData"].get("newerReleases", 0)

        print(f"Comp {name} - {cversion}   Releases {newer_releases}")
        cver = VersionInfo(cversion)
        # Blackduck's newerReleases is based on "releasedOn" date. This means that if a upstream component releases a beta or rc, it counts as newer but we do not consider those newer for our purposes
        # Missing newerReleases means we do not have to upgrade
        # TODO - remove skip of FireFox since it has soooo many versions
        #if newer_releases > 0 and name not in ("Mozilla Firefox", "Boost C++ Libraries - boost"):
        if newer_releases > 0:
            print(f"Comp {name} - {cversion}   Releases {newer_releases}")
            limit = newer_releases + 1
            versions_url = component["component"] + f"/versions?sort=releasedon%20desc&limit={limit}"
            vjson = hub.execute_get(versions_url).json()

            versions = [(ver["versionName"], ver["releasedOn"]) for ver in vjson["items"]]

      
            print(versions)

            
            ver_info = [VersionInfo(ver["versionName"]) for ver in vjson["items"]]
            ver_info = [ver for ver in ver_info if ver.production_version == True]
            print(ver_info)

            ver_info = sorted([ver for ver in ver_info if ver.production_version == True and ver > cver])
            print(ver_info)
        # else:
        #     LOGGER.warn(f"Missing version upgrade information for {name}")

        return Component(name, cversion, licenses, policy_status, security_risk, newer_releases)


class BlackDuckConfig:
    """
    Black Duck configuration settings.

    Format is defined by Black Duck Python hub API.
    """

    def __init__(self):
        """Init Black Duck config from disk."""
        if not os.path.exists(BLACKDUCK_RESTCONFIG):
            raise ValueError("Cannot find %s for blackduck configuration" % (BLACKDUCK_RESTCONFIG))

        with open(BLACKDUCK_RESTCONFIG, "r") as rfh:
            rc = json.loads(rfh.read())

        self.url = rc["baseurl"]
        self.username = rc["username"]
        self.password = rc["password"]


def _run_scan():
    # Get user name and password from .restconfig.json
    bdc = BlackDuckConfig()

    with tempfile.NamedTemporaryFile() as fp:
        fp.write(f"""#/!bin/sh
curl --retry 5 -s -L https://detect.synopsys.com/detect.sh  | bash -s -- --blackduck.url={bdc.url} --blackduck.username={bdc.username} --blackduck.password={bdc.password} --detect.report.timeout={BLACKDUCK_TIMEOUT_SECS} --snippet-matching --upload-source --detect.wait.for.results=true
""".encode())
        fp.flush()

        subprocess.call(["/bin/sh", fp.name])


def _scan_cmd_args(args):
    # pylint: disable=unused-argument
    LOGGER.info("Running Black Duck Scan")

    _run_scan()


def _query_blackduck():

    hub = HubInstance()

    LOGGER.info("Fetching project %s from blackduck", BLACKDUCK_PROJECT)
    project = hub.get_project_by_name(BLACKDUCK_PROJECT)

    LOGGER.info("Fetching project version %s from blackduck", BLACKDUCK_PROJECT_VERSION)
    version = hub.get_version_by_name(project, BLACKDUCK_PROJECT_VERSION)

    LOGGER.info("Getting version components from blackduck")
    bom_components = hub.get_version_components(version)

    components = [Component.parse(hub, comp) for comp in bom_components["items"]]

    return components


class TestResultEncoder(json.JSONEncoder):
    """JSONEncoder for TestResults."""

    def default(self, o):
        """Serialize objects by default as a dictionary."""
        # pylint: disable=method-hidden
        return o.__dict__


class TestResult:
    """A single test result in the Evergreen report.json format"""

    def __init__(self, name, status):
        """Init test result."""
        # This matches the report.json schema
        # See https://github.com/evergreen-ci/evergreen/blob/789bee107d3ffb9f0f82ae344d72502945bdc914/model/task/task.go#L264-L284
        assert status in ["pass", "fail"]

        self.test_file = name
        self.status = status
        self.exit_code = 1

        if status == "pass":
            self.exit_code = 0



class TestResults:
    """Evergreen TestResult format for report.json."""
    def __init__(self):
        self.results = []

    def add_result(self, result: TestResult):
        self.results.append(result)

    def write(self, filename: str):

        with open(filename, "w") as wfh:
            wfh.write(json.dumps(self, cls=TestResultEncoder))


class ReportLogger(object, metaclass=ABCMeta):
    """Base Class for all report loggers."""

    @abstractmethod
    def log_report(self, name: str, content: str):
        """Get the command to run a linter."""
        pass


class LocalReportLogger(ReportLogger):
    """Write reports to local directory as a set of files."""

    def __init__(self):
        """Init logger and create directory."""
        if not os.path.exists(LOCAL_REPORTS_DIR):
            os.mkdir(LOCAL_REPORTS_DIR)

    def log_report(self, name: str, content: str):
        """Log report to a local file."""
        file_name = os.path.join(LOCAL_REPORTS_DIR, name + ".log")

        with open(file_name, "w") as wfh:
            wfh.write(content)


class BuildLoggerReportLogger(ReportLogger):
    """Write reports to a build logger server."""

    def __init__(self, build_logger):
        """Init logger."""
        self.build_logger = build_logger

        self.build_id = self.build_logger.new_build_id("bdh")

    def log_report(self, name: str, content: str):
        """Log report to a build logger."""

        content = content.split("\n")

        self.build_logger.post_new_file(self.build_id, name, content)


def _get_default(list1, idx, default):
    if (idx + 1) < len(list1):
        return list1[idx]

    return default


class TableWriter:
    """Generate an ASCII table that summarizes the results of all the reports generated."""

    def __init__(self, headers: [str]):
        """Init writer."""
        self._headers = headers
        self._rows = []

    def add_row(self, row: [str]):
        """Add a row to the table."""
        self._rows.append(row)

    @staticmethod
    def _write_row(col_sizes: [int], row: [str], writer: io.StringIO):
        writer.write("|")
        for idx, row_value in enumerate(row):
            writer.write(row_value)
            writer.write(" " * (col_sizes[idx] - len(row_value)))
            writer.write("|")
        writer.write("\n")

    def print(self, writer: io.StringIO):
        """Print the final table to the string stream."""
        cols = max([len(r) for r in self._rows])

        assert cols == len(self._headers)

        col_sizes = []
        for col in range(0, cols):
            col_sizes.append(
                max([len(_get_default(row, col, []))
                     for row in self._rows] + [len(self._headers[col])]))

        TableWriter._write_row(col_sizes, self._headers, writer)

        TableWriter._write_row(col_sizes, ["-" * c for c in col_sizes], writer)

        for row in self._rows:
            TableWriter._write_row(col_sizes, row, writer)


class ReportManager:
    """Manage logging reports to ReportLogger and generate summary report."""

    def __init__(self, logger: ReportLogger):
        """Init report manager."""
        self.logger = logger
        self.results = TestResults()
        self.results_per_comp = {}

    def write_report(self, comp_name: str, report_name: str, status: str, content: str):
        """
        Write a report about a test to the build logger.

        status is a string of "pass" or "fail"
        """
        comp_name = comp_name.replace(" ", "_").replace("/", "_")

        name = comp_name + "_" + report_name

        LOGGER.info("Writing Report %s - %s", name, status)

        self.results.add_result(TestResult(name, status))

        if comp_name not in self.results_per_comp:
            self.results_per_comp[comp_name] = []

        self.results_per_comp[comp_name].append(status)

        # TODO - evaluate whether to wrap lines if that would look better in BFs
        # The textwrap module strips empty lines by default

        self.logger.log_report(name, content)

    def finish(self, reports_file: Optional[str]):
        """Generate final summary of all reports run."""

        if reports_file:
            self.results.write(reports_file)

        tw = TableWriter(["Component", "Vulnerability", "Upgrade"])

        for comp in self.results_per_comp:
            tw.add_row([comp] + self.results_per_comp[comp])

        stream = io.StringIO()
        tw.print(stream)
        print(stream.getvalue())


class ThirdPartyComponent:
    """MongoDB Third Party compoient from third_party_components.yml."""

    def __init__(self, name, homepage_url, local_path, team_owner):
        """Init class."""
        # Required fields
        self.name = name
        self.homepage_url = homepage_url
        self.local_path = local_path
        self.team_owner = team_owner

        # optional fields
        self.vulnerability_suppression = None
        self.upgrade_suppression = None

def _get_field(name, ymap, field: str):
    if field not in ymap:
        raise ValueError("Missing field %s for component %s" % (field, name))

    return ymap[field]


def _read_third_party_components():
    with open(THIRD_PARTY_COMPONENTS_FILE) as rfh:
        yaml_file = yaml.load(rfh.read())

    third_party = []
    components = yaml_file["components"]
    for comp in components:
        cmap = components[comp]

        tp = ThirdPartyComponent(comp, _get_field(comp, cmap, 'homepage_url'),
                                 _get_field(comp, cmap, 'local_directory_path'),
                                 _get_field(comp, cmap, 'team_owner'))

        tp.vulnerability_suppression = cmap.get("vulnerability_suppression", None)
        tp.vulnerability_suppression = cmap.get("vulnerability_suppression", None)

        third_party.append(tp)

    return third_party

def _generate_report_upgrade(mgr: ReportManager, comp: Component, mcomp: ThirdPartyComponent, fail: bool):
    # TODO
    if not fail:
        mgr.write_report(comp.name, "upgrade_check", "pass", "Blackduck run passed")
        return

    mgr.write_report(comp.name, "upgrade_check", "fail", "Test Report TODO")

def _generate_report_vulnerability(mgr: ReportManager, comp: Component, mcomp: ThirdPartyComponent,
                                   fail: bool):
    if not fail:
        mgr.write_report(comp.name, "vulnerability_check", "pass", "Blackduck run passed")
        return

    mgr.write_report(
        comp.name, "vulnerability_check", "fail", f"""A Black Duck scan was run and failed.

The ${comp.name} library had HIGH and/or CRITICAL security issues. The current version in Black Duck is ${comp.version}.

MongoDB policy requires all third-party software to be updated to a version clean of HIGH and CRITICAL vulnerabilities on the master branch.

Next Steps:

Build Baron:
A BF ticket should be generated and assigned to ${mcomp.team_owner} with this text.

Developer:
To address this build failure, the next steps are as follows:
1. File a SERVER ticket to update the software if one already does not exist.
2. Add a “vulnerability_supression” to etc/third_party_components.yml with the SERVER ticket

If you believe the library is already up-to-date but Black Duck has the wrong version, you will need to update the Black Duck configuration.

Note that you do not need to immediately update the library. For more information, https://wiki.corp.mongodb.com/Black Duck.
""")


class Analyzer:
    """
    Analyze the MongoDB source code for software maintence issues.

    Queries Black Duck for out of date software
    Consults a local yaml file for detailed information about third party components included in the MongoDB source code.
    """

    def __init__(self):
        """Init analyzer."""
        self.third_party_components = None
        self.third_party_directories = None
        self.black_duck_components = None
        self.mgr = None

    def _do_reports(self):
        for comp in self.black_duck_components:
            # 1. Validate there are no security issues
            self._verify_vulnerability_status(comp)

            # 2. Check for upgrade issues
            self._verify_upgrade_status(comp)

    def _verify_upgrade_status(self, comp: Component):
        mcomp = self._get_mongo_component(comp)

        if comp.newer_releases:
            _generate_report_upgrade(self.mgr, comp, mcomp,  True)
        else:
            _generate_report_upgrade(self.mgr, comp, mcomp,  False)

    def _verify_vulnerability_status(self, comp: Component):
        mcomp = self._get_mongo_component(comp)

        if comp.security_risk in ["HIGH", "CRITICAL"]:
            _generate_report_vulnerability(self.mgr, comp, mcomp, True)
        else:
            _generate_report_vulnerability(self.mgr, comp, mcomp, False)

    def _get_mongo_component(self, comp: Component):
        mcomp = next((x for x in self.third_party_components if x.name == comp.name), None)

        if not mcomp:
            raise ValueError(
                "Cannot find third party component for Black Duck Component '%s'. Please update '%s'. "
                % (comp.name, THIRD_PARTY_COMPONENTS_FILE))

        return mcomp

    def run(self, logger: ReportLogger, report_file: Optional[str]):
        """Run analysis of Black Duck scan and local files."""

        self.third_party_components = _read_third_party_components()

        self.black_duck_components = _query_blackduck()

        # Black Duck detects ourself everytime we release a new version
        # Rather then constantly have to supress this in Black Duck itself which will generate false positives
        # We filter ourself our of the list of components.
        self.black_duck_components = [
            comp for comp in self.black_duck_components if not comp.name == "MongoDB"
        ]

        self.mgr = ReportManager(logger)

        self._do_reports()

        self.mgr.finish(report_file)


def _get_build_logger_from_file(filename, build_logger_url, task_id):
    tmp_globals = {}
    config = {}

    # The build logger config file is actually python
    # It is a mix of qupted strings and ints
    exec(compile(open(filename, "rb").read(), filename, 'exec'), tmp_globals, config)

    # Rename "slavename" to "username" if present.
    if "slavename" in config and "username" not in config:
        config["username"] = config["slavename"]
        del config["slavename"]

    # Rename "passwd" to "password" if present.
    if "passwd" in config and "password" not in config:
        config["password"] = config["passwd"]
        del config["passwd"]

    return BuildloggerServer(config["username"], config["password"], task_id, config["builder"],
                             config["build_num"], config["build_phase"], build_logger_url)


def _generate_reports_args(args):
    LOGGER.info("Generating Reports")

    logger = LocalReportLogger()

    if args.build_logger_local:
        build_logger = BuildloggerServer("fake_user", "fake_pass", "fake_task", "fake_builder", 1,
                                         "fake_build_phase", "http://localhost:8080")
        logger = BuildLoggerReportLogger(build_logger)
    elif args.build_logger:
        if not args.build_logger_task_id:
            raise ValueError("Most set build_logger_task_id if using build logger")

        build_logger = _get_build_logger_from_file(args.build_logger, args.build_logger_url,
                                                   args.build_logger_task_id)
        logger = BuildLoggerReportLogger(build_logger)

    analyzer = Analyzer()
    analyzer.run(logger, args.report_file)


def _scan_and_report_args(args):
    LOGGER.info("Running Black Duck Scan And Generating Reports")

    _run_scan()

    _generate_reports_args(args)


def main() -> None:
    """Execute Main entry point."""

    parser = argparse.ArgumentParser(description='Black Duck hub controller.')

    parser.add_argument('-v', "--verbose", action='store_true', help="Enable verbose logging")
    parser.add_argument('-d', "--debug", action='store_true', help="Enable debug logging")

    sub = parser.add_subparsers(title="Hub subcommands", help="sub-command help")
    generate_reports_cmd = sub.add_parser('generate_reports',
                                          help='Generate reports from Black Duck')

    generate_reports_cmd.add_argument("--report_file", type=str,
                                      help="report json file to write to")
    generate_reports_cmd.add_argument("--build_logger", type=str,
                                      help="Log to build logger with credentials")
    generate_reports_cmd.add_argument("--build_logger_url", type=str,
                                      default=BUILD_LOGGER_DEFAULT_URL,
                                      help="build logger url to log to")
    generate_reports_cmd.add_argument("--build_logger_task_id", type=str,
                                      help="build logger task id")
    generate_reports_cmd.add_argument("--build_logger_local", action='store_true',
                                      help="Log to local build logger")
    generate_reports_cmd.set_defaults(func=_generate_reports_args)

    scan_cmd = sub.add_parser('scan', help='Do Black Duck Scan')
    scan_cmd.set_defaults(func=_scan_cmd_args)

    scan_and_report_cmd = sub.add_parser('scan_and_report',
                                         help='Run scan and then generate reports')
    scan_and_report_cmd.add_argument("--report_file", type=str, help="report json file to write to")

    scan_and_report_cmd.add_argument("--build_logger", type=str,
                                     help="Log to build logger with credentials")
    scan_and_report_cmd.add_argument("--build_logger_url", type=str,
                                     default=BUILD_LOGGER_DEFAULT_URL,
                                     help="build logger url to log to")
    scan_and_report_cmd.add_argument("--build_logger_task_id", type=str,
                                     help="build logger task id")
    scan_and_report_cmd.add_argument("--build_logger_local", action='store_true',
                                     help="Log to local build logger")
    scan_and_report_cmd.set_defaults(func=_scan_and_report_args)

    args = parser.parse_args()

    test_version_info()

    if args.debug:
        logging.basicConfig(level=logging.DEBUG)
    elif args.verbose:
        logging.basicConfig(level=logging.INFO)

    args.func(args)


if __name__ == "__main__":
    main()
