#!/usr/bin/env python3
"""
Script for testing mongodb in containers.

Requires ssh, scp, and sh on local and remote hosts.
Assumes remote host is Linux
"""

import argparse
import datetime
import json
import logging
import os
import pprint
import re
import subprocess
import sys
import threading
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

# TODO
# 2. Add way to garbage collect all tasks older then X?
#    - May not need tags if all confined to one cluster

############################################################################
# Local configuration settings for testing against the local docker/podman
#
# TODO - in order to finish the ECS emulation, we have to tweak the networking of the container
DOCKER_EXECUTABLE = "podman"

IMAGE_NAME = "fedora_sshd"

CONTAINER_DEFAULT_PORT = 4000

CONTAINER_DEFAULT_NAME = "container_tester_test1"
############################################################################


############################################################################
# Configuration settings for working with a ECS cluster in a region
#

# These settings depend on a cluster, task subnet, and security group already setup
ECS_DEFAULT_CLUSTER = "arn:aws:ecs:us-east-2:579766882180:cluster/tf-mcb-ecs-cluster"
ECS_DEFAULT_TASK_DEFINITION = "arn:aws:ecs:us-east-2:579766882180:task-definition/tf-app:2"
ECS_DEFAULT_SUBNET = 'subnet-a5e114cc'
# Must allow ssh from 0.0.0.0
ECS_DEFAULT_SECURITY_GROUP = 'sg-051a91d96332f8f3a'

# This is just a string local to this file
DEFAULT_SERVICE_NAME = 'script-test2'

# Garbage collection threshold for old/stale services
DEFAULT_GARBAGE_COLLECTION_THRESHOLD = datetime.timedelta(hours=1)

############################################################################


def _run_process(params, cwd=None):
    LOGGER.info("RUNNING COMMAND: %s", params)
    ret = subprocess.run(params, cwd=cwd)
    return ret.returncode

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
    LOGGER.info("RETURN CODE: %s", ret)
    return ret

def _run_test_args(args):
    run_test(args.endpoint, args.script, args.files)

def run_test(endpoint, script, files):
    """
    Run a test on a machine

    Steps
    1. Copy over a files which are tuples of (src, dest)
    2. Copy over the test script to "/tmp/test.sh"
    3. Run the test script and return the results
    """
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
    port_mapping = f"127.0.0.1:{CONTAINER_DEFAULT_PORT}:22"
    _run_process([DOCKER_EXECUTABLE, "run", "-p", port_mapping, "--name", CONTAINER_DEFAULT_NAME, "--rm", "-d", IMAGE_NAME ])

def _local_stop_container_args(args):
     _run_process([DOCKER_EXECUTABLE, "stop", CONTAINER_DEFAULT_NAME])

def _get_region(arn):
    return arn.split(':')[3]


def _remote_ps_container_args(args):
    remote_ps_container(ECS_DEFAULT_CLUSTER)

def remote_ps_container(cluster):
    """
    Get a list of task running in the cluster with their network addresses.

    Emulates the docker ps and ecs-cli ps commands.
    """
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
    remote_create_container(ECS_DEFAULT_CLUSTER, ECS_DEFAULT_TASK_DEFINITION, DEFAULT_SERVICE_NAME, ECS_DEFAULT_SUBNET, ECS_DEFAULT_SECURITY_GROUP)

def remote_create_container(cluster, task_definition, service_name, subnet, security_group):
    """
    Create a task in ECS
    """
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
    remote_stop_container(ECS_DEFAULT_CLUSTER, DEFAULT_SERVICE_NAME)

def remote_stop_container(cluster, service_name):
    """
    Stop a ECS task
    """
    ecs_client = boto3.client('ecs', region_name=_get_region(cluster))

    resp = ecs_client.delete_service(cluster=cluster, service=service_name, force=True)
    pprint.pprint(resp)

    service_arn = resp["service"]["serviceArn"]

    print(f"Waiting for Service {service_arn} to become inactive...")
    waiter = ecs_client.get_waiter('services_inactive')

    waiter.wait(cluster=cluster, services=[service_arn])

def _remote_gc_services_container_args(args):
    remote_gc_services_container(ECS_DEFAULT_CLUSTER)

def remote_gc_services_container(cluster):
    """
    Delete all ECS services over then a given treshold.
    """
    ecs_client = boto3.client('ecs', region_name=_get_region(cluster))

    services = ecs_client.list_services(cluster=cluster)

    services_details = ecs_client.describe_services(cluster=cluster, services=services["serviceArns"])

    not_expired_now = datetime.datetime.now().astimezone() - DEFAULT_GARBAGE_COLLECTION_THRESHOLD

    for service in services_details["services"]:
        created_at = service["createdAt"]

        # Find the services that we created "too" long ago
        if created_at < not_expired_now:
            print("DELETING expired service %s which was created at %s." % (service["serviceName"], created_at))

            remote_stop_container(cluster, service["serviceName"])

def remote_get_endpoint_str(cluster, service_name):
    """
    Get an SSH connection string for the remote service
    """
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
    endpoint = remote_get_endpoint_str(cluster, service_name)
    print(endpoint)

def _get_caller_identity(args):
    sts_client = boto3.client('sts')

    pprint.pprint(sts_client.get_caller_identity())


def _run_e2e_test_args(args):
    _run_e2e_test(args.script, args.files)

def _run_e2e_test(script, files):
    """
    Run a test end-to-end

    1. Start an ECS service
    2. Copy the files over and run the test
    3. Stop the ECS service
    """
    remote_create_container(ECS_DEFAULT_CLUSTER, ECS_DEFAULT_TASK_DEFINITION, DEFAULT_SERVICE_NAME, ECS_DEFAULT_SUBNET, ECS_DEFAULT_SECURITY_GROUP)

    endpoint = remote_get_endpoint_str(ECS_DEFAULT_CLUSTER, DEFAULT_SERVICE_NAME)

    try:
        run_test(endpoint, script, files)
    finally:
        remote_stop_container(ECS_DEFAULT_CLUSTER, DEFAULT_SERVICE_NAME)

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

    remote_gc_services_cmd = sub.add_parser('remote_gc_services', help='GC Remote Container')
    # remote_gc_services_cmd.add_argument("--endpoint", required=True, type=str, help="User and Host and port, ie user@host:port")
    # remote_gc_services_cmd.add_argument("--script", required=True, type=str, help="script to run")
    # remote_gc_services_cmd.add_argument("--files", type=str, nargs="*", help="Files to copy, each string must be a pair of src:dest joined by a colon")
    remote_gc_services_cmd.set_defaults(func=_remote_gc_services_container_args)

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
