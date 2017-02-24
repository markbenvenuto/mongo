"""
IDL Parser Syntax classes.

These class represent the structure of the raw IDL document.
It maps 1-1 to the YAML file, and has not been checked if
it follows the rules of the IDL, etc.
"""
from __future__ import absolute_import, print_function, unicode_literals

from typing import List, Union, Any, Optional, Tuple

from . import common
from . import errors


# TODO: add dump to yaml support
class IDLSpec(object):
    """
    The in-memory representation of an IDL file.

    - Includes all imported files
    - Fields may have had types derived depending on pass run
    """

    def __init__(self, *args, **kwargs):
        """Construct an IDL spec"""
        self.symbols = SymbolTable()  # type: SymbolTable
        self.globals = None  # type: Optional[Globals]
        #TODO self.imports = None # type: Optional[Imports]

        super(IDLSpec, self).__init__(*args, **kwargs)


class IDLParsedSpec(object):
    """A parsed IDL document or a set of errors if parsing failed."""

    def __init__(self, spec, error_collection):
        """Must specify either a IDL document or errors, not both."""
        # type: (IDLSpec, errors.ParserErrorCollection) -> None
        assert (spec is None and error_collection is not None) or (spec is not None and
                                                                   error_collection is None)
        self.spec = spec
        self.errors = error_collection


class SymbolTable(object):
    """
    IDL Symbol Table

    - Contains all information ...
    TODO fill out
    """

    def __init__(self, *args, **kwargs):
        """Construct an empty symbol table."""
        self.types = []  # type: List[Type]
        self.structs = []  # type: List[Struct]
        self.commands = []  # type: List[Command]

        super(SymbolTable, self).__init__(*args, **kwargs)

    def _is_duplicate(self, ctxt, location, name, duplicate_class_name):
        # type: (errors.ParserContext, common.SourceLocation, unicode, unicode) -> bool
        """Return true if the given item already exist in the symbol table."""
        for struct in self.structs:
            if struct.name == name:
                ctxt.add_duplicate_symbol_error(location, name, duplicate_class_name, 'struct')
                return True
        for idltype in self.types:
            if idltype.name == name:
                ctxt.add_duplicate_symbol_error(location, name, duplicate_class_name, 'type')
                return True

        return False

    def add_struct(self, ctxt, struct):
        # type: (errors.ParserContext, Struct) -> None
        """Add an IDL struct to the symbol table and check for duplicates."""
        if not self._is_duplicate(ctxt, struct, struct.name, "struct"):
            self.structs.append(struct)

    def add_type(self, ctxt, idltype):
        """Add an IDL type to the symbol table and check for duplicates."""
        # type: (errors.ParserContext, Type) -> None
        if not self._is_duplicate(ctxt, idltype, idltype.name, "type"):
            self.types.append(idltype)

    def resolve_field_type(self, ctxt, field):
        # type: (errors.ParserContext, Field) -> Tuple[Optional[Struct], Optional[Type]]
        """Find the type or struct a field refers to or log an error."""
        for idltype in self.types:
            if idltype.name == field.type:
                return (None, idltype)

        for struct in self.structs:
            if struct.name == field.type:
                return (struct, None)

        ctxt.add_unknown_type_error(field, field.name, field.type)
        return (None, None)

    def add_command(self, ctxt, command):
        """Add an IDL command  to the symbol table and check for duplicates."""
        pass


class Global(common.SourceLocation):
    """
    IDL global object container.

    Not all fields are populated.
    """

    def __init__(self, file_name, line, column, *args, **kwargs):
        """Construct a Global."""
        # type: (unicode, int, int, *str, **bool) -> None
        self.cpp_namespace = None  # type: unicode
        self.cpp_includes = []  # type: List[unicode]
        super(Global, self).__init__(file_name, line, column)


class Import(common.SourceLocation):
    """IDL imports object."""

    pass


class Type(common.SourceLocation):
    """
    Stores all type information about an IDL type.

    Not all fields are populated.
    """

    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        """Construct a Type."""
        self.name = None  # type: unicode
        self.cpp_type = None  # type: unicode
        self.bson_serialization_type = None  # type: unicode
        self.serializer = None  # type: unicode
        self.deserializer = None  # type: unicode
        super(Type, self).__init__(file_name, line, column)


class Field(common.SourceLocation):
    """
    An instance of a field in a struct.

    Name is always populated.
    Other fields may not be populated.
    """

    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        """Construct a Field."""
        self.name = None  # type: unicode
        self.type = None  # type: unicode
        self.ignore = False  # type: bool
        self.required = False  # type: bool

        # Properties common to type and fields
        self.cpp_type = None  # type: unicode
        self.bson_serialization_type = None  # type: unicode
        self.serializer = None  # type: unicode
        self.deserializer = None  # type: unicode

        super(Field, self).__init__(file_name, line, column)


class Struct(common.SourceLocation):
    """
    IDL struct information.

    Not all fields are populated.
    """

    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        """Construct a Struct."""
        self.name = None  # type: unicode
        self.fields = []  # type: List[Field]
        super(Struct, self).__init__(file_name, line, column)


class Command(Struct):
    """IDL command."""
