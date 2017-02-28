from __future__ import absolute_import, print_function, unicode_literals

from . import parser
from . import binder
from . import generator

class compileArgs(object):
    """

    - Include Path
    - Input Document
    - Output Document
    - OUput 
    """

def compile(stream):
    parsed_doc = parser.parse(stream)

    if not parsed_doc.errors:
        bound_doc = binder.bind(parsed_doc.spec)
        if not bound_doc.errors:
            generator.generate_code(bound_doc.spec, "example_gen")
        else:
            bound_doc.errors.dump_errors()
    else:
        parsed_doc.errors.dump_errors()


    # TODO: bind and validate the tree

    #spec.dump()

    # Dump code for all the generated files
    # 1. Generate Header file
    # 2. Generate C++ file stuff    

    # Create series of classes to generate code for each type
