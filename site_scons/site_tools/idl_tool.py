#!/usr/bin/env python2
# Copyright (C) 2017 MongoDB Inc.
#
# This program is free software: you can redistribute it and/or  modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
"""IDL Compiler Scons Tool."""

import os.path
import sys

import SCons

def idlc_emitter(target, source, env):
    """For each input IDL file, the tool produces a .cpp and .h file."""
    first_source = str(source[0])

    if not first_source.endswith(".idl"):
        raise ValueError("Bad idl file name '%s', it must end with '.idl' " % (first_source))

    base_file_name, _ = SCons.Util.splitext(str(target[0]))
    target_source = base_file_name + "_gen.cpp"
    target_header = base_file_name + "_gen.h"

    return [target_source, target_header], source


IDLCAction = SCons.Action.Action('$IDLCCOM', '$IDLCCOMSTR')

# TODO: create a scanner for imports when imports are implemented
IDLCBuilder = SCons.Builder.Builder(
    action=IDLCAction,
    emitter=idlc_emitter,
    srcsuffx=".idl",
    suffix=".cpp"
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