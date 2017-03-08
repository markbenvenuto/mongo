"""
IDL Parser.

Converts a YAML document to a syntax tree.abs
Only validates the document is syntatically correct, not semantically.
"""
from __future__ import absolute_import, print_function, unicode_literals

from typing import List, Union, Any
from yaml import nodes
import yaml

from . import errors
from . import syntax


def parse_global(ctxt, spec, node):
    # type: (errors.ParserContext, syntax.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse a global section in the IDL file."""
    if not ctxt.is_mapping_node(node, "global"):
        return

    idlglobal = syntax.Global(ctxt.file_name, node.start_mark.line, node.start_mark.column)

    field_name_set = set()  # type: Set[str]

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name in field_name_set:
            ctxt.add_duplicate(first_node, first_name)
            continue

        if first_name == "cpp_namespace":
            if ctxt.is_scalar_node(second_node, "cpp_namespace"):
                # TODO: validate C++ namespace
                idlglobal.cpp_namespace = second_node.value
        elif first_name == "cpp_includes":
            if ctxt.is_sequence_or_scalar_node(second_node, "cpp_includes"):
                idlglobal.cpp_includes = ctxt.get_list(second_node)
        else:
            ctxt.add_unknown_node_error(first_node, "global")

        field_name_set.add(first_name)

    if spec.globals:
        ctxt.add_duplicate(node, "global")
        return

    spec.globals = idlglobal


def parse_type(ctxt, spec, node):
    # type: (errors.ParserContext, syntax.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse a type section in the IDL file."""
    if not ctxt.is_mapping_node(node, "type"):
        return

    idltype = syntax.Type(ctxt.file_name, node.start_mark.line, node.start_mark.column)

    field_name_set = set()  # type: Set[str]

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name in field_name_set:
            ctxt.add_duplicate(first_node, first_name)
            continue

        if first_name == "name":
            if ctxt.is_scalar_node(second_node, "name"):
                idltype.name = second_node.value
        elif first_name == "description":
            if ctxt.is_scalar_node(second_node, "description"):
                idltype.description = second_node.value
        elif first_name == "cpp_type":
            if ctxt.is_scalar_node(second_node, "cpp_type"):
                idltype.cpp_type = second_node.value
        elif first_name == "bson_serialization_type":
            if ctxt.is_sequence_or_scalar_node(second_node, "bson_serialization_type"):
                idltype.bson_serialization_type = ctxt.get_list(second_node)
        elif first_name == "serializer":
            if ctxt.is_scalar_node(second_node, "serializer"):
                idltype.serializer = second_node.value
        elif first_name == "deserializer":
            if ctxt.is_scalar_node(second_node, "deserializer"):
                idltype.deserializer = second_node.value
        elif first_name == "bindata_subtype":
            if ctxt.is_scalar_node(second_node, "bindata_subtype"):
                idltype.bindata_subtype = second_node.value
        elif first_name == "default":
            if ctxt.is_scalar_node(second_node, "default"):
                idltype.default = second_node.value
        else:
            ctxt.add_unknown_node_error(first_node, "type")

        field_name_set.add(first_name)

    if idltype.name is None:
        ctxt.add_missing_required_field(node, "type", "name")

    if idltype.cpp_type is None:
        ctxt.add_missing_required_field(node, "type", "cpp_type")
    
    if idltype.description is None:
        ctxt.add_missing_required_field(node, "type", "description")

    if idltype.bson_serialization_type is None:
        ctxt.add_missing_required_field(node, "type", "bson_serialization_type")

    spec.symbols.add_type(ctxt, idltype)


