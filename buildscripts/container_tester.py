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
import threading
import subprocess
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

#def ssh(endpoint)
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
    cmd = ["scp", "-P", port, src, "%s@%s:%s" % (user, host, dest) ]
    _run_process(cmd)

def _ssh(endpoint, cmd):
    (user, host, port) = _userandhostandport(endpoint)
    cmd = ["ssh", "-p", port, "%s@%s" % (user, host), cmd ]
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



def main() -> None:
    """Execute Main entry point."""

    parser = argparse.ArgumentParser(description='Quick C++ Lint frontend.')

    parser.add_argument('-v', "--verbose", action='store_true', help="Enable verbose logging")

    sub = parser.add_subparsers(title="Linter subcommands", help="sub-command help")

    run_test_cmd = sub.add_parser('run_test', help='Run Test')
    run_test_cmd.add_argument("--endpoint", required=True, type=str, help="User and Host and port, ie user@host:port")
    run_test_cmd.add_argument("--script", required=True, type=str, help="script to run")
    run_test_cmd.add_argument("--files", type=str, nargs="*", help="Files to copy, each string must be a pair of src:dest joined by a colon")
    run_test_cmd.set_defaults(func=_run_test_args)

    args = parser.parse_args()

    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)

    args.func(args)


if __name__ == "__main__":
    main()
