#!/usr/bin/env python3

import argparse
import os
import json
import logging
import sys
import datetime
from json import JSONEncoder
import yaml

from blackduck.HubRestApi import HubInstance
#import blackduck

PROJECT = "mongodb/mongo"
VERSION = "master"

THIRD_PARTY_DIRECTORIES = [
    'src/third_party/wiredtiger/test/3rdparty',
    'src/third_party',
]


#https://github.com/blackducksoftware/hub-rest-api-python/blob/master/examples/get_bom_component_policy_violations.py
# logging.basicConfig(format='%(asctime)s%(levelname)s:%(message)s', stream=sys.stderr, level=logging.DEBUG)
# logging.getLogger("requests").setLevel(logging.WARNING)
# logging.getLogger("urllib3").setLevel(logging.WARNING)



#print(json.dumps(bom_components))

import time
import requests
import warnings
import urllib3.util.retry as urllib3_retry

try:
    import requests.packages.urllib3.exceptions as urllib3_exceptions
except ImportError:
    # Versions of the requests package prior to 1.2.0 did not vendor the urllib3 package.
    urllib3_exceptions = None

def default_if_none(value, default):
    """Set default if value is 'None'."""
    return value if value is not None else default



# TODO - load credentials
CREATE_BUILD_ENDPOINT = "/build"
APPEND_GLOBAL_LOGS_ENDPOINT = "/build/%(build_id)s"
CREATE_TEST_ENDPOINT = "/build/%(build_id)s/test"
APPEND_TEST_LOGS_ENDPOINT = "/build/%(build_id)s/test/%(test_id)s"

_TIMEOUT_SECS = 65

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

    def post(self, endpoint, data=None, headers=None, timeout_secs=_TIMEOUT_SECS):
        """Send a POST request to the specified endpoint with the supplied data.

        Return the response, either as a string or a JSON object based
        on the content type.
        """

        data = default_if_none(data, [])
        data = json.dumps(data)

        headers = default_if_none(headers, {})
        headers["Content-Type"] = "application/json; charset=utf-8"

        url = self._make_url(endpoint)

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


class BuildloggerServer(object):
    """A remote server to which build logs can be sent.

    It is used to retrieve handlers that can then be added to logger
    instances to send the log to the servers.
    """

    def __init__(self, username, password, task_id, builder, build_num, url):
        """Initialize BuildloggerServer."""
        self.username = username
        self.password = password
        self.builder = builder
        self.build_num = build_num
        self.url = url
        self.task_id = task_id

        self.handler = HTTPHandler(url_root=self.url,
                                       username=self.username,
                                       password=self.password, should_retry=True)


    def new_build_id(self, suffix):
        """Return a new build id for sending global logs to."""
        builder = "%s_%s" % (self.builder, suffix)
        build_num = int(self.build_num)

        response = self.handler.post(
            CREATE_BUILD_ENDPOINT, data={
                "builder": builder,
                "buildnum": build_num,
                "task_id": self.task_id,
            })

        return response["id"]

    def new_test_id(self, build_id, test_filename, test_command):
        """Return a new test id for sending test logs to."""
        endpoint = CREATE_TEST_ENDPOINT % {"build_id": build_id}

        response = self.handler.post(
            endpoint, data={
                "test_filename": test_filename,
                "command": test_command,
                "phase": "unknown",
                "task_id": self.task_id,
            })

        return response["id"]

    # def get_global_handler(self, build_id, handler_info):
    #     """Return the global handler."""
    #     return BuildloggerGlobalHandler(self.config, build_id, **handler_info)

    # def get_test_handler(self, build_id, test_id, handler_info):
    #     """Return the test handler."""
    #     return BuildloggerTestHandler(self.config, build_id, test_id, **handler_info)


    def post_new_file(self, build_id, test_name, lines):
        test_id = self.new_test_id(build_id, test_name, "foo")
        endpoint = APPEND_TEST_LOGS_ENDPOINT % {
            "build_id": build_id,
            "test_id": test_id,
        }

        dt = datetime.datetime.now().isoformat()

        # dt = time.strftime("%Y-%m-%dT%H:%M:%S",  datetime.datetime.now())
        dlines = [(dt, line) for line in lines]

        try:
            print("POSTING to %s" % (endpoint))
            self.handler.post(endpoint, data=dlines)
        except requests.HTTPError as err:
            # Handle the "Request Entity Too Large" error, set the max size and retry.
            raise ValueError("Encountered an HTTP error: %s", err)
        except requests.RequestException as err:
            raise ValueError("Encountered a network error: %s", err)
        except:  # pylint: disable=bare-except
            raise ValueError("Encountered an error.")


    # @staticmethod
    # def get_build_log_url(build_id):
    #     """Return the build log URL."""
    #     base_url = _config.BUILDLOGGER_URL.rstrip("/")
    #     endpoint = APPEND_GLOBAL_LOGS_ENDPOINT % {"build_id": build_id}
    #     return "%s/%s" % (base_url, endpoint.strip("/"))

    # @staticmethod
    # def get_test_log_url(build_id, test_id):
    #     """Return the test log URL."""
    #     base_url = _config.BUILDLOGGER_URL.rstrip("/")
    #     endpoint = APPEND_TEST_LOGS_ENDPOINT % {"build_id": build_id, "test_id": test_id}
    #     return "%s/%s" % (base_url, endpoint.strip("/"))




