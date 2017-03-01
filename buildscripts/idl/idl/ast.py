"""
IDL AST classes.

Represents the derived IDL specification after type resolution has occurred.

This is a lossy translation as the IDL AST only contains information about what structs
need code generated for them, and just enough information to do that.

"""
from __future__ import absolute_import, print_function, unicode_literals

from typing import List, Union, Any, Optional, Tuple

from . import common
from . import errors


# TODO: add dump to yaml support
class IDLAST(object):
    """
    The in-memory representation of an IDL file.

    - Includes all imported files
    - Fields may have had types derived depending on pass run
    """

    def __init__(self):
        # type: () -> None
        """Construct a IDLAST."""
        self.globals = None  # type: Global
        self.structs = []  # type: List[Struct]


class IDLBoundSpec(object):
    """A bound IDL document or a set of errors if parsing failed."""

    def __init__(self, spec, error_collection):
        # type: (IDLAST, errors.ParserErrorCollection) -> None
        """Must specify either a IDL document or errors, not both."""
        assert (spec is None and error_collection is not None) or (spec is not None and
                                                                   error_collection is None)
        self.spec = spec
        self.errors = error_collection


class Global(common.SourceLocation):
    """
    IDL global object container.

    Not all fields are populated.
    """

    def __init__(self, file_name, line, column):
        # type: (unicode, int, int) -> None
        """Construct a Global."""
        self.cpp_namespace = None  # type: unicode
        self.cpp_includes = []  # type: List[unicode]
        super(Global, self).__init__(file_name, line, column)


class Field(common.SourceLocation):
    """
    An instance of a field in a struct.

    Name is always populated.
    A struct will either have a struct or a cpp_type, not both.
    Not all fields are set.
    """

    def __init__(self, file_name, line, column):
        # type: (unicode, int, int) -> None
        """Construct a Field."""
        self.name = None  # type: unicode
        self.description = None  # type: unicode
        self.optional = False  # type: bool
        self.ignore = False  # type: bool

        # Properties specific to fields which are types
        self.cpp_type = None  # type: unicode
        self.bson_serialization_type = None  # type: List[unicode]
        self.serializer = None  # type: unicode
        self.deserializer = None  # type: unicode
        self.bindata_subtype = None  # type: unicode
        self.default = None  # type: unicode

        # Properties specific to fields with are structs
        self.struct_type = None  # type: unicode

        super(Field, self).__init__(file_name, line, column)


class Struct(common.SourceLocation):
    """
    IDL struct information.

    Not all fields are populated.
    """

    def __init__(self, file_name, line, column):
        # type: (unicode, int, int) -> None
        """Construct a struct."""
        self.name = None  # type: unicode
        self.description = None  # type: unicode
        self.fields = []  # type: List[Field]
        super(Struct, self).__init__(file_name, line, column)
