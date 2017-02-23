from __future__ import absolute_import, print_function, unicode_literals

from . import parser


def compile(stream):

    spec = parser.parse(stream)

    # TODO: bind and validate the tree

    #spec.dump()

    # Dump code for all the generated files
    # 1. Generate Header file
    # 2. Generate C++ file stuff    

    # Create series of classes to generate code for each type
