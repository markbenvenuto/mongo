#!/usr/bin/env python3

import subprocess
import argparse
import os
import json
import logging
import sys
import io
import datetime
import tempfile
import logging
from json import JSONEncoder
import time
import requests
import warnings
import urllib3.util.retry as urllib3_retry
import yaml
from abc import ABCMeta, abstractmethod
from typing import Dict, List, Optional


# TODO
# 1. Check upgrade-guidance
#     - https://mongodb.app.blackduck.com/api-doc/public.html#_reading_a_component_version_s_upgrade_guidance
#     - https://mongodb.app.blackduck.com/api/components/48190a0a-604d-41eb-aadc-9c942ecdb6fd/versions/ac208ec9-78fe-4723-be56-859e90fe9d94/origins/d365371b-e19b-4769-87d3-beae0c7b21e9/upgrade-guidance
# 2. Add link back to blackduck so people can update the version in the manual
# 3. Add table formatting
# 4. Write sumnary o CSV
# 5. Write third_party.md file
#

from blackduck.HubRestApi import HubInstance
#import blackduck

LOGGER = logging.getLogger(__name__)

############################################################################

PROJECT = "mongodb/mongo"
VERSION = "master"
BLACKDUCK_TIMEOUT_SECS=600

PROJECT_URL="https://mongodb.app.blackduck.com/api/projects/0258c84e-bb6c-4e37-b104-49148547b027/versions/2b272c5d-6c5e-401d-95c4-6449c06377c4/components?sort=projectName%20ASC&offset=0&limit=100&filter=bomInclusion%3Atrue&filter=bomInclusion%3Afalse"

THIRD_PARTY_DIRECTORIES = [
    'src/third_party/wiredtiger/test/3rdparty',
    'src/third_party',
]


THIRD_PART_COMPONENTS_FILE = "etc/third_party_components.yml";


############################################################################

# Build Logger constants

CREATE_BUILD_ENDPOINT = "/build"
APPEND_GLOBAL_LOGS_ENDPOINT = "/build/%(build_id)s"
CREATE_TEST_ENDPOINT = "/build/%(build_id)s/test"
APPEND_TEST_LOGS_ENDPOINT = "/build/%(build_id)s/test/%(test_id)s"

_TIMEOUT_SECS = 65

############################################################################

#https://github.com/blackducksoftware/hub-rest-api-python/blob/master/examples/get_bom_component_policy_violations.py
# logging.basicConfig(format='%(asctime)s%(levelname)s:%(message)s', stream=sys.stderr, level=logging.DEBUG)
# logging.getLogger("requests").setLevel(logging.WARNING)
# logging.getLogger("urllib3").setLevel(logging.WARNING)

# Use cases
#
# 1. A developer adds a new third party library
#    Black Duck will detect the new component
#    A BF will be generated since it is unknown library
# 2. A new vulnerability is discovered in a third party library
#    Black Duck will update their database
#    We will query their database and a BF will be generated
# 3. A third party library releases a new version
#    Black Duck will update their database
#    We will query their database and a BF will be generated
# 4. A new third party library is added to src/third_party but not detected by Black Duck
#    We will error on any files in src/third_party that are not in etc/third_party_components.yml
# 5. A known third party library is removed but not from etc/third_party_components.yml or Black Duck
# 6. Black Duck detects a new library that is not actually in product
#    TODO


#print(json.dumps(bom_components))

try:
    import requests.packages.urllib3.exceptions as urllib3_exceptions
except ImportError:
    # Versions of the requests package prior to 1.2.0 did not vendor the urllib3 package.
    urllib3_exceptions = None

def default_if_none(value, default):
    """Set default if value is 'None'."""
    return value if value is not None else default



# TODO - load credentials

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
    def __init__(self, name, version, licenses, policy_status, security_risk, newer_releases, policies):
        self.name = name
        self.version = version
        self.licenses = licenses
        self.policy_status = policy_status
        self.security_risk = security_risk
        self.policies = policies
        self.newer_releases = newer_releases

    def parse(hub, component):
        name = component["componentName"]
        cversion = component.get("componentVersionName", "unknown_version")
        licenses = ",".join([a.get("spdxId", a["licenseDisplay"]) for a in component["licenses"]])

        policy_status =  component["policyStatus"]
        securityRisk = _compute_security_risk(component['securityRiskProfile'])

        policies = []
        if policy_status == "IN_VIOLATION":
            response = hub.execute_get(hub.get_link(component, "policy-rules"))
            policies_dict = response.json()

            policies = [Policy.parse(p) for p in policies_dict["items"]]

        newer_releases = component["activityData"].get("newerReleases", None)

        # TODO - handle newer releases with some data cleaning
        #if newer_releases > 0:


        return Component(name, cversion, licenses, policy_status, securityRisk, newer_releases, policies)


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
RESTCONFIG = ".restconfig.json"

