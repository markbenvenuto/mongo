#!/usr/bin/env python2
"""Extensible script to run one or more Python Linters across a subset of files in parallel."""
from __future__ import absolute_import
from __future__ import print_function

import argparse
import difflib
import os
import re
import subprocess
import sys
import threading
from abc import ABCMeta, abstractmethod
from typing import Any, Callable, Dict, List, Tuple

# Get relative imports to work when the package is not installed on the PYTHONPATH.
if __name__ == "__main__" and __package__ is None:
    sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(os.path.realpath(__file__)))))

from buildscripts.linter import git  # pylint: disable=wrong-import-position
from buildscripts.linter import parallel  # pylint: disable=wrong-import-position

##############################################################################


class LinterBase(object):
    """Base Class for all linters."""

    __metaclass__ = ABCMeta

    def __init__(self, cmd_name, required_version, cmd_path):
        # type: (str, str, str) -> None
        """Create a linter with a short friendly name, and a path to the linter to run."""
        self.cmd_name = cmd_name
        self._cmd_path = cmd_path
        self._required_version = required_version

    @abstractmethod
    def get_lint_cmd(self, file_name):
        # type: (str) -> List[str]
        """Get the command to run a linter."""
        pass

    @abstractmethod
    def get_lint_version_cmd(self):
        # type: () -> List[str]
        """Get the command to run a linter version check."""
        pass

    def needs_file_diff(self):
        # type: () -> bool
        # pylint: disable=no-self-use
        """
        Check if we need to diff the output of this linter with the original file.

        This applies to tools like clang_format and yapf which do not have a notion of linting. we
        introduce the idea of linting by formatting a file with the tool to standard out and
        comparing it to the original.
        """
        return False

    def get_required_version(self):
        # type: () -> str
        """Check the required version of the linter."""
        return self._required_version

    def update_config(self, config_dict):
        # type: (Dict[str,str]) -> None
        """Update the binary location for a linter."""
        if self.cmd_name in config_dict and config_dict[self.cmd_name]:
            self._cmd_path = config_dict[self.cmd_name]


class PyLintLinter(LinterBase):
    """Pylint linter."""

    def __init__(self):
        # type: () -> None
        """Create a pylint linter."""
        self._rc_file = os.path.join(
            os.path.normpath(git.get_base_dir()), "buildscripts", "pylintrc")
        super(PyLintLinter, self).__init__("pylint", "pylint 1.6.5", "pylint")

    def get_lint_version_cmd(self):
        # type: () -> List[str]
        """Get the command to run a linter version check."""
        return [self._cmd_path, "--version"]

    def get_lint_cmd(self, file_name):
        # type: (str) -> List[str]
        """Get the command to run a linter."""
        # pylintrc only searches parent directories if it is a part of a module, and since our code
        # is split across modules, we need to specify the path to the rcfile.
        # See https://pylint.readthedocs.io/en/latest/user_guide/run.html
        return [
            self._cmd_path, "--rcfile=%s" % (self._rc_file), "--output-format", "msvs",
            "--reports=n", file_name
        ]


class PyDocstyleLinter(LinterBase):
    """PyDocStyle linter."""

    def __init__(self):
        # type: () -> None
        """Create a pydocstyle linter."""
        super(PyDocstyleLinter, self).__init__("pydocstyle", "1.1.1", "pydocstyle")

    def get_lint_version_cmd(self):
        # type: () -> List[str]
        """Get the command to run a linter version check."""
        return [self._cmd_path, "--version"]

    def get_lint_cmd(self, file_name):
        # type: (str) -> List[str]
        """Get the command to run a linter."""
        return [self._cmd_path, file_name]


class MypyLinter(LinterBase):
    """My-Py linter."""

    def __init__(self):
        # type: () -> None
        """Create a my-py linter."""
        #super(MypyLinter, self).__init__("mypy", "mypy")
        super(MypyLinter, self).__init__("mypy", "mypy 0.501", "c:\\python36\\scripts\\mypy.bat")

    def get_lint_version_cmd(self):
        # type: () -> List[str]
        """Get the command to run a linter version check."""
        return [self._cmd_path, "--version"]

    def get_lint_cmd(self, file_name):
        # type: (str) -> List[str]
        """Get the command to run a linter."""
        return [
            self._cmd_path, "--py2", "--disallow-untyped-defs", "--ignore-missing-imports",
            file_name
        ]


