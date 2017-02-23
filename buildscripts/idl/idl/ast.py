"""IDL AST classes"""
from __future__ import absolute_import, print_function, unicode_literals

from typing import List, Union, Any, Optional, Tuple

from . import common
from . import errors


# TODO: add dump to yaml support
class IDLAST(object):
    """The in-memory representation of an IDL file

        - Includes all imported files
        - Fields may have had types derived depending on pass run
    """

    def __init__(self, *args, **kwargs):
        self.globals = None  # Global
        self.structs = []  # List[Struct]

        super(IDLAST, self).__init__(*args, **kwargs)


class IDLBoundSpec(object):
    def __init__(self, spec, error_collection):
        # type: (IDLAST, errors.ParserErrorCollection) -> None
        self.spec = spec
        self.errors = error_collection


class Global(common.SourceLocation):
    """IDL global object"""

    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        self.cpp_namespace = None  # type: unicode
        self.cpp_includes = []  # type: List[unicode]
        super(Global, self).__init__(file_name, line, column)


class Field(common.SourceLocation):
    """Fields in a struct"""

    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        self.name = None  # type: unicode
        self.required = False  # type: bool
        self.ignore = False  # type: bool

        # Properties specific to fields which are types
        self.cpp_type = None  # type: unicode
        self.bson_serialization_type = None  # type: unicode
        self.serializer = None  # type: unicode
        self.deserializer = None  # type: unicode

        # Properties specific to fields with are structs
        self.struct = None  # type: Struct

        super(Field, self).__init__(file_name, line, column)


class Struct(common.SourceLocation):
    """IDL struct"""

    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        self.name = None  # type: unicode
        self.fields = []  # type: List[Field]
        super(Struct, self).__init__(file_name, line, column)