def _to_dict(items, func):
    dm = {}
    for i in items:
        tuple1 = func(i)
        dm[tuple1[0]] = tuple1[1]
    return dm


#  'securityRiskProfile': {'counts': [{'countType': 'UNKNOWN', 'count': 0},
#    {'countType': 'OK', 'count': 5},
#    {'countType': 'LOW', 'count': 0},
#    {'countType': 'MEDIUM', 'count': 0},
#    {'countType': 'HIGH', 'count': 0},
#    {'countType': 'CRITICAL', 'count': 0}]},
def _compute_security_risk(securityRiskProfile):
    counts = securityRiskProfile["counts"]
    
    cm = _to_dict(counts, lambda i : (i["countType"], int(i["count"])) )


    priorities = ['CRITICAL', 'HIGH', 'MEDIUM', 'LOW', 'OK', 'UNKNOWN']

    for p in priorities:
        if cm[p] > 0:
            return p

    return "OK"

class Component:
    def __init__(self, name, version, licenses, policy_status, security_risk, policies):
        self.name = name
        self.version = version
        self.licenses = licenses
        self.policy_status = policy_status
        self.security_risk = security_risk
        self.policies = policies

    def parse(hub, component):
        name = component["componentName"]
        cversion = component["componentVersionName"]
        licenses = ",".join([a.get("spdxId", a["licenseDisplay"])        for a in component["licenses"]])

        # TODO - security risk
    #  'securityRiskProfile': {'counts': [{'countType': 'UNKNOWN', 'count': 0},
    #    {'countType': 'OK', 'count': 5},
    #    {'countType': 'LOW', 'count': 0},
    #    {'countType': 'MEDIUM', 'count': 0},
    #    {'countType': 'HIGH', 'count': 0},
    #    {'countType': 'CRITICAL', 'count': 0}]},


        policy_status =  component["policyStatus"]
        securityRisk = _compute_security_risk(component['securityRiskProfile'])


        policies = []
        if policy_status == "IN_VIOLATION":
            response = hub.execute_get(hub.get_link(component, "policy-rules"))
            policies_dict = response.json()

            policies = [Policy.parse(p) for p in policies_dict["items"]]
        
        return Component(name, cversion, licenses, policy_status, securityRisk, policies)


class Policy:
    def __init__(self, name, severity, status):
        self.name = name
        self.severity = severity
        self.status = status
    
    def parse(policy):
        return Policy(policy["name"], policy["severity"], policy["policyApprovalStatus"])


# from Cheetah.Template import Template
# templateDef = """
# <HTML>
# <HEAD><TITLE>$title</TITLE></HEAD>
# <BODY>
# $contents
# ## this is a single-line Cheetah comment and won't appear in the output
# #* This is a multi-line comment and won't appear in the output
#    blah, blah, blah
# *#
# </BODY>
# </HTML>"""
# nameSpace = {'title': 'Hello World Example', 'contents': 'Hello World!'}

def query_blackduck():

    hub = HubInstance()

    project = hub.get_project_by_name(PROJECT)
    version = hub.get_version_by_name(project, VERSION)

    bom_components = hub.get_version_components(version)

    components = [Component.parse(hub, c) for c in bom_components["items"]]

    return components

class TestResultEncoder(JSONEncoder):
        def default(self, obj):
            return obj.__dict__

