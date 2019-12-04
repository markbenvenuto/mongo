#!/usr/bin/env python3
"""
Script for testing mongodb in containers.

Requires ssh, scp, and sh on local and remote hosts.
Assumes remote host is Linux
"""

import argparse
import logging
import os
import re
import sys
import json
import threading
import subprocess
import pprint
from typing import List

import boto3

LOGGER = logging.getLogger(__name__)

# Steps
# 1 - Build container with sshd
# 2 - Upload container to ECR
# 3 - ECS Provider
#   1 - Automate setup of ECS Task
#   2 - Get SSH Interface
#   3 - Automate teardown of ECS task
# 4 - Podman/Docker Provider
#   1 - Launch container
#   2 - Get SSH Interface
#   3 - Automate teardown of Podman

############################################################################
# Local configuration settings for testing against the local docker/podman
#
DOCKER_EXECUTABLE = "podman"

IMAGE_NAME = "fedora_sshd"

CONTAINER_DEFAULT_PORT = 4000

CONTAINER_DEFAULT_NAME = "container_tester_test1"
############################################################################


############################################################################
# Configuration settings for working with a ECS cluster in a region
#

# These settings depend on a cluster, task subnet, and security group already setup
ECS_DEFAULT_CLUSTER = "arn:aws:ecs:us-east-2:579766882180:cluster/mcb-sample2"
ECS_DEFAULT_TASK_DEFINITION = "arn:aws:ecs:us-east-2:579766882180:task-definition/sshd:1"
ECS_DEFAULT_SUBNET = 'subnet-a5e114cc'
# Must allow ssh from 0.0.0.0
ECS_DEFAULT_SECURITY_GROUP = 'sg-051a91d96332f8f3a'

# This is just a string local to this file
DEFAULT_SERVICE_NAME = 'script-test2'

############################################################################


class GitCommandResult(object):
    """The result of running git subcommand.

    Args:
        cmd: the git subcommand that was executed (e.g. 'clone', 'diff').
        process_args: the full list of process arguments, starting with the 'git' command.
        returncode: the return code.
        stdout: the output of the command.
        stderr: the error output of the command.
    """

    def __init__(  # pylint: disable=too-many-arguments
            self, process_args, returncode, stdout=None, stderr=None):
        """Initialize GitCommandResult."""
        self.process_args = process_args
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr

    def check_returncode(self):
        """Raise GitException if the exit code is non-zero."""
        if self.returncode:
            raise GitException(
                "Process failed with code '{0}'".format(" ".join(self.process_args),
                                                              self.returncode), self.returncode,
                self.process_args, self.stdout, self.stderr)


# def _run_cmd(self, cmd, args):
#     """Run the git command and return a GitCommandResult instance."""

#     params = ["git", cmd] + args
#     return _run_process(cmd, params)

def _run_process(params, cwd=None):
    LOGGER.info("RUNNING COMMAND: %s", params)
    process = subprocess.Popen(params, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd)
    (stdout, stderr) = process.communicate()
    if process.returncode:
        if stdout:
            LOGGER.error("Output of '%s': %s", " ".join(params), stdout)
        if stderr:
            LOGGER.error("Error output of '%s': %s", " ".join(params), stderr)
    return GitCommandResult(params, process.returncode, stdout=stdout, stderr=stderr)

def _userandhostandport(endpoint):
    user_and_host = endpoint.find("@")
    if user_and_host == -1:
        raise ValueError("Invalid endpoint, Endpoint must be user@host:port")
    (user, host) = (endpoint[:user_and_host], endpoint[user_and_host + 1:])

    colon = host.find(":")
    if colon == -1:
        return (user, host, "22")
    return (user, host[:colon], host[colon + 1:])

def _scp(endpoint, src, dest):
    (user, host, port) = _userandhostandport(endpoint)
    cmd = ["scp", "-o", "StrictHostKeyChecking=no", "-P", port, src, "%s@%s:%s" % (user, host, dest) ]
    _run_process(cmd)