class YapfLinter(LinterBase):
    """Yapf linter."""

    def __init__(self):
        # type: () -> None
        """Create a yapf linter."""
        super(YapfLinter, self).__init__("yapf", "yapf 0.16.0", "yapf")

    def get_lint_version_cmd(self):
        # type: () -> List[str]
        """Get the command to run a linter version check."""
        return [self._cmd_path, "--version"]

    def needs_file_diff(self):
        # type: () -> bool
        """See comment in base class."""
        return True

    def get_lint_cmd(self, file_name):
        # type: (str) -> List[str]
        """Get the command to run a linter."""
        return [self._cmd_path, file_name]

    def get_fix_cmd(self, file_name):
        # type: (str) -> List[str]
        """Get the command to run a linter fix."""
        return [self._cmd_path, "-i", file_name]


# List of supported linters
_LINTERS = [
    YapfLinter(),
    PyLintLinter(),
    PyDocstyleLinter(),
    MypyLinter(),
]


def get_py_linter(linter_filter):
    # type: (str) -> List[LinterBase]
    """Get a list of linters ."""
    if linter_filter is None or linter_filter == "all":
        return _LINTERS

    linter_list = linter_filter.split(",")

    linter_candidates = [linter for linter in _LINTERS if linter in linter_list]

    if len(linter_candidates) == 0:
        raise ValueError("No linters found for filter '%s'" % (linter_filter))

    return linter_candidates


class LintRunner(object):
    """Run a linter and print results in a thread safe manner."""

    def __init__(self):
        # type: () -> None
        """Create a Lint Runner."""
        self.print_lock = threading.Lock()

    def _safe_print(self, line):
        # type: (str) -> None
        """
        Print a line of text under a lock.

        Take a lock to ensure diffs do not get mixed when printed to the screen.
        """
        with self.print_lock:
            print(line)

    def check_versions(self, linter_list):
        # type: (List[LinterBase]) -> None
        """Check if all the linters have the correct versions."""
        for linter in linter_list:

            required_version = linter.get_required_version()
            cmd = linter.get_lint_version_cmd()

            try:
                print("cmd: " + str(cmd))
                output = subprocess.check_output(cmd)
                if not required_version in output:
                    raise ValueError("Linter %s has wrong version. Expected '%s', received '%s'" %
                                     ('TODO', required_version, output))
            except subprocess.CalledProcessError as cpe:
                self._safe_print("CMD [%s] failed:\n%s" % (cmd, cpe.output))

    def run_lint(self, linter, file_name):
        # type: (LinterBase, str) -> bool
        # pylint: disable=unused-argument
        """Run the specified linter for the file."""

        cmd = linter.get_lint_cmd(file_name)

        self._safe_print("cmd: " + str(cmd))
        try:
            if linter.needs_file_diff():
                # Need a file diff
                with open(file_name, 'rb') as original_text:
                    original_file = original_text.read()

                formatted_file = subprocess.check_output(cmd)
                if original_file != formatted_file:
                    original_lines = original_file.splitlines()
                    formatted_lines = formatted_file.splitlines()
                    result = difflib.unified_diff(original_lines, formatted_lines)

                    # Take a lock to ensure diffs do not get mixed when printed to the screen
                    with self.print_lock:
                        print("ERROR: Found diff for " + file_name)
                        print("To fix formatting errors, run pylinters.py fix %s" % (file_name))

                        count = 0
                        for line in result:
                            print(line.rstrip())
                            count += 1

                        if count == 0:
                            print("ERROR: The files only differ in trailing whitespace? LF vs CRLF")

                    return False
            else:
                subprocess.check_output(cmd)

        except subprocess.CalledProcessError as cpe:
            self._safe_print("CMD [%s] failed:\n%s" % (cmd, cpe.output))
            return False

        return True

    def run(self, linter, cmd):
        # type: (LinterBase, List[str]) -> bool
        # pylint: disable=unused-argument
        """Check the specified cmd succeeds."""
        self._safe_print("cmd: " + str(cmd))
        try:
            subprocess.check_output(cmd)
        except subprocess.CalledProcessError as cpe:
            self._safe_print("CMD [%s] failed:\n%s" % (cmd, cpe.output))
            return False

        return True


def is_interesting_file(file_name):
    # type: (str) -> bool
    """"Return true if this file should be checked."""
    return file_name.endswith(".py") and (file_name.startswith("buildscripts/idl") or
                                          file_name.startswith("buildscripts/linter") or
                                          file_name.startswith("buildscripts/pylinters.py"))


def get_list_from_lines(lines):
    # type: (str) -> List[str]
    """"Convert a string containing a series of lines into a list of strings."""
    return [line.rstrip() for line in lines.splitlines()]


def _get_build_dir():
    # type: () -> str
    """Get the location of the scons' build directory in case we need to download clang-format."""
    return os.path.join(git.get_base_dir(), "build")


