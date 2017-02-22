"""IDL AST classes"""
from __future__ import absolute_import, print_function, unicode_literals

from typing import List, Union, Any

#from . import parser
#import yaml
#from yaml import nodes

class SourceLocation(object):
    """Source location information about an ast object"""
    def __init__(self, file_name, line, column):
        # type: (unicode, int, int) -> None
        self.file_name = file_name
        self.line = line
        self.column = column
        #super(SourceLocation, self).__init__(*args, **kwargs)

class ParserError(SourceLocation):
    def __init__(self, id, msg, file_name, line, column, *args, **kwargs):
        # type: (unicode, unicode, unicode, int, int, *str, **bool) -> None
        self.id = id
        self.msg = msg
        super(ParserError, self).__init__(file_name, line, column)

class ParserErrorCollection(object):
    """A collection of errors with line & context information"""
    def __init__(self):
        self._errors = [] # List[ParserError]

    def add(self, location, id, msg):
        # type: (SourceLocation, unicode, unicode) -> None
        """Add an error message with file (line, column) information"""
        self._errors.append(ParserError(id, msg, location.file_name, location.line, location.column))

    def has_errors(self):
        # type: () -> bool
        """Have any errors been added to the collection?"""
        return len(self._errors) > 0

    def contains(self, id):
        # type: (unicode) -> bool
        """Check if the error collection has at least one message of a given id"""
        return len([a for a in self._errors if a.id == id]) > 0

    def dump_errors(self):
        # type: () -> None
        """Print the list of errors"""
        for error in self._errors:
            error_msg = "%s: (%d, %d): %s: %s" % (error.file, error.line, error.column, error.id, error.msg)
            print ("%s\n\n" % error_msg)

class IDLParsedSpec(object):
    def __init__(self, spec, errors):
        self.spec = spec
        self.errors = errors

class IDLBoundSpec(object):
    def __init__(self, spec, errors):
        self.spec = spec
        self.errors = errors

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
        # type: (parser.ParserContext, SourceLocation, unicode, unicode) -> bool
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
        # type: (parser.ParserContext, SourceLocation, struct) -> None
        """Add an IDL struct to the symbol table and check for duplicates"""
        if not self._is_duplicate(ctxt, struct, struct.name, "struct"):
            self.structs.append(struct)

    def add_type(self, ctxt, idltype):
        """Add an IDL type to the symbol table and check for duplicates"""
        # type: (parser.ParserContext, SourceLocation, struct) -> None
        if not self._is_duplicate(ctxt, idltype, idltype.name, "type"):
            self.types.append(idltype)

    def add_command(self, ctxt, command):
        """Add an IDL command  to the symbol table  and check for duplicates"""
        pass

#TODO add file, line, col info everywhere

class Global(SourceLocation):
    """IDL global object"""
    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        self.cpp_namespace = None # type: unicode
        self.cpp_includes = [] # type: List[unicode]
        super(Global, self).__init__(file_name, line, column)

class Import(SourceLocation):
    """IDL imports object"""

class Type(SourceLocation):
    """IDL type"""
    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        self.name = None # type: unicode
        self.cpp_type = None # type: unicode
        self.bson_serialization_type = None # type: unicode
        self.serializer = None # type: unicode
        self.deserializer = None # type: unicode
        super(Type, self).__init__(file_name, line, column)

class Field(SourceLocation):
    """Fields in a struct"""
    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        self.name = None # type: unicode
        self.type = None  # type: unicode

        self.cpp_type = None # type: unicode
        self.bson_serialization_type = None # type: unicode
        self.serializer = None # type: unicode
        self.deserializer = None # type: unicode

        self.required = False # type: bool
        super(Field, self).__init__(file_name, line, column)

class Struct(SourceLocation):
    """IDL struct"""
    def __init__(self, file_name, line, column, *args, **kwargs):
        # type: (unicode, int, int, *str, **bool) -> None
        self.name = None  # type: unicode
        self.fields = []  # type: List[Field]
        super(Struct, self).__init__(file_name, line, column)

class Command(Struct):
    """IDL command"""