class BlackDuckConfig:
    def __init__(self):
        if not os.path.exists(RESTCONFIG):
            raise ValueError("Cannot find %s for blackduck configuration" % (RESTCONFIG))

        with open(RESTCONFIG, "r") as rfh:
            rc = json.loads(rfh.read())

        self.url = rc["baseurl"]
        self.username = rc["username"]
        self.password = rc["password"]


def run_scan():
    # Get user name and password from .restconfig.json
    bdc = BlackDuckConfig()

#    os.system(f"bash <(curl --retry 5 -s -L https://detect.synopsys.com/detect.sh) --blackduck.url={bdc.url} --blackduck.username={bdc.username} --blackduck.password={bdc.password} --detect.report.timeout=600 --detect.wait.for.results=true")

    # TODO - set JAVA_HOME on machines?
    with tempfile.NamedTemporaryFile() as fp:
        fp.write(f"""#/!bin/sh
curl --retry 5 -s -L https://detect.synopsys.com/detect.sh  | bash -s -- --blackduck.url={bdc.url} --blackduck.username={bdc.username} --blackduck.password={bdc.password} --detect.report.timeout={BLACKDUCK_TIMEOUT_SECS} --detect.wait.for.results=true
""".encode())
        fp.flush()

        # subprocess.call(["ls", "-l", fp.name])
        # subprocess.call(["cat", fp.name])

        subprocess.call(["/bin/sh", fp.name])

#    subprocess.call(["/bin/sh", "-c", f"bash <(curl --retry 5 -s -L https://detect.synopsys.com/detect.sh) --blackduck.username={bdc.username} --blackduck.password={bdc.password} --detect.report.timeout={BLACKDUCK_TIMEOUT_SECS} --detect.wait.for.results=true"])

def _scan_cmd_args(args):
    LOGGER.info("Running BlackDuck Scan")

    run_scan()
    
    pass

#run_scan()

def query_blackduck():

    hub = HubInstance()

    LOGGER.info("Fetching project %s from blackduck", PROJECT)
    project = hub.get_project_by_name(PROJECT)

    LOGGER.info("Fetching project version %s from blackduck", VERSION)
    version = hub.get_version_by_name(project, VERSION)

    LOGGER.info("Getting version components from blackduck")
    bom_components = hub.get_version_components(version)

    project = hub.get_project_by_name(PROJECT)
    version = hub.get_version_by_name(project, VERSION)
    bom_components = hub.get_version_components(version)

    components = [Component.parse(hub, c) for c in bom_components["items"]]

    return components

class TestResultEncoder(JSONEncoder):
    def default(self, o):
        return o.__dict__

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

class TableWriter:
    def __init__(self, headers: [str]):
        self._headers = headers
        self._rows = []

    def add_row(self, row : [str]):
        self._rows.append(row)  


    def _write_row(self, col_size : [int], row : [str], writer : io.StringIO):
        for idx in range(len(row)):
            writer.write(row[idx])
            writer.write(" " * (col_sizes[idx] - len(row[idx])))
            writer.write("|")

    def print(self, writer : io.StringIO):

        cols = max([len(r) for r in self._rows])

        col_sizes = []
        for c in range(0, cols):
            col_sizes = len(self._rows.get(c, ""))


        self._write_row(col_sizes, self._headers, writer)

        for r in self._rows:
            _write_row(self, col_size, r, writer)


class ReportManager:

    def __init__(self, logger: ReportLogger):
        self.logger = logger
        self.results = []
        self.results_per_comp = {}

    def write_report(self, comp_name :str, report_name : str, status : str, content : str):
        """
            status is a string of "pass" or "fail"
        """
        comp_name = comp_name.replace(" ", "_").replace("/", "_")

        # TODO - write out to logger

        name = comp_name + "_" + report_name

        LOGGER.info("Writing Report %s - %s", name, status)

        self.results.append( TestResult(name, status))

        #TODO - self.results_per_comp[comp_name][name] = status

        self.logger.log_report(name, content)



    def finish(self, reports_file : str):

        with open( reports_file, "w") as wfh:
            wfh.write(json.dumps(self.results, cls = TestResultEncoder))

        # self.logger.finish()


def get_third_party_directories():
    third_party = []
    for tp in THIRD_PARTY_DIRECTORIES:
        for entry in os.scandir(tp):
            if entry.name not in ["scripts"] and entry.is_dir():
                third_party.append(entry.path)

    return sorted(third_party)

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
    with open(THIRD_PART_COMPONENTS_FILE) as rfh:
        yaml_file = yaml.load(rfh.read())

    print(yaml_file)

    third_party = []
    components = yaml_file["components"]
    for comp in components:
        cmap = components[comp]

        tp = ThirdPartyComponent(comp, _get_field(comp, cmap, 'homepage_url'),  _get_field(comp, cmap, 'local_directory_path'),  _get_field(comp, cmap, 'team_owner'))

        tp.upgrade_suppression = cmap.get("upgrade_suppression", None)
        tp.vulnerability_suppression = cmap.get("vulnerability_suppression", None)

        third_party.append(tp)

    return third_party



