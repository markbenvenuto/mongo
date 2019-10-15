"""Simple C++ linter support module."""

from typing import List

from . import base


class SimpleCppLinter(base.LinterBase):
    """Simple C++ linter."""

    def __init__(self):
        # type: () -> None
        """Create a Simple C++ linter."""
        super(SimpleCppLinter, self).__init__("buildscripts/linter/simplecpplint.py", "Simple")

    def get_lint_version_cmd_args(self):
        # type: () -> List[str]
        """Get the command to run a linter version check."""
        return ["--help"]

    def get_lint_cmd_args(self, file_name):
        # type: (str) -> List[str]
        """Get the command to run a linter."""
        return ["buildscripts/linter/simplecpplint.py", file_name]