def _ssh(endpoint, cmd):
    (user, host, port) = _userandhostandport(endpoint)
    cmd = ["ssh", "-o", "StrictHostKeyChecking=no", "-p", port, "%s@%s" % (user, host), cmd ]
    ret = _run_process(cmd)
    print(ret.stdout.decode('utf-8'))
    LOGGER.info("RETURN CODE: %s", ret.returncode)
    return ret.returncode

def _run_test_args(args):
    run_test(args.endpoint, args.script, args.files)

def run_test(endpoint, script, files):
    LOGGER.info("Copying files to %s", endpoint)

    for file in files:
        colon = file.find(":")
        (src, dest) = (file[:colon], file[colon + 1:])
        _scp(endpoint, src, dest)

    LOGGER.info("Copying script to %s", endpoint)
    _scp(endpoint, script, "/tmp/test.sh")
    return_code = _ssh(endpoint, "/bin/sh -x /tmp/test.sh")
    if return_code != 0:
        LOGGER.error("FAILED: %s", return_code)
        raise ValueError(f"test failed with {return_code}")

def _local_start_container_args(args):
#    start_container_args(args)
    port_mapping = f"127.0.0.1:{CONTAINER_DEFAULT_PORT}:22"
    ret = _run_process([DOCKER_EXECUTABLE, "run", "-p", port_mapping, "--name", CONTAINER_DEFAULT_NAME, "--rm", "-d", IMAGE_NAME ])
    print(ret.stdout.decode('utf-8'))

def _local_stop_container_args(args):
#    start_container_args(args)
    ret = _run_process([DOCKER_EXECUTABLE, "stop", CONTAINER_DEFAULT_NAME])
    print(ret.stdout.decode('utf-8'))

def _get_region(arn):
    return arn.split(':')[3]


def _remote_ps_container_args(args):
    _remote_ps_container(ECS_DEFAULT_CLUSTER)

def _remote_ps_container(cluster):
    ecs_client = boto3.client('ecs', region_name=_get_region(cluster))
    ec2_client = boto3.client('ec2', region_name=_get_region(cluster))

    tasks = ecs_client.list_tasks(cluster=cluster)

    task_list = ecs_client.describe_tasks(cluster=cluster, tasks=tasks['taskArns'])

    #Example from ecs-cli tool
    #Name                                       State    Ports                    TaskDefinition  Health
    #aa2c2642-3013-4370-885e-8b8d956e753d/sshd  RUNNING  3.15.149.114:22->22/tcp  sshd:1          UNKNOWN

    print("Name                                       State    Ports                    TaskDefinition  Health")
    for task in task_list['tasks']:

        taskDefinition = task['taskDefinitionArn']
        taskDefinition_short = taskDefinition[taskDefinition.rfind('/') + 1:]

        enis = []
        for b in [ a['details'] for a in task["attachments"] if a['type'] == 'ElasticNetworkInterface']:
            for c in b:
                if c['name'] == 'networkInterfaceId':
                    enis.append(c['value'])
        assert enis

        eni = ec2_client.describe_network_interfaces(NetworkInterfaceIds=enis)
        public_ip = [n["Association"]["PublicIp"] for n in eni["NetworkInterfaces"]][0]

        for container in task['containers']:
            taskArn = container['taskArn']
            task_id = taskArn[taskArn.rfind('/')+ 1:]
            name = container['name']
            task_id = task_id + "/" + name
            lastStatus = container['lastStatus']


        # TODO - to get the port bindings, we have to get them from the task defintition
        print("{:<43}{:<9}{:<25}{:<16}".format(task_id, lastStatus, public_ip, taskDefinition_short ))

def _remote_create_container_args(args):
    _remote_create_container(ECS_DEFAULT_CLUSTER, ECS_DEFAULT_TASK_DEFINITION, DEFAULT_SERVICE_NAME, ECS_DEFAULT_SUBNET, ECS_DEFAULT_SECURITY_GROUP)

