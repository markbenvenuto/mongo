import sys

import SCons
import os.path

def idlc_emitter(target, source, env):
    
    s1 = str(source[0])

    if not s1.endswith(".idl"):
        raise ValueError("Bad idl file name '%s', it must end with '.idl' " % s1)
    
    base_file_name, ext = SCons.Util.splitext(str(target[0]))
    target_source = base_file_name + "_gen.cc"
    target_header = base_file_name + "_gen.hpp"

    return [target_source, target_header], source


IDLCAction = SCons.Action.Action('$IDLCCOM', '$IDLCCOMSTR')

# TODO - create a scanner for imports when imports are implemented
IDLCBuilder = SCons.Builder.Builder(
    action = IDLCAction,
    emitter = idlc_emitter,
    srcsuffx=".idl.yml",
    suffix=".cc"
    )
 
def generate(env):

    bld = IDLCBuilder
    env['BUILDERS']['Idlc'] = bld
    
    env['IDLC'] = sys.executable + " buildscripts/idl/idlc.py" 
    env['IDLCFLAGS'] = ''
    env['IDLCCOM'] = '$IDLC --header ${TARGETS[1]} --output ${TARGETS[0]} $SOURCES '
    env['IDLCSUFFIX'] = '.idl'

def exists(env):
    return True