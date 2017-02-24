"""Binder"""
from __future__ import absolute_import, print_function, unicode_literals

from . import ast
from . import syntax
from . import errors

# TODO: fix up to be mongo as default namespace

# TODO: ban StringData as a type

def bind_struct(ctxt, parsed_spec, struct):
    # type: (errors.ParserContext, syntax.IDLSpec, syntax.Struct) -> ast.Struct

    ast_struct = ast.Struct(struct.file_name, struct.line, struct.column)
    ast_struct.name = struct.name

    # TODO: validate struct

    for field in struct.fields:
        ast_field = bind_field(ctxt, parsed_spec, field)
        if ast_field:
            ast_struct.fields.append(ast_field)

    return ast_struct


def bind_field(ctxt, parsed_spec, field):
    # type: (errors.ParserContext, syntax.IDLSpec, syntax.Field) -> ast.Field

    ast_field = ast.Field(field.file_name, field.line, field.column)
    ast_field.name = field.name

    # TODO validate field

    if field.ignore:
        #  TODO: validate ignored field
        ast_field.ignore = field.ignore
        return ast_field

    (struct, idltype) = parsed_spec.symbols.resolve_field_type(ctxt, field)
    if not struct and not idltype:
        return None

    # Copy over common fields
    ast_field.required = field.required

    # Copy over only the needed information if this a struct or a type
    if struct:
        ast_field.struct_type = struct.name
    else:
        # TODO: merge types
        ast_field.cpp_type = idltype.cpp_type
        ast_field.bson_serialization_type = idltype.bson_serialization_type
        ast_field.serializer = idltype.serializer
        ast_field.deserializer = idltype.deserializer

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
        ast_global = ast.Global("None", 0, 0)

    return ast_global


def bind(parsed_spec):
    # type: (syntax.IDLSpec) -> ast.IDLBoundSpec
    """Bind and validate a IDL Specification."""

    ctxt = errors.ParserContext("unknown", errors.ParserErrorCollection())

    bound_spec = ast.IDLAST()

    # TODO: Validate all the types here...

    bound_spec.globals = bind_globals(ctxt, parsed_spec)

    for struct in parsed_spec.symbols.structs:
        bound_spec.structs.append(bind_struct(ctxt, parsed_spec, struct))

    if ctxt.errors.has_errors():
        ctxt.errors.dump_errors()
        return ast.IDLBoundSpec(None, ctxt.errors)
    else:
        return ast.IDLBoundSpec(bound_spec, None)