def _lint_files(linters, config_dict, file_names):
    # type: (str, Dict[str, str], List[str]) -> None
    """Lint a list of files with clang-format."""
    linter_list = get_py_linter(linters)

    runner = LintRunner()

    runner.check_versions(linter_list)

    for linter in linter_list:
        linter.update_config(config_dict)
        lint_clean = parallel.parallel_process([os.path.abspath(f) for f in file_names],
                                               lambda param1: runner.run_lint(linter, param1))  # pylint: disable=cell-var-from-loop

        if not lint_clean:
            print("ERROR: Code Style does not match coding style")
            sys.exit(1)


def lint_patch(linters, config_dict, file_name):
    # type: (str, Dict[str, str], List[str]) -> None
    """Lint patch command entry point."""
    file_names = git.get_files_to_check_from_patch(file_name, is_interesting_file)

    # Patch may have files that we do not want to check which is fine
    if file_names:
        _lint_files(linters, config_dict, file_names)


def lint(linters, config_dict, file_names):
    # type: (str, Dict[str, str], List[str]) -> None
    """Lint files command entry point."""
    all_file_names = git.get_files_to_check(file_names, is_interesting_file)

    _lint_files(linters, config_dict, all_file_names)


def lint_all(linters, config_dict, file_names):
    # type: (str, Dict[str, str], List[str]) -> None
    # pylint: disable=unused-argument
    """Lint files command entry point based on working tree."""
    all_file_names = git.get_files_to_check_working_tree(is_interesting_file)

    _lint_files(linters, config_dict, all_file_names)


def _run_fix(runner, linter, file_name):
    # type: (LintRunner, LinterBase, str) -> bool
    # This is a separate function so the mypy type error and pylint lambada local variable
    # errors separately.
    return runner.run(linter, linter.get_fix_cmd(file_name))  # type: ignore


def _fix_files(linters, config_dict, file_names):
    # type: (str, Dict[str, str], List[str]) -> None
    """Fix a list of files with linters if possible."""
    linter_list = get_py_linter(linters)

    # Maybe use abc and isinstance instead here?
    fix_list = [fixer for fixer in linter_list if getattr(fixer, 'get_fix_cmd', None)]

    if len(fix_list) == 0:
        raise ValueError("Cannot find any linters '%s' that support fixing." % (linters))

    runner = LintRunner()

    runner.check_versions(fix_list)

    for linter in fix_list:
        linter.update_config(config_dict)

        run_linter = lambda param1: _run_fix(runner, linter, param1)  # pylint: disable=cell-var-from-loop

        lint_clean = parallel.parallel_process([os.path.abspath(f) for f in file_names], run_linter)

        if not lint_clean:
            print("ERROR: Code Style does not match coding style")
            sys.exit(1)


def fix_func(linters, config_dict, file_names):
    # type: (str, Dict[str, str], List[str]) -> None
    """Fix files command entry point."""
    all_file_names = git.get_files_to_check(file_names, is_interesting_file)

    _fix_files(linters, config_dict, all_file_names)


def main():
    # type: () -> None
    """Main entry point."""
    parser = argparse.ArgumentParser(description='PyLinter frontend.')

    linters = get_py_linter(None)

    dest_prefix = "linter_"
    for linter1 in linters:
        msg = 'Path to linter %s' % (linter1.cmd_name)
        parser.add_argument(
            '--' + linter1.cmd_name, type=str, help=msg, dest=dest_prefix + linter1.cmd_name)

    parser.add_argument(
        '--linters', type=str, help="List of filters to use, defaults to all", default="all")

    sub = parser.add_subparsers(
        title="Linter subcommands", description="foo", help="sub-command help")

    parser_lint = sub.add_parser('lint', help='Lint only Git files')
    parser_lint.add_argument("file_names", nargs="*", help="fooooo")
    parser_lint.set_defaults(func=lint)

    parser_lint_all = sub.add_parser('lint-all', help='Lint All files')
    parser_lint_all.add_argument("file_names", nargs="*", help="fooooo")
    parser_lint_all.set_defaults(func=lint_all)

    parser_lint_patch = sub.add_parser('lint-patch', help='Lint the files in a patch')
    parser_lint_patch.add_argument("file_names", nargs="*", help="fooooo", metavar="FILE")
    parser_lint_patch.set_defaults(func=lint_patch)

    parser_fix = sub.add_parser('fix', help='Fix files if possible')
    parser_fix.add_argument("file_names", nargs="*", help="fooooo")
    parser_fix.set_defaults(func=fix_func)

    args = parser.parse_args()

    config_dict = {}
    for key in args.__dict__:
        if key.startswith("linter_"):
            name = key.replace(dest_prefix, "")
            config_dict[name] = args.__dict__[key]

    args.func(args.linters, config_dict, args.file_names)


if __name__ == "__main__":
    main()