def _remote_create_container(cluster, task_definition, service_name, subnet, security_group):
    ecs_client = boto3.client('ecs', region_name=_get_region(cluster))

    # TODO - consider clientToken
    # TODO - generate serviceName
    resp = ecs_client.create_service(cluster=cluster, serviceName=service_name,
        taskDefinition = task_definition,
        desiredCount = 1,
        launchType='FARGATE',
        networkConfiguration={
            'awsvpcConfiguration': {
                'subnets': [
                    subnet
                ],
                'securityGroups': [
                    security_group,
                ],
                'assignPublicIp': "ENABLED"
            }
        }
        )

    pprint.pprint(resp)

    service_arn = resp["service"]["serviceArn"]
    print(f"Waiting for Service {service_arn} to become active...")

    waiter = ecs_client.get_waiter('services_stable')

    waiter.wait(cluster=cluster, services=[service_arn])

def _remote_stop_container_args(args):
    _remote_stop_container(ECS_DEFAULT_CLUSTER, DEFAULT_SERVICE_NAME)

def _remote_stop_container(cluster, service_name):
    ecs_client = boto3.client('ecs', region_name=_get_region(cluster))

    resp = ecs_client.delete_service(cluster=cluster, service=service_name, force=True)
    pprint.pprint(resp)

    service_arn = resp["service"]["serviceArn"]

    print(f"Waiting for Service {service_arn} to become inactive...")
    waiter = ecs_client.get_waiter('services_inactive')

    waiter.wait(cluster=cluster, services=[service_arn])

def _remote_get_endpoint_str(cluster, service_name):
    ecs_client = boto3.client('ecs', region_name=_get_region(cluster))
    ec2_client = boto3.client('ec2', region_name=_get_region(cluster))

    tasks = ecs_client.list_tasks(cluster=cluster, serviceName=service_name)

    task_list = ecs_client.describe_tasks(cluster=cluster, tasks=tasks['taskArns'])

    for task in task_list['tasks']:

        enis = []
        for b in [ a['details'] for a in task["attachments"] if a['type'] == 'ElasticNetworkInterface']:
            for c in b:
                if c['name'] == 'networkInterfaceId':
                    enis.append(c['value'])
        assert enis

        eni = ec2_client.describe_network_interfaces(NetworkInterfaceIds=enis)
        public_ip = [n["Association"]["PublicIp"] for n in eni["NetworkInterfaces"]][0]
        break

    return f"root@{public_ip}:22"

def _remote_get_endpoint_args(args):
    _remote_get_endpoint(ECS_DEFAULT_CLUSTER, DEFAULT_SERVICE_NAME)

def _remote_get_endpoint(cluster, service_name):
    endpoint = _remote_get_endpoint_str(cluster, service_name)
    print(endpoint)

def _get_caller_identity(args):
    sts_client = boto3.client('sts')

    pprint.pprint(sts_client.get_caller_identity())


def _run_e2e_test_args(args):
    _run_e2e_test(args.script, args.files)

def _run_e2e_test(script, files):

    _remote_create_container(ECS_DEFAULT_CLUSTER, ECS_DEFAULT_TASK_DEFINITION, DEFAULT_SERVICE_NAME, ECS_DEFAULT_SUBNET, ECS_DEFAULT_SECURITY_GROUP)

    endpoint = _remote_get_endpoint_str(ECS_DEFAULT_CLUSTER, DEFAULT_SERVICE_NAME)

    run_test(endpoint, script, files)

    _remote_stop_container(ECS_DEFAULT_CLUSTER, DEFAULT_SERVICE_NAME)



