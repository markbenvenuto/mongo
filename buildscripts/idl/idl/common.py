"""
IDL Common classes.

Classes are shared among both the syntax and ast trees.
"""
from __future__ import absolute_import, print_function, unicode_literals
  

class SourceLocation(object):
    """Source location information about a syntax or ast object."""

    def __init__(self, file_name, line, column):
        # type: (unicode, int, int) -> None
        """Construct a source location."""
        self.file_name = file_name
        self.line = line
        self.column = column
        #super(SourceLocation, self).__init__(*args, **kwargs)
