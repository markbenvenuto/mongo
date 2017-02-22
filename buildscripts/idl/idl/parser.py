"""IDL Parser"""
from __future__ import absolute_import, print_function, unicode_literals

from typing import List, Union, Any

import pprint
import yaml
from yaml import nodes

from . import errors
from . import ast

class ParserContext(object):
    """IDL parser context

        Responsible for:
        - keeping track of current file
        - generating error messages
    """
    def __init__(self, file_name, errors, *args, **kwargs):
        # type: (unicode, ast.ParserErrorCollection, *str, **bool) -> None
        self.errors = errors
        self.file_name = file_name
        #super(ParserContext, self).__init__(*args, **kwargs)

    def _add_error(self, location, id, msg):
        # type: (ast.SourceLocation, unicode, unicode) -> None
        self.errors.add(location, id, msg)

    def _add_node_error(self, node, id, msg):
        # type: (yaml.nodes.Node, unicode, unicode) -> None
        self.errors.add(ast.SourceLocation(self.file_name, node.start_mark.line, node.start_mark.column), id, msg)

    def add_unknown_root_node_error(self, node):
        # type: (yaml.nodes.Node) -> None
        """Add an error about an unknown root node"""
        self._add_node_error(node, errors.ERROR_ID_UNKNOWN_ROOT, "Unrecognized IDL specification root level node '%s' only (global, import, type, command, and struct) are accepted" % (node.value))

    def add_duplicate_symbol_error(self, location, name, duplicate_class_name, original_class_name):
        # type: (ast.SourceLocation, unicode, unicode, unicode) -> None
        """Add an error about a duplicate symbol"""
        self._add_error(location, errors.ERROR_ID_DUPLICATE_SYMBOL, "%s '%s' is a duplicate symbol of an existing %s" % (duplicate_class_name, name, original_class_name))

    def _is_node_type(self, node, node_name, expected_node_type):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode, unicode) -> bool
        if not node.id == expected_node_type:
            self._add_node_error(node, errors.ERROR_ID_IS_NODE_TYPE, "Illegal node type '%s' for '%s', expected node type '%s'" % (node.id, node_name, expected_node_type))
            return False
        return True

    def is_mapping_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Is this YAML node a Map?"""
        return self._is_node_type(node, node_name, "mapping")

    def is_scalar_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Is this YAML node a Scalar?"""
        return self._is_node_type(node, node_name, "scalar")

    def is_sequence_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Is this YAML node a Sequence?"""
        return self._is_node_type(node, node_name, "sequence")

    def is_sequence_or_scalar_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Is this YAML node a Scalar or Sequence?"""
        if not node.id == "scalar" and not node.id == "sequence":
            self._add_node_error(node, errors.ERROR_ID_IS_NODE_TYPE_SCALAR_OR_SEQUENCE, "Illegal node type '%s' for '%s', expected node type 'scalar' or 'sequence'" % (node.id, node_name))
            return False
        return True

    def get_list(self, node):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> List[unicode]
        """Get a YAML scalar or sequence node as a list of strings"""
        assert self.is_sequence_or_scalar_node(node, "unknown")
        if node.id == "scalar":
            return [node.value]
        elif node.id == "sequence":
            # Unzip the list of ScalarNode
            return [v.value for v in node.value]

    def is_duplicate(self, node, value, node_name):
        # type: (yaml.nodes.Node, Any, unicode) -> bool
        """Is this a duplicate node, check to see if value is not None"""
        if value is not None:
            self._add_node_error(node, errors.ERROR_ID_DUPLICATE_NODE, "Duplicate node found for '%s'" % (node_name))
            return True

        return False

    def is_empty_list(self, node, value, node_name):
        # type: (yaml.nodes.Node, List, unicode) -> bool
        """Is this a duplicate node, check to see if value is not an empty list"""
        if len(value) == 0:
            return True

        self._add_node_error(node, errors.ERROR_ID_DUPLICATE_NODE, "Duplicate node found for '%s'" % (node_name))
        return False

def parse_global(ctxt, spec, node):
    # type: (ParserContext, ast.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse a global section in the IDL file"""
    if not ctxt.is_mapping_node(node, "global"):
        return

    idlglobal = ast.Global(ctxt.file_name, node.start_mark.line, node.start_mark.column)

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
            if (ctxt.is_empty_list(second_node, idlglobal.cpp_includes, "cpp_includes")) and \
                ctxt.is_sequence_or_scalar_node(second_node, "cpp_includes"):
                idlglobal.cpp_includes = ctxt.get_list(second_node)
        else:
            ctxt.add_unknown_root_node_error(first_node)

    if ctxt.is_duplicate(node, spec.globals, "global"):
        return

    spec.globals = idlglobal

