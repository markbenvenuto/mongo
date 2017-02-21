import yaml
from . import errors
from . import ast

import pprint

class ParserErrors(object):
    def __init__(self):
        self._errors = []

    def add(self, file, node, msg):
        line = node.start_mark.line
        column = node.start_mark.column
        str = "%s: (%d, %d): %s" % (file, line, column, msg)
        self._errors.append(str)

    def hasErrors(self):
        return len(self._errors) > 0

    def dumpErrors(self):
        for error in self._errors:
            print ("%s\n\n" % error)

class ParserContext(object):
    def __init__(self, file, *args, **kwargs):
        self._errors = ParserErrors()
        self._file = file
        return super(ParserContext, self).__init__(*args, **kwargs)

    def addUnknownRootNodeError(self, node):
        self._errors.add(self._file, node, "Unrecognized IDL specification root level node '%s' only (global, import, type, command, and struct) are accepted" % (node.value))

    def addDuplicateSymbolError(self, node, name, duplicate_class_name, original_class_name):
        self._errors.add(self._file, node, "%s '%s' is a duplicate of an existing %s" % (duplicate_class_name, name, original_class_name))

#    def addNameCollision(self, node, type):
 #       self._errors.add(file, node, "Unrecognized root level node '%s' only (global, import, type, command, and struct) are accepted" % (node.value))

    def _isNodeType(self, node, node_name, expected_node_type):
        if not node.id == expected_node_type:
            self._errors.add(self._file, node, "Illegal node type '%s' for '%s', expected node type '%s'" % (node.id, node_name, expected_node_type))
            return False
        return True

    def isMappingNode(self, node, node_name):
        return self._isNodeType(node, node_name, "mapping")

    def isScalarNode(self, node, node_name):
        return self._isNodeType(node, node_name, "scalar")

    def isSequenceNode(self, node, node_name):
        return self._isNodeType(node, node_name, "sequence")

    def isSequenceOrScalarNode(self, node, node_name):
        if not node.id == "scalar" and not node.id == "sequence" :
            self._errors.add(self._file, node, "Illegal node type '%s' for '%s', expected node type 'scalar' or 'sequence'" % (node.id, node_name, expected_node_type))
            return False
        return True

    def getList(self, node):
        assert self.isSequenceOrScalarNode(node, "unknown")
        if node.id == "scalar":
            return [node.value]
        elif node.id == "sequence":
            # Unzip the list of ScalarNode
            return [v.value for v in node.value]

    def isDuplicate(self, node, value, node_name):
        if value is not None:
            self._errors.add(self._file, node, "Duplicate node found for '%s'" % (node_name))
            return True

        return False

    def isEmptyList(self, node, value, node_name):
        if len(value) == 0:
            return True

        self._errors.add(self._file, node, "Duplicate node found for '%s'" % (node_name))
        return False

    def hasErrors(self):
        return self._errors.hasErrors()

    def dumpErrors(self):
        self._errors.dumpErrors()

def parse_global(ctxt, spec, node):
    if not ctxt.isMappingNode(node, "global"):
        return

    idlglobal = ast.idl_global()

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value
        
        if first_name == "cpp_namespace":
            if (not ctxt.isDuplicate(second_node, idlglobal.cpp_namespace, "cpp_namespace")) and \
                ctxt.isScalarNode(second_node, "cpp_namespace"):
                # TODO: validate namespace
                idlglobal.cpp_namespace = second_node.value
        elif first_name == "cpp_includes":
            if (not ctxt.isEmptyList(second_node, idlglobal.cpp_includes, "cpp_includes")) and \
                ctxt.isSequenceOrScalarNode(second_node, "cpp_includes"):
                idlglobal.cpp_namespace = ctxt.getList(second_node)
        else:
            ctxt.addUnknownRootNodeError(first_node)

    if ctxt.isDuplicate(node, spec.globals, "global"):
        return

    spec.globals = idlglobal
   
def parse_type(ctxt, spec, node):

    pass

def parse_struct(ctxt, spec, node):
    if not ctxt.isMappingNode(node, "struct"):
        return

    struct = ast.struct()

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value
        
        if first_name == "name":
            if (not ctxt.isDuplicate(second_node, struct.name, "name")) and \
                ctxt.isScalarNode(second_node, "name"):
                # TODO: validate name
                struct.name = second_node.value
        else:
            ctxt.addUnknownRootNodeError(first_node)

    spec.symbols.addStruct(ctxt, node, struct)

def parse(stream):
    """Parse a YAML document into an AST"""

    # This may throw
    nodes = yaml.compose(stream)

    ctxt = ParserContext("root")

    #print(dir(nodes))
    #print(nodes.__class__)
    #if not isinstance(nodes.value, yaml.nodes.MappingNode):
    if not nodes.id == "mapping":
        raise errors.IDLError("Did not expected mapping node as root node of IDL document")

    spec = ast.IDLSpec()

    for node_pair in nodes.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name == "global":
            parse_global(ctxt, spec, second_node)
        elif first_name == "type":
            parse_type(ctxt, spec, second_node)
        elif first_name == "struct":
            parse_struct(ctxt, spec, second_node)
        else:
            ctxt.addUnknownRootNodeError(first_node)

    if ctxt.hasErrors():
        ctxt.dumpErrors()
        return None
    else:
        return spec

