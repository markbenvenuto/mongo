"""IDL AST classes"""
from __future__ import absolute_import, print_function

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

    def _is_duplicate(self, ctxt, node, name, duplicate_class_name):
        """Does the given item already exist in the symbol table"""
        for struct in self.structs:
            if struct.name == name:
                ctxt.add_duplicate_symbol_error(node, name, duplicate_class_name, 'struct')
                return False
        return True

    def add_struct(self, ctxt, node, struct):
        """Add an IDL struct to the symbol table and check for duplicates"""
        if not self._is_duplicate(ctxt, node, struct.name, "struct"):
            self.structs.append(struct)

    def add_type(self, ctxt, node, idltype):
        """Add an IDL type to the symbol table and check for duplicates"""
        pass

    def add_command(self, ctxt, node, command):
        """Add an IDL command  to the symbol table  and check for duplicates"""
        pass

class Global(object):
    """IDL global object"""
    def __init__(self, *args, **kwargs):
        self.cpp_namespace = None
        self.cpp_includes = []
        super(Global, self).__init__(*args, **kwargs)

class Import(object):
    """IDL imports object"""

class Type(object):
    """IDL type"""

class Field(object):
    """fields in a struct"""
    def __init__(self, *args, **kwargs):
        self.name = None
        super(Field, self).__init__(*args, **kwargs)

class Struct(object):
    """IDL struct"""
    def __init__(self, *args, **kwargs):
        self.name = None
        self.fields = []
        super(Struct, self).__init__(*args, **kwargs)

class Command(Struct):
    """IDL command"""
