"""Binder"""
from __future__ import absolute_import, print_function, unicode_literals

from . import ast

# TODO: fix up to be mongo as default namespace

def bind(spec):
    # type: (ast.IDLSpec) -> ast.IDLBoundSpec
    """Bind and validate a IDL Specification
       The IDL specification is modified in-place
    """

    for struct in spec.symbols.structs:
        for field in struct.fields:
            bind_field(spec, field)

    if ctxt.errors.has_errors():
        ctxt.errors.dump_errors()
        return ast.IDLParsedSpec(None, ctxt.errors)
    else:
        return ast.IDLParsedSpec(spec, None)

