"""IDL Parser"""
from __future__ import absolute_import, print_function, unicode_literals

from typing import List, Union, Any

import pprint
import yaml
from yaml import nodes

from . import errors
from . import ast


def parse_global(ctxt, spec, node):
    # type: (errors.ParserContext, ast.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
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
    # type: (errors.ParserContext, ast.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
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
    # type: (errors.ParserContext, str, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> ast.Field
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
            elif first_name == "ignore":
                # TODO: add duplicate check
                #if not ctxt.is_duplicate(second_node, field.ignore, "ignore"):
                field.ignore = second_node.value
            elif first_name == "required":
                # TODO: add duplicate check
                #if not ctxt.is_duplicate(second_node, field.ignore, "ignore"):
                field.required = second_node.value
            #TODO: fix
            #else:
            #    ctxt.add_unknown_root_node_error(first_node)

    return field

def parse_fields(ctxt, spec, node):
    # type: (errors.ParserContext, ast.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> List[ast.Field]
    """Parse a fields section in a struct in the IDL file"""

    fields = []

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        # Simple Type
        if second_node.id == "scalar":
            field = ast.Field(ctxt.file_name, node.start_mark.line, node.start_mark.column)
            field.name = first_name
            field.type = second_node.value
            fields.append(field)
        else:
        # TODO: check for duplicate fields
            field = parse_field(ctxt, first_name, second_node)
            
            fields.append(field)

    return fields

def parse_struct(ctxt, spec, node):
    # type: (errors.ParserContext, ast.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
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

    ctxt = errors.ParserContext("root", errors.ParserErrorCollection())

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
