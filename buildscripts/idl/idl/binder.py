"""Transform syntax trees from the parser into well-defined AST trees."""
from __future__ import absolute_import, print_function, unicode_literals

from typing import Union

from . import ast
from . import bson
from . import errors
from . import syntax


def validate_bson_types_list(ctxt, idl_type, syntax_type):
    # type: (errors.ParserContext, Union[syntax.Type, ast.Field], unicode) -> bool
    """Validate each type for its bson serialization type is correct."""

    bson_types = idl_type.bson_serialization_type
    if len(bson_types) == 1:
        # Any is only valid if it is the only bson type specified
        if bson_types[0] == "any":
            return True

        if not bson.is_valid_bson_type(bson_types[0]):
            ctxt.add_bad_bson_type(idl_type, syntax_type, idl_type.name, bson_types[0])
            return False

        # Validate bindata_subytpe
        if bson_types[0] == "bindata":
            subtype = idl_type.bindata_subtype

            if subtype is None:
                subtype = "<unknown>"

            if not bson.is_valid_bindata_subtype(subtype):
                ctxt.add_bad_bson_bindata_subtype_value(idl_type, syntax_type, idl_type.name,
                                                        subtype)
        elif idl_type.bindata_subtype is not None:
            ctxt.add_bad_bson_bindata_subtype(idl_type, syntax_type, idl_type.name, bson_types[0])

        return True

    for bson_type in bson_types:

        if not bson.is_valid_bson_type(bson_type):
            ctxt.add_bad_bson_type(idl_type, syntax_type, idl_type.name, bson_type)
            return False

        # V1 restiction: cannot mix bindata into list of types unless someone can prove this is actually needed.
        if bson_type == "bindata":
            ctxt.add_bad_bson_type(idl_type, syntax_type, idl_type.name, bson_type)
            return False

        # Cannot mix non-scalar types into the list of types
        if not bson.is_scalar_bson_type(bson_type):
            ctxt.add_bad_bson_scalar_type(idl_type, syntax_type, idl_type.name, bson_type)
            return False

    return True


def validate_type(ctxt, idl_type):
    # type: (errors.ParserContext, syntax.Type) -> None
    """Validate each type is correct."""

    # Validate naming restrictions
    if idl_type.name.startswith("array"):
        ctxt.add_array_not_valid(idl_type, "type", idl_type.name)

    validate_type_properties(ctxt, idl_type, 'type')


def validate_type_properties(ctxt, idl_type, syntax_type):
    # type: (errors.ParserContext, Union[syntax.Type, ast.Field], unicode) -> None
    """Validate each type is correct."""

    # Validate bson type restrictions
    if not validate_bson_types_list(ctxt, idl_type, syntax_type):
        # Error exit to avoid too much nesting in if statements
        return

    if len(idl_type.bson_serialization_type) == 1:
        bson_type = idl_type.bson_serialization_type[0]
        if bson_type == "any":
            if idl_type.deserializer is None:
                ctxt.add_missing_ast_required_field(idl_type, syntax_type, idl_type.name,
                                                    "deserializer")
        elif bson_type == "object":
            if idl_type.deserializer is None:
                ctxt.add_missing_ast_required_field(idl_type, syntax_type, idl_type.name,
                                                    "deserializer")

            if idl_type.serializer is None:
                ctxt.add_missing_ast_required_field(idl_type, syntax_type, idl_type.name,
                                                    "serializer")
    else:
        # Now, this is a list of scalar types
        if idl_type.deserializer is None:
            ctxt.add_missing_ast_required_field(idl_type, syntax_type, idl_type.name,
                                                "deserializer")

    # Validate cpp_type
    # Do not allow StringData, use std::string instead.abs
    if "StringData" in idl_type.cpp_type:
        ctxt.add_no_string_data(idl_type, syntax_type, idl_type.name)


def validate_types(ctxt, parsed_spec):
    # type: (errors.ParserContext, syntax.IDLSpec) -> None
    """Validate all types are correct."""

    for idl_type in parsed_spec.symbols.types:
        validate_type(ctxt, idl_type)


def bind_struct(ctxt, parsed_spec, struct):
    # type: (errors.ParserContext, syntax.IDLSpec, syntax.Struct) -> ast.Struct
    """
    Bind a struct.
    
    - Validating a struct and fields.
    - Create the AST version from the syntax tree.
    """

    ast_struct = ast.Struct(struct.file_name, struct.line, struct.column)
    ast_struct.name = struct.name
    ast_struct.description = struct.description
    ast_struct.strict = struct.strict

    # Validate naming restrictions
    if ast_struct.name.startswith("array"):
        ctxt.add_array_not_valid(ast_struct, "struct", ast_struct.name)

    for field in struct.fields:
        ast_field = bind_field(ctxt, parsed_spec, field)
        if ast_field:
            ast_struct.fields.append(ast_field)

    return ast_struct


