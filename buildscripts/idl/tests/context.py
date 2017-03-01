import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

import idl.parser
import idl.syntax
import idl.binder
import idl.ast
import idl.generator
import idl.errors
