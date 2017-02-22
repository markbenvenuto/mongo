"""IDL AST classes"""
from __future__ import absolute_import, print_function, unicode_literals

from typing import List, Union, Any, Optional, Tuple

from . import common
from . import errors
#import yaml
#from yaml import nodes


# TODO: add dump to yaml support
class IDLSpec(object):
    """The in-memory representation of an IDL file

        - Includes all imported files
        - Fields may have had types derived depending on pass run
    """
    def __init__(self, *args, **kwargs):
        self.symbols = SymbolTable()
        self.globals = None
        self.imports = None

        super(IDLSpec, self).__init__(*args, **kwargs)

class IDLParsedSpec(object):
    def __init__(self, spec, error_collection):
        # type: (IDLSpec, errors.ParserErrorCollection) -> None
        self.spec = spec
        self.errors = error_collection

class IDLBoundSpec(object):
    def __init__(self, spec, error_collection):
        # type: (IDLSpec, errors.ParserErrorCollection) -> None
        self.spec = spec
        self.errors = error_collection


class SymbolTable(object):
    """IDL Symbol Table

        - Contains all information ...
    """

    def __init__(self, *args, **kwargs):
        self.types = []
        self.structs = []
        self.commands = []

        super(SymbolTable, self).__init__(*args, **kwargs)

    def _is_duplicate(self, ctxt, location, name, duplicate_class_name):
        # type: (errors.ParserContext, common.SourceLocation, unicode, unicode) -> bool
        """Does the given item already exist in the symbol table"""
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
        """Add an IDL struct to the symbol table and check for duplicates"""
        if not self._is_duplicate(ctxt, struct, struct.name, "struct"):
            self.structs.append(struct)

    def add_type(self, ctxt, idltype):
        """Add an IDL type to the symbol table and check for duplicates"""
        # type: (errors.ParserContext, Type) -> None
        if not self._is_duplicate(ctxt, idltype, idltype.name, "type"):
            self.types.append(idltype)

    def resolve_field_type(self, ctxt, field):
        # type: (errors.ParserContext, Field) -> Tuple[Struct, Type]
        for idltype in self.types:
            if idltype.name == field.type:
                return (None, idltype)

        for struct in self.structs:
            if struct.name == field.type:
                return (struct, None)
        
        ctxt.add_unknown_type_error(field, field.name, field.type)
        return (None, None)

    def add_command(self, ctxt, command):
        """Add an IDL command  to the symbol table  and check for duplicates"""
        pass

#TODO add file, line, col info everywhere

class Global(common.SourceLocation):
    """IDL global object"""
    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        self.cpp_namespace = None # type: unicode
        self.cpp_includes = [] # type: List[unicode]
        super(Global, self).__init__(file_name, line, column)

class Import(common.SourceLocation):
    """IDL imports object"""

class Type(common.SourceLocation):
    """IDL type"""
    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        self.name = None # type: unicode
        self.cpp_type = None # type: unicode
        self.bson_serialization_type = None # type: unicode
        self.serializer = None # type: unicode
        self.deserializer = None # type: unicode
        super(Type, self).__init__(file_name, line, column)

class Field(common.SourceLocation):
    """Fields in a struct"""
    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        self.name = None # type: unicode
        self.type = None  # type: unicode

        # Properties common to types
        self.cpp_type = None # type: unicode
        self.bson_serialization_type = None # type: unicode
        self.serializer = None # type: unicode
        self.deserializer = None # type: unicode

        # Properties specific to fields
        self.struct = None # type: Struct
        self.required = False # type: bool
        self.ignore = False # type: bool
        super(Field, self).__init__(file_name, line, column)

class Struct(common.SourceLocation):
    """IDL struct"""
    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        self.name = None  # type: unicode
        self.fields = []  # type: List[Field]
        super(Struct, self).__init__(file_name, line, column)

class Command(Struct):
    """IDL command"""
