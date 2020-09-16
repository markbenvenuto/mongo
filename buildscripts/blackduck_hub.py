#!/usr/bin/env python3

import argparse
import json
import logging
import sys
from blackduck.HubRestApi import HubInstance
#import blackduck

PROJECT="mongodb/mongo"
VERSION="master"

#https://github.com/blackducksoftware/hub-rest-api-python/blob/master/examples/get_bom_component_policy_violations.py
# logging.basicConfig(format='%(asctime)s%(levelname)s:%(message)s', stream=sys.stderr, level=logging.DEBUG)
# logging.getLogger("requests").setLevel(logging.WARNING)
# logging.getLogger("urllib3").setLevel(logging.WARNING)



#print(json.dumps(bom_components))

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
    for c in components:

        print("%s - %s - %s - %s - %s" % (c.name, c.version, c.licenses, c.policy_status, c.security_risk))

query_blackduck()