def main() -> None:
    """Execute Main entry point."""

    parser = argparse.ArgumentParser(description='Quick C++ Lint frontend.')

    parser.add_argument('-v', "--verbose", action='store_true', help="Enable verbose logging")
    parser.add_argument('-d', "--debug", action='store_true', help="Enable debug logging")

    sub = parser.add_subparsers(title="Linter subcommands", help="sub-command help")

    run_test_cmd = sub.add_parser('run_test', help='Run Test')
    run_test_cmd.add_argument("--endpoint", required=True, type=str, help="User and Host and port, ie user@host:port")
    run_test_cmd.add_argument("--script", required=True, type=str, help="script to run")
    run_test_cmd.add_argument("--files", type=str, nargs="*", help="Files to copy, each string must be a pair of src:dest joined by a colon")
    run_test_cmd.set_defaults(func=_run_test_args)

    start_local_cmd = sub.add_parser('start_local', help='Start Local Container')
    # start_local_cmd.add_argument("--endpoint", required=True, type=str, help="User and Host and port, ie user@host:port")
    # start_local_cmd.add_argument("--script", required=True, type=str, help="script to run")
    # start_local_cmd.add_argument("--files", type=str, nargs="*", help="Files to copy, each string must be a pair of src:dest joined by a colon")
    start_local_cmd.set_defaults(func=_local_start_container_args)

    stop_local_cmd = sub.add_parser('stop_local', help='Stop Local Container')
    # stop_local_cmd.add_argument("--endpoint", required=True, type=str, help="User and Host and port, ie user@host:port")
    # stop_local_cmd.add_argument("--script", required=True, type=str, help="script to run")
    # stop_local_cmd.add_argument("--files", type=str, nargs="*", help="Files to copy, each string must be a pair of src:dest joined by a colon")
    stop_local_cmd.set_defaults(func=_local_stop_container_args)

    remote_ps_cmd = sub.add_parser('remote_ps', help='Stop Local Container')
    # remote_ps_cmd.add_argument("--endpoint", required=True, type=str, help="User and Host and port, ie user@host:port")
    # remote_ps_cmd.add_argument("--script", required=True, type=str, help="script to run")
    # remote_ps_cmd.add_argument("--files", type=str, nargs="*", help="Files to copy, each string must be a pair of src:dest joined by a colon")
    remote_ps_cmd.set_defaults(func=_remote_ps_container_args)

    remote_create_cmd = sub.add_parser('remote_create', help='Create Remote Container')
    # remote_create_cmd.add_argument("--endpoint", required=True, type=str, help="User and Host and port, ie user@host:port")
    # remote_create_cmd.add_argument("--script", required=True, type=str, help="script to run")
    # remote_create_cmd.add_argument("--files", type=str, nargs="*", help="Files to copy, each string must be a pair of src:dest joined by a colon")
    remote_create_cmd.set_defaults(func=_remote_create_container_args)

    remote_stop_cmd = sub.add_parser('remote_stop', help='Stop Remote Container')
    # remote_stop_cmd.add_argument("--endpoint", required=True, type=str, help="User and Host and port, ie user@host:port")
    # remote_stop_cmd.add_argument("--script", required=True, type=str, help="script to run")
    # remote_stop_cmd.add_argument("--files", type=str, nargs="*", help="Files to copy, each string must be a pair of src:dest joined by a colon")
    remote_stop_cmd.set_defaults(func=_remote_stop_container_args)

    get_caller_identity_cmd = sub.add_parser('get_caller_identity', help='Get the AWS IAM caller identity')
    get_caller_identity_cmd.set_defaults(func=_get_caller_identity)

    remote_get_endpoint_cmd = sub.add_parser('remote_get_endpoint', help='Get SSH remote endpoint')
    remote_get_endpoint_cmd.set_defaults(func=_remote_get_endpoint_args)

    run_e2e_test_cmd = sub.add_parser('run_e2e_test', help='Run Test')
    run_e2e_test_cmd.add_argument("--script", required=True, type=str, help="script to run")
    run_e2e_test_cmd.add_argument("--files", type=str, nargs="*", help="Files to copy, each string must be a pair of src:dest joined by a colon")
    run_e2e_test_cmd.set_defaults(func=_run_e2e_test_args)


    args = parser.parse_args()

    if args.verbose:
        logging.basicConfig(level=logging.INFO)

    if args.debug:
        logging.basicConfig(level=logging.DEBUG)

    args.func(args)


if __name__ == "__main__":
    main()