def write_policy_report(c):

    for p in c.policies:
        pass

def _generate_report_missing_component(mgr : ReportManager, comp : Component):
    # TODO
    #mgr.write_report(f"{comp.name}_yaml_check", "fail", "Test Report TODO")
    pass

def _generate_report_missing_directory(mgr : ReportManager, cdir: str):
    # TODO
    #mgr.write_report(f"{cdir}_missing_comp_check", "fail", "Test Report TODO")
    pass

def _generate_report_upgrade(mgr : ReportManager,comp : Component):
    # TODO
    mgr.write_report(comp.name, "upgrade_check", "fail", "Test Report TODO")

def _generate_report_vulnerability(mgr : ReportManager,comp : Component):
    # TODO
    mgr.write_report(comp.name, "vulnerability_check", "fail", "Test Report TODO")

class Analyzer:

    def __init__(self):
        self.third_party_components  = None
        self.third_party_directories  = None
        self.black_duck_components  = None
        self.mgr  = None

    def _do_reports(self):
        print("Component List")
        for c in self.black_duck_components:
            print("%s - %s - %s - %s - %s" % (c.name, c.version, c.licenses, c.newer_releases, c.security_risk))

        for c in self.black_duck_components:
            # 1. Validate if this is in the YAML file
            self._verify_yaml_match(c)

            # 2. Validate there are no security issues
            self._verify_vulnerability_status(c)

            # 3. Check for upgrade issues
            self._verify_upgrade_status(c)

        # 4. Validate that each third_party directory is in the YAML file
        self._verify_directories_in_yaml()

    def _verify_yaml_match(self, comp: Component):

        if comp.name not in [c.name for c in self.third_party_components]:
            _generate_report_missing_component(self.mgr, comp)

        # TODO - generate pass report

    def _verify_vulnerability_status(self, comp: Component):
        if comp.security_risk in ["HIGH", "CRITICAL"]:
            _generate_report_vulnerability(self.mgr, comp)


    def _verify_upgrade_status(self, comp: Component):
        if comp.policies:
            _generate_report_upgrade(self.mgr, comp)

    def _verify_directories_in_yaml(self):


        comp_dirs = [c.local_path for c in self.third_party_components]
        for cdir in self.third_party_directories:
            if cdir in ["src/third_party/wiredtiger"]:
                continue

            if cdir not in comp_dirs:
                _generate_report_missing_directory(self.mgr, cdir)


    #do_report()


    def run(self):

        self.third_party_components = read_third_party_components()
        # self.third_party_components_directories = [c.local_path for c in self.third_party_components]
        # print(self.third_party_components_directories)

        self.third_party_directories = get_third_party_directories()
        self.third_party_directories_short = [os.path.basename(f) for f in self.third_party_directories]

        print(self.third_party_directories)
        print(self.third_party_directories_short)

        self.black_duck_components = query_blackduck()

        self.mgr = ReportManager(LocalReportLogger())

        self._do_reports()

        # TODO - take a file name
        self.mgr.finish("reports.json")


def _generate_reports_args(args):
    LOGGER.info("Generating Reports")

    analyzer = Analyzer()
    analyzer.run()

    pass

def _scan_and_report_args(args):
    LOGGER.info("Running BlackDuck Scan And Generating Reports")
    pass


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

def main() -> None:
    """Execute Main entry point."""

    parser = argparse.ArgumentParser(description='Black Duck hub controller.')

    parser.add_argument('-v', "--verbose", action='store_true', help="Enable verbose logging")
    parser.add_argument('-d', "--debug", action='store_true', help="Enable debug logging")

    sub = parser.add_subparsers(title="Hub subcommands", help="sub-command help")

    generate_reports_cmd = sub.add_parser('generate_reports', help='Generate reports from Black Duck')
    generate_reports_cmd.set_defaults(func=_generate_reports_args)

    scan_cmd = sub.add_parser('scan', help='Do Black Duck Scan')
    scan_cmd.set_defaults(func=_scan_cmd_args)

    scan_and_report_cmd = sub.add_parser('scan_and_report', help='Run scan and then generate reports')
    scan_and_report_cmd.set_defaults(func=_scan_and_report_args)

    args = parser.parse_args()

    if args.debug:
        logging.basicConfig(level=logging.DEBUG)
    elif args.verbose:
        logging.basicConfig(level=logging.INFO)


    args.func(args)


if __name__ == "__main__":
    main()
