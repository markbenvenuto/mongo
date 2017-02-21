def fooar():
    print "Asas"

class IDLSpec(object):
    def __init__(self, *args, **kwargs):
        self.symbols = symbol_table()
        self.globals = None
        self.imports = None

        return super(IDLSpec, self).__init__(*args, **kwargs)



class symbol_table(object):
    """description of class"""

    def __init__(self, *args, **kwargs):
        self.types = []
        self.structs = []
        self.commands = []

        return super(symbol_table, self).__init__(*args, **kwargs)
     
    def _isDuplicate(self, ctxt, node, name, duplicate_class_name):
        for struct in self.structs:
            if struct.name == name:
                ctxt.addDuplicateSymbolError(node, name, duplicate_class_name, 'struct')
                return False
        return True

    def addStruct(self, ctxt, node, struct):
        if not self._isDuplicate(ctxt, node, struct.name, "struct"):
            self.structs.append(struct)

    def addType(self, ctxt, node, type):
        pass

    def addCommand(self, ctxt, node, command):
        pass
       
class idl_global(object):
    """IDL global object"""
    def __init__(self, *args, **kwargs):
        self.cpp_namespace = None
        self.cpp_includes = []
        return super(idl_global, self).__init__(*args, **kwargs)

class idl_import(object):
    """IDL imports object"""

class type(object):
    """type"""

class field(object):
    """fields"""
    def __init__(self, *args, **kwargs):
        self.name = None
        return super(struct, self).__init__(*args, **kwargs)

class struct(object):
    """IDL struct"""
    def __init__(self, *args, **kwargs):
        self.name = None
        self.fields = []
        return super(struct, self).__init__(*args, **kwargs)

class command(struct):
    """IDL command"""