def parse_field(ctxt, name, node):
    # type: (errors.ParserContext, str, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> syntax.Field
    """Parse a field in a struct/command in the IDL file."""
    field = syntax.Field(ctxt.file_name, node.start_mark.line, node.start_mark.column)

    field_name_set = set()  # type: Set[str]
    field.name = name
    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name in field_name_set:
            ctxt.add_duplicate(first_node, first_name)
            continue

        if first_name == "bson_serialization_type":
            if ctxt.is_sequence_or_scalar_node(second_node, "bson_serialization_type"):
                field.bson_serialization_type = ctxt.get_list(second_node)
        elif ctxt.is_scalar_node(second_node, name):
            if first_name == "type":
                field.type = second_node.value
            elif first_name == "ignore":
                if ctxt.is_scalar_bool_node(second_node, "ignore"):
                    field.ignore = second_node.value
            elif first_name == "optional":
                if ctxt.is_scalar_bool_node(second_node, "optional"):
                    field.optional = second_node.value
            elif first_name == "description":
                field.description = second_node.value
            elif first_name == "cpp_type":
                field.cpp_type = second_node.value
            elif first_name == "serializer":
                field.serializer = second_node.value
            elif first_name == "deserializer":
                field.deserializer = second_node.value
            elif first_name == "bindata_subtype":
                field.bindata_subtype = second_node.value
            elif first_name == "default":
                field.default = second_node.value
            else:
                ctxt.add_unknown_node_error(first_node, "field")

        field_name_set.add(first_name)

    return field


def parse_fields(ctxt, spec, node):
    # type: (errors.ParserContext, syntax.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> List[syntax.Field]
    """Parse a fields section in a struct in the IDL file."""

    fields = []

    field_name_set = set()  # type: Set[str]

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name in field_name_set:
            ctxt.add_duplicate(first_node, first_name)
            continue

        # Simple Type
        if second_node.id == "scalar":
            field = syntax.Field(ctxt.file_name, node.start_mark.line, node.start_mark.column)
            field.name = first_name
            field.type = second_node.value
            fields.append(field)
        else:
            field = parse_field(ctxt, first_name, second_node)

            fields.append(field)

        field_name_set.add(first_name)

    return fields


def parse_struct(ctxt, spec, node):
    # type: (errors.ParserContext, syntax.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse a struct section in the IDL file."""
    if not ctxt.is_mapping_node(node, "struct"):
        return

    struct = syntax.Struct(ctxt.file_name, node.start_mark.line, node.start_mark.column)
    field_name_set = set()  # type: Set[str]

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name in field_name_set:
            ctxt.add_duplicate(first_node, first_name)
            continue

        if first_name == "name":
            if ctxt.is_scalar_node(second_node, "name"):
                struct.name = second_node.value
        elif first_name == "description":
            if ctxt.is_scalar_node(second_node, "description"):
                struct.description = second_node.value
        elif first_name == "strict":
            if ctxt.is_scalar_bool_node(second_node, "strict"):
                struct.strict = second_node.value
        elif first_name == "fields":
            if ctxt.is_mapping_node(second_node, "fields"):
                struct.fields = parse_fields(ctxt, spec, second_node)
        else:
            ctxt.add_unknown_node_error(first_node, "struct")

        field_name_set.add(first_name)

    if struct.name is None:
        ctxt.add_missing_required_field(node, "struct", "name")
    elif len(struct.fields) == 0:
        ctxt.add_empty_struct(node, struct.name)

    if struct.description is None:
        ctxt.add_missing_required_field(node, "struct", "description")

    spec.symbols.add_struct(ctxt, struct)


def parse(stream, error_file_name="unknown"):
    # type: (Any, unicode) -> syntax.IDLParsedSpec
    """
    Parse a YAML document into an AST.
    
    error_file_name just a file name for error messages to use.
    """

    # This will throw if the YAML parse fails
    root_node = yaml.compose(stream)

    ctxt = errors.ParserContext(error_file_name, errors.ParserErrorCollection())

    spec = syntax.IDLSpec()

    # If the document is empty, we are done
    if not root_node:
        return syntax.IDLParsedSpec(spec, None)

    #print(dir(nodes))
    #print(nodes.__class__)
    #if not isinstance(nodes.value, yaml.nodes.MappingNode):
    if not root_node.id == "mapping":
        raise errors.IDLError("Did not expected mapping node as root node of IDL document")

    for node_pair in root_node.value:
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

    if ctxt.errors.has_errors():
        return syntax.IDLParsedSpec(None, ctxt.errors)
    else:
        return syntax.IDLParsedSpec(spec, None)
