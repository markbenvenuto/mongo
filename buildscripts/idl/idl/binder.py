"""Binder"""
from __future__ import absolute_import, print_function, unicode_literals

from . import ast
from . import errors

# TODO: fix up to be mongo as default namespace

def bind_field(ctxt, spec, field):
    # type: (errors.ParserContext, ast.IDLSpec, ast.Field) -> None
    
    (struct, idltype) = spec.symbols.resolve_field_type(ctxt, field)
    if not struct and not idltype:
        return
    
    if struct:
        field.struct = struct
    else:
        # TODO: merge types
        field.cpp_type = idltype.cpp_type
        field.bson_serialization_type = idltype.bson_serialization_type
        field.serializer = idltype.serializer
        field.deserializer = idltype.deserializer

def bind(spec):
    # type: (ast.IDLSpec) -> ast.IDLBoundSpec
    """Bind and validate a IDL Specification
       The IDL specification is modified in-place
    """

    ctxt = errors.ParserContext("unknown", errors.ParserErrorCollection())

    for struct in spec.symbols.structs:
        for field in struct.fields:
            if not field.ignore:
                bind_field(ctxt, spec, field)

    if ctxt.errors.has_errors():
        ctxt.errors.dump_errors()
        return ast.IDLBoundSpec(None, ctxt.errors)
    else:
        return ast.IDLBoundSpec(spec, None)