def parse_type(ctxt, spec, node):
    # type: (ParserContext, ast.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse a type section in the IDL file"""
    if not ctxt.is_mapping_node(node, "type"):
        return

    idltype = ast.Type(ctxt.file_name, node.start_mark.line, node.start_mark.column)

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name == "name":
            if (not ctxt.is_duplicate(second_node, idltype.name, "name")) and \
                ctxt.is_scalar_node(second_node, "name"):
                idltype.name = second_node.value
        elif first_name == "cpp_type":
            if (not ctxt.is_duplicate(second_node, idltype.cpp_type, "cpp_type")) and \
                ctxt.is_scalar_node(second_node, "cpp_type"):
                idltype.cpp_type = second_node.value
        elif first_name == "bson_serialization_type":
            if (not ctxt.is_duplicate(second_node, idltype.bson_serialization_type, "bson_serialization_type")) and \
                ctxt.is_scalar_node(second_node, "bson_serialization_type"):
                idltype.bson_serialization_type = second_node.value
        elif first_name == "serializer":
            if (not ctxt.is_duplicate(second_node, idltype.serializer, "serializer")) and \
                ctxt.is_scalar_node(second_node, "serializer"):
                idltype.serializer = second_node.value
        elif first_name == "deserializer":
            if (not ctxt.is_duplicate(second_node, idltype.deserializer, "deserializer")) and \
                ctxt.is_scalar_node(second_node, "deserializer"):
                idltype.deserializer = second_node.value
        #TODO: fix
        #else:
        #    ctxt.add_unknown_root_node_error(first_node)

    spec.symbols.add_type(ctxt, idltype)

def parse_field(ctxt, name, node):
    # type: (ParserContext, str, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> ast.Field
    """Parse a field in a struct/command in the IDL file"""
    field = ast.Field(ctxt.file_name, node.start_mark.line, node.start_mark.column)

    field.name = name
    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if ctxt.is_scalar_node(second_node, name):
            if first_name == "type":
                if not ctxt.is_duplicate(second_node, field.type, "type"):
                    field.type = second_node.value
            #TODO: fix
            #else:
            #    ctxt.add_unknown_root_node_error(first_node)

    return field

def parse_fields(ctxt, spec, node):
    # type: (ParserContext, ast.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> List[ast.Field]
    """Parse a fields section in a struct in the IDL file"""

    fields = []

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        # Simple Type
        #ctxt.is_scalar_node(second_node, "name"):
        if second_node.id == "scalar":
            pass
        else:
        # TODO: check for duplicate fields
            field = parse_field(ctxt, first_name, second_node)
            
            fields.append(field)

    return fields

def parse_struct(ctxt, spec, node):
    # type: (ParserContext, ast.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse a struct section in the IDL file"""
    if not ctxt.is_mapping_node(node, "struct"):
        return

    struct = ast.Struct(ctxt.file_name, node.start_mark.line, node.start_mark.column)

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name == "name":
            if (not ctxt.is_duplicate(second_node, struct.name, "name")) and \
                ctxt.is_scalar_node(second_node, "name"):
                # TODO: validate name
                struct.name = second_node.value
        elif first_name == "fields":
            if (ctxt.is_empty_list(second_node, struct.fields, "fields")) and \
                ctxt.is_mapping_node(second_node, "fields"):
                struct.fields = parse_fields(ctxt, spec, second_node)
        #TODO: fix
        #else:
        #    ctxt.add_unknown_root_node_error(first_node)

    spec.symbols.add_struct(ctxt, struct)

def parse(stream):
    """Parse a YAML document into an AST"""

    # This may throw
    nodes = yaml.compose(stream)

    ctxt = ParserContext("root", ast.ParserErrorCollection())

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


    #pp = pprint.PrettyPrinter()

    #pp.pprint(spec)

    if ctxt.errors.has_errors():
        ctxt.errors.dump_errors()
        return ast.IDLParsedSpec(None, ctxt.errors)
    else:
        return ast.IDLParsedSpec(spec, None)