def validate_ignored_field(ctxt, field):
    # type: (errors.ParserContext, syntax.Field) -> None
    """Validate that for ignored fields, no other properties are set."""
    if field.optional:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "optional")
    if field.cpp_type is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "cpp_type")
    if field.bson_serialization_type is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "bson_serialization_type")
    if field.bindata_subtype is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "bindata_subtype")
    if field.serializer is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "serializer")
    if field.deserializer is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "deserializer")
    if field.default is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "default")


def validate_field_of_type_struct(ctxt, field):
    # type: (errors.ParserContext, syntax.Field) -> None
    """Validate that for fields with a type of struct, no other properties are set."""
    if field.cpp_type is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "cpp_type")
    if field.bson_serialization_type is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "bson_serialization_type")
    if field.bindata_subtype is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "bindata_subtype")
    if field.serializer is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "serializer")
    if field.deserializer is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "deserializer")
    if field.default is not None:
        ctxt.add_ignored_field_must_be_empty(field, field.name, "default")


def bind_field(ctxt, parsed_spec, field):
    # type: (errors.ParserContext, syntax.IDLSpec, syntax.Field) -> ast.Field
    """
    Bind a field from the syntax tree.

    - Create the AST version from the syntax tree.
    - Validate the resulting type is correct.
    """
    ast_field = ast.Field(field.file_name, field.line, field.column)
    ast_field.name = field.name
    ast_field.description = field.description

    # TODO validate field

    # Validate naming restrictions
    if ast_field.name.startswith("array"):
        ctxt.add_array_not_valid(ast_field, "field", ast_field.name)

    if field.ignore:
        ast_field.ignore = field.ignore
        validate_ignored_field(ctxt, field)
        return ast_field

    # TODO: support array
    (struct, idltype) = parsed_spec.symbols.resolve_field_type(ctxt, field)
    if not struct and not idltype:
        return None

    # Copy over common fields
    ast_field.optional = field.optional

    # Copy over only the needed information if this a struct or a type
    if struct:
        ast_field.struct_type = struct.name
        ast_field.bson_serialization_type = ["object"]
        validate_field_of_type_struct(ctxt, field)
    else:
        # Produce the union of type information for the type and this field.

        # Copy over the type fields first
        ast_field.cpp_type = idltype.cpp_type
        ast_field.bson_serialization_type = idltype.bson_serialization_type
        ast_field.serializer = idltype.serializer
        ast_field.deserializer = idltype.deserializer
        ast_field.bindata_subtype = idltype.bindata_subtype

        # Copy over any field fields second
        if field.cpp_type:
            ast_field.cpp_type = field.cpp_type
        if field.bson_serialization_type:
            ast_field.bson_serialization_type = field.bson_serialization_type
        if field.serializer:
            ast_field.serializer = field.serializer
        if field.deserializer:
            ast_field.deserializer = field.deserializer
        if field.bindata_subtype:
            ast_field.bindata_subtype = field.bindata_subtype

        # Validate merged type
        validate_type_properties(ctxt, ast_field, "field")

    return ast_field


def bind_globals(ctxt, parsed_spec):
    # type: (errors.ParserContext, syntax.IDLSpec) -> ast.Global
    """Bind the globals object from the syntax tree into the ast tree by doing a deep copy."""
    if parsed_spec.globals:
        ast_global = ast.Global(parsed_spec.globals.file_name, parsed_spec.globals.line,
                                parsed_spec.globals.column)
        ast_global.cpp_namespace = parsed_spec.globals.cpp_namespace
        ast_global.cpp_includes = parsed_spec.globals.cpp_includes
    else:
        ast_global = ast.Global("<implicit>", 0, 0)

        # If no namespace has been set, default it do "mongo"
        ast_global.cpp_namespace = "mongo"

    return ast_global


def bind(parsed_spec):
    # type: (syntax.IDLSpec) -> ast.IDLBoundSpec
    """Bind and validate a IDL Specification."""

    ctxt = errors.ParserContext("unknown", errors.ParserErrorCollection())

    bound_spec = ast.IDLAST()

    bound_spec.globals = bind_globals(ctxt, parsed_spec)

    validate_types(ctxt, parsed_spec)

    for struct in parsed_spec.symbols.structs:
        bound_spec.structs.append(bind_struct(ctxt, parsed_spec, struct))

    if ctxt.errors.has_errors():
        return ast.IDLBoundSpec(None, ctxt.errors)
    else:
        return ast.IDLBoundSpec(bound_spec, None)
