"""IDL Parser"""
from __future__ import absolute_import, print_function

import yaml
from . import errors
from . import ast

class ParserErrorCollection(object):
    """A collection of errors with line & context information"""
    def __init__(self):
        self._errors = []

    def add(self, file_name, node, msg):
        """Add an error message with file (line, column) information"""
        line = node.start_mark.line
        column = node.start_mark.column
        error_msg = "%s: (%d, %d): %s" % (file_name, line, column, msg)
        self._errors.append(error_msg)

    def has_errors(self):
        """Have any errors been added to the collection?"""
        return len(self._errors) > 0

    def dump_errors(self):
        """Print the list of errors"""
        for error in self._errors:
            print ("%s\n\n" % error)

class ParserContext(object):
    """IDL parser context

        Responsible for:
        - keeping track of current file
        - generating error messages
    """
    def __init__(self, file_name, *args, **kwargs):
        self._errors = ParserErrorCollection()
        self._file = file_name
        super(ParserContext, self).__init__(*args, **kwargs)

    def _add_error(self, node, msg):
        self._errors.add(self._file, node, msg)

    def add_unknown_root_node_error(self, node):
        """Add an error about an unknown root node"""
        self._add_error(node, "Unrecognized IDL specification root level node '%s' only (global, import, type, command, and struct) are accepted" % (node.value))

    def add_duplicate_symbol_error(self, node, name, duplicate_class_name, original_class_name):
        """Add an error about a duplicate symbol"""
        self._add_error(node, "%s '%s' is a duplicate symbol of an existing %s" % (duplicate_class_name, name, original_class_name))

    def _is_node_type(self, node, node_name, expected_node_type):
        if not node.id == expected_node_type:
            self._add_error(node, "Illegal node type '%s' for '%s', expected node type '%s'" % (node.id, node_name, expected_node_type))
            return False
        return True

    def is_mapping_node(self, node, node_name):
        """Is this YAML node a Map?"""
        return self._is_node_type(node, node_name, "mapping")

    def is_scalar_node(self, node, node_name):
        """Is this YAML node a Scalar?"""
        return self._is_node_type(node, node_name, "scalar")

    def is_sequence_node(self, node, node_name):
        """Is this YAML node a Sequence?"""
        return self._is_node_type(node, node_name, "sequence")

    def is_sequence_or_scalar_node(self, node, node_name):
        """Is this YAML node a Scalar or Sequence?"""
        if not node.id == "scalar" and not node.id == "sequence":
            self._add_error(node, "Illegal node type '%s' for '%s', expected node type 'scalar' or 'sequence'" % (node.id, node_name))
            return False
        return True

    def get_list(self, node):
        """Get a YAML scalar or sequence node as a list of strings"""
        assert self.is_sequence_or_scalar_node(node, "unknown")
        if node.id == "scalar":
            return [node.value]
        elif node.id == "sequence":
            # Unzip the list of ScalarNode
            return [v.value for v in node.value]

    def is_duplicate(self, node, value, node_name):
        """Is this a duplicate node, check to see if value is not None"""
        if value is not None:
            self._add_error(node, "Duplicate node found for '%s'" % (node_name))
            return True

        return False

    def is_empty_list(self, node, value, node_name):
        """Is this a duplicate node, check to see if value is not an empty list"""
        if len(value) == 0:
            return True

        self._add_error(node, "Duplicate node found for '%s'" % (node_name))
        return False

    def has_errors(self):
        """Have any errors been added to the collection?"""
        return self._errors.has_errors()

    def dump_errors(self):
        """Print the list of errors"""
        self._errors.dump_errors()

def parse_global(ctxt, spec, node):
    """Parse a global section in the IDL file"""
    if not ctxt.is_mapping_node(node, "global"):
        return

    idlglobal = ast.Global()

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name == "cpp_namespace":
            if (not ctxt.is_duplicate(second_node, idlglobal.cpp_namespace, "cpp_namespace")) and \
                ctxt.is_scalar_node(second_node, "cpp_namespace"):
                # TODO: validate namespace
                idlglobal.cpp_namespace = second_node.value
        elif first_name == "cpp_includes":
            if (not ctxt.is_empty_list(second_node, idlglobal.cpp_includes, "cpp_includes")) and \
                ctxt.is_sequence_or_scalar_node(second_node, "cpp_includes"):
                idlglobal.cpp_namespace = ctxt.get_list(second_node)
        else:
            ctxt.add_unknown_root_node_error(first_node)

    if ctxt.is_duplicate(node, spec.globals, "global"):
        return

    spec.globals = idlglobal

def parse_type(ctxt, spec, node):
    """Parse a type section in the IDL file"""
    pass

def parse_struct(ctxt, spec, node):
    """Parse a struct section in the IDL file"""
    if not ctxt.is_mapping_node(node, "struct"):
        return

    struct = ast.Struct()

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name == "name":
            if (not ctxt.is_duplicate(second_node, struct.name, "name")) and \
                ctxt.is_scalar_node(second_node, "name"):
                # TODO: validate name
                struct.name = second_node.value
        else:
            ctxt.add_unknown_root_node_error(first_node)

    spec.symbols.add_struct(ctxt, node, struct)

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
            ctxt.add_unknown_root_node_error(first_node)

    if ctxt.has_errors():
        ctxt.dump_errors()
        return None
    else:
        return spec