class TestResult:
    def __init__(self, name, status):
        # This matches the report.json schema
        # See https://github.com/evergreen-ci/evergreen/blob/789bee107d3ffb9f0f82ae344d72502945bdc914/model/task/task.go#L264-L284
        # TODO log file
        assert status in ["pass", "fail"]

        self.test_file = name
        self.status = status
        self.exit_code = 1

        if status == "pass":
            self.exit_code = 0

from abc import ABCMeta, abstractmethod
from typing import Dict, List, Optional


class ReportLogger(object, metaclass=ABCMeta):
    """Base Class for all report loggers."""

    @abstractmethod
    def log_report(self, name : str, content : str):
        """Get the command to run a linter."""
        pass


REPORTS_DIR = "bd_reports"

class LocalReportLogger(ReportLogger):

    def __init__(self):
        if not os.path.exists(REPORTS_DIR):
            os.mkdir(REPORTS_DIR)

    def log_report(self, name : str, content: str) :
        file_name = os.path.join(REPORTS_DIR, name + ".log")

        with open( file_name, "w") as wfh:
            wfh.write(content)

        

class ReportManager:

    def __init__(self, logger: ReportLogger):
        self.logger = logger
        self.results = []

    def write_report(self, name :str, status : str, content : str):
        """
            status is a string of "pass" or "fail"
        """
        name = name.replace(" ", "_")

        # TODO - write out to logger

        self.results.append( TestResult(name, status))

        self.logger.log_report(name, content)

    
    def finish(self, reports_file : str):

        with open( reports_file, "w") as wfh:
            wfh.write(json.dumps(self.results, cls = TestResultEncoder))

        # self.logger.finish()


def write_policy_report(c):

    for p in c.policies:
        pass


def write_security_report(c):
    pass

def do_report():
    components = query_blackduck()

    print("Component List")
    for c in components:
        print("%s - %s - %s - %s - %s" % (c.name, c.version, c.licenses, c.policy_status, c.security_risk))

    for c in components:
        write_security_report(c)

        write_policy_report(c)

#do_report()

def get_third_party_directories():
    third_party = []
    for tp in THIRD_PARTY_DIRECTORIES:
        for entry in os.scandir(tp):
            if entry.name not in ["scripts"] and entry.is_dir():
                third_party.append(entry.path)

    print(third_party)

    return sorted(third_party)

get_third_party_directories()

class ThirdPartyComponent:

    def __init__(self, name, homepage_url, local_path, team_owner):
        # Required fields
        self.name = name
        self.homepage_url = homepage_url
        self.local_path = local_path
        self.team_owner = team_owner

        # optional fields
        self.upgrade_suppression = None
        self.vulnerability_suppression = None


def _get_field(name, ymap, field:str) :
    if field not in ymap:
        raise ValueError("Missing field %s for component %s" % (field, name))

    return ymap[field]

def read_third_party_components():
    
    FILE = "etc/third_party_components.yml";
    with open(FILE) as rfh:
        yaml_file = yaml.load(rfh.read())

    print(yaml_file)

    third_party = []
    components = yaml_file["components"]
    for comp in components:
        cmap = components[comp]

        tp = ThirdPartyComponent(comp, _get_field(comp, cmap, 'homepage_url'),  _get_field(comp, cmap, 'local_directory_path'),  _get_field(comp, cmap, 'team_owner'))
        third_party.append(tp)

    return third_party

read_third_party_components()

# def do_report2():
#     mgr = ReportManager(LocalReportLogger())

#     mgr.write_report("bd1_pass", "pass", "Test Report 1")
#     mgr.write_report("bd2_fail", "pass", "Test Report 2")


#     mgr.finish("reports.json")

# do_report2()


# component_name:
# 	homepage_url:
# 	local_directory_path:
# 	upgrade_suppression: SERVER-12345
# 	vulnerability_suppression: SERVER-12345
# 	team_owner:


# server = BuildloggerServer("fake", "oass", "123", "a_builder" , "456", "http://localhost:8080")

# build_id = server.new_build_id("789")

# test_file = """
# Here is a sample test file
# You can find more details here at ...
# asd
# as
# da
# sd
# asd
# asda;klsfjhas'fhk'
# """

# server.post_new_file(build_id, "sample_test", test_file.split("\n"))

# server.post_new_file(build_id, "sample_test2", test_file.split("\n"))
