"""LLDB Pretty-printers for MongoDB.

To import script in lldb, run:

   command script import buildscripts/lldb/lldb_printers.py

This file must maintain Python 2 and 3 compatibility until Apple
upgrades to Python 3 and updates their LLDB to use it.
"""
from __future__ import print_function

import string
import struct
import sys
import uuid

import lldb

try:
    import bson
    import collections
    from bson import json_util
    from bson.codec_options import CodecOptions
except ImportError:
    print("Warning: Could not load bson library for Python {}.".format(sys.version))
    print("Check with the pip command if pymongo 3.x is installed.")
    bson = None


def __lldb_init_module(debugger, *_args):
    """Register pretty printers."""
    debugger.HandleCommand(
        "type summary add -s 'A${*var.__ptr_.__value_}' -x '^std::__1::unique_ptr<.+>$'")
    debugger.HandleCommand("type summary add mongo::BSONObj -F lldb_printers.BSONObjPrinter")
    debugger.HandleCommand("type summary add mongo::Status -F lldb_printers.StatusPrinter")
    debugger.HandleCommand("type summary add mongo::StatusWith -F lldb_printers.StatusWithPrinter")
    debugger.HandleCommand("type summary add mongo::StringData -F lldb_printers.StringDataPrinter")
    debugger.HandleCommand("type summary add mongo::UUID -F lldb_printers.UUIDPrinter")
    #debugger.HandleCommand("type summary add -x '^std::__cxx11::basic_string<.+>$'  -F lldb_printers.String1Printer")
    debugger.HandleCommand(
        "type summary add --summary-string '${var.m_pathname}' 'boost::filesystem::path'")
    debugger.HandleCommand(
        "type synthetic add -x '^boost::optional<.+>$' --python-class lldb_printers.OptionalPrinter"
    )
    debugger.HandleCommand(
        "type synthetic add -x '^std::unique_ptr<.+>$' --python-class lldb_printers.UniquePtrPrinter"
    )


    debugger.HandleCommand(
        "type synthetic add -x '^absl::container_internal::flat_hash_set<.+>$' --python-class lldb_printers.AbslHashSetPrinter"
    )
    debugger.HandleCommand(
        "type synthetic add -x '^absl::container_internal::raw_hash_map<.+>$' --python-class lldb_printers.AbslHashMapPrinter"
    )
    debugger.HandleCommand(
        "type synthetic add -x '^absl::container_internal::raw_hash_set<.*NodeHashSetPolicy.+>$' --python-class lldb_printers.AbslHashSetPrinter"
    )
    debugger.HandleCommand(
        "type synthetic add -x '^absl::container_internal::raw_hash_set<.+>$' --python-class lldb_printers.AbslHashSetPrinter"
    )
    #debugger.HandleCommand("type summary add -x '^std::pair<.+>$'  -F lldb_printers.Pair1Printer")


#############################
# Pretty Printer Defintions #
#############################

def String1Printer(valobj, *_args):  # pylint: disable=invalid-name
    """Pretty-Prints MongoDB Status objects."""
    return "Test1234"

def Pair1Printer(valobj, *_args):  # pylint: disable=invalid-name
    """Pretty-Prints MongoDB Status objects."""
    print("f %s " % (valobj.GetTypeName()))
    print("f %s " % (valobj.GetType().GetCanonicalType()))
    print("f %s " % (valobj.GetNumChildren()))
    return "qwert" #valobj.GetChildMemberWithName("first")


def StatusPrinter(valobj, *_args):  # pylint: disable=invalid-name
    """Pretty-Prints MongoDB Status objects."""
    err = valobj.GetChildMemberWithName("_error")
    code = err.\
        GetChildMemberWithName("code").\
        GetValue()
    if code is None:
        return "Status::OK()"
    reason = err.\
        GetChildMemberWithName("reason").\
        GetSummary()
    return "Status({}, {})".format(code, reason)


def StatusWithPrinter(valobj, *_args):  # pylint: disable=invalid-name
    """Extend the StatusPrinter to print the value of With for a StatusWith."""
    status = valobj.GetChildMemberWithName("_status")
    code = status.GetChildMemberWithName("_error").\
        GetChildMemberWithName("code").\
        GetValue()
    if code is None:
        return "StatusWith(OK, {})".format(valobj.GetChildMemberWithName("_t").GetValue())
    rep = StatusPrinter(status)
    return rep.replace("Status", "StatusWith", 1)


def StringDataPrinter(valobj, *_args):  # pylint: disable=invalid-name
    """Print StringData value."""
    ptr = valobj.GetChildMemberWithName("_data").GetValueAsUnsigned()
    size1 = valobj.GetChildMemberWithName("_size").GetValueAsUnsigned(0)
    return '"{}"'.format(valobj.GetProcess().ReadMemory(ptr, size1, lldb.SBError()).encode("utf-8"))


def BSONObjPrinter(valobj, *_args):  # pylint: disable=invalid-name
    """Print a BSONObj in a JSON format."""
    ptr = valobj.GetChildMemberWithName("_objdata").GetValueAsUnsigned()
    size = struct.unpack("<I", valobj.GetProcess().ReadMemory(ptr, 4, lldb.SBError()))[0]
    if size < 5 or size > 17 * 1024 * 1024:
        return None
    buf = bson.BSON(bytes(valobj.GetProcess().ReadMemory(ptr, size, lldb.SBError())))
    buf_str = buf.decode()
    obj = json_util.dumps(buf_str, indent=4)
    # If the object is huge then just dump it as one line
    if obj.count("\n") > 1000:
        return json_util.dumps(buf_str)
    # Otherwise try to be nice and pretty print the JSON
    return obj


def UUIDPrinter(valobj, *_args):  # pylint: disable=invalid-name
    """Print the UUID's hex string value."""
    char_array = valobj.GetChildMemberWithName("_uuid").GetChildAtIndex(0)
    raw_bytes = [x.GetValueAsUnsigned() for x in char_array]
    uuid_hex_bytes = [hex(b)[2:].zfill(2) for b in raw_bytes]
    return str(uuid.UUID("".join(uuid_hex_bytes)))


class UniquePtrPrinter:
    """Pretty printer for std::unique_ptr."""

    def __init__(self, valobj, *_args):
        """Store valobj and retrieve object held at the unique_ptr."""
        self.valobj = valobj
        self.update()

    def num_children(self):  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        return 1

    def get_child_index(self, name):  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        if name == "ptr":
            return 0
        else:
            return None

    def get_child_at_index(self, index):  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API.

        Always prints object pointed at by the ptr.
        """
        if index == 0:
            return self.valobj.GetChildMemberWithName("__ptr_").GetChildMemberWithName(
                "__value_").Dereference()
        else:
            return None

    def has_children():  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        return True

    def update(self):  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        pass


class OptionalPrinter:
    """Pretty printer for boost::optional."""

    def __init__(self, valobj, *_args):
        """Store the valobj and get the value of the optional."""
        self.valobj = valobj
        self.update()

    def num_children(self):  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        return 1

    def get_child_index(self, name):  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        if name == "value":
            return 0
        else:
            return None

    def get_child_at_index(self, index):  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        if index == 0:
            return self.value
        else:
            return None

    def has_children():  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        return True

    def update(self):
        """Check if the optional has changed."""
        self.is_init = self.valobj.GetChildMemberWithName("m_initialized").GetValueAsUnsigned() != 0
        self.value = None
        if self.is_init:
            temp_type = self.valobj.GetType().GetTemplateArgumentType(0)
            storage = self.valobj.GetChildMemberWithName("m_storage")
            self.value = storage.Cast(temp_type)

class AbslHashSetPrinter:
    """Pretty printer for absl::container_internal::raw_hash_set."""

    def __init__(self, valobj, *_args):
        """Store the valobj and get the value of the hash_set."""
        self.valobj = valobj

    def num_children(self):  # pylint: no-method-argument
        """Match LLDB's expected API."""
        return self.valobj.GetChildMemberWithName("size_").GetValueAsUnsigned()
        #return 1

    def get_child_index(self, name):  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        return None

    def get_child_at_index(self, index):  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        pos = 0
        count = 0
        ctrl = self.valobj.GetChildMemberWithName("ctrl_")
        if index == 0 and ctrl.GetChildAtIndex(pos,False,True).GetValueAsSigned() > 0:
            pos = 1
        else:
            while count <= index and pos <= self.capacity:
                slot = ctrl.GetChildAtIndex(pos,False,True).GetValueAsSigned()
                if slot >= 0:
                    count += 1

                pos += 1

        value = self.valobj.GetChildMemberWithName("slots_").GetChildAtIndex(pos - 1, False, True)
        print("OOO: %s - %s" % (index, pos));

        #value = self.valobj.GetChildMemberWithName("slots_").GetChildAtIndex(pos - 1,False,False)

        # print("pos: %s" % (pos))
        # print("s: %s" %( value.IsSynthetic()))
        # print("s: %s" %( value.IsDynamic()))
        # print("s: %s" %( value.IsValid()))
        # print("s: %s" %( value.GetID()))
        # print("t: %s" %( value.GetType()))
        # print("t: %s" %( value.GetTypeName()))
        # print("t: %s" %( value.GetType().GetPointeeType()))
        # print("t: %s" %( value.GetType().GetPointeeType()))

        second = value.Dereference()
        # print("s: %s" %( second.IsSynthetic()))
        # print("s: %s" %( second.IsDynamic()))
        # print("s: %s" %( second.IsValid()))
        # print("s: %s" %( second.GetID()))
        # print("t: %s" %( second.GetType()))
        # print("t: %s" %( second.GetTypeName()))
        # print("t: %s" %( second.GetType().GetPointeeType()))
        # print("t: %s" %( second.GetType().GetPointeeType()))
        # # print("t2: %s" %( second.GetType().IsTypedefType()))
        # # print("t3: %s" %( second.GetType().GetTypedefedType()))
        # # #print("t3: %s" %( second.GetType().GetCanonicalType()))
        # # #print("t3: %s" %( second.GetType().GetTypedefedType().GetTypedefedType()))
        # # #return self.valobj.CreateValueFromData('[%d]' % (index), second.GetData(), second.GetType().GetTypedefedType().GetTypedefedType())
        # print("d1: %s "% (second.GetData()) )
        # print("d1: %s "% (second.GetData().GetByteSize()) )

        v = self.valobj.CreateValueFromData('[%d]' % (index), second.GetData(), second.GetType().GetTypedefedType().GetTypedefedType())
        # print("s7: %s" %( v.IsSynthetic()))
        # print("s7: %s" %( v.IsDynamic()))
        # print("s7: %s" %( v.IsValid()))
        # print("s7: %s" %( v.GetID()))
        # print("t7: %s" %( v.GetType()))
        # print("t7: %s" %( v.GetTypeName()))
        # print("t7: %s" %( v.GetData()))
        print("t7: %s" %( v.GetTypeSummary()))

        print("l: %s" %( self.valobj.GetChildMemberWithName("slots_").GetChildAtIndex(0, False, True).GetLocation() ))
        print("l: %s" %( value.GetLocation()))
        value = self.valobj.GetChildMemberWithName("slots_").CreateChildAtOffset("abc + %d" % (pos - 1), (pos - 1) * self.data_size , self.data_type   )
        #return value
        #return self.valobj.CreateValueFromData('[%d]' % (index), second.GetData(), second.GetType().GetTypedefedType().GetTypedefedType() )
        return value.Dereference()

    def has_children():  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        return True

    def update(self):
        self.capacity = self.valobj.GetChildMemberWithName("capacity_").GetValueAsUnsigned()

        self.data_type = self.valobj.GetChildMemberWithName("slots_").GetType()
        
        self.data_size = self.data_type.GetByteSize()
        print("size: %s " % (self.data_size))

    def has_children(self):
        return True

    def get_value(self):
        print("get_value3")
        raise ValueError


# https://github.com/WebKit/webkit/blob/master/Tools/lldb/lldb_webkit.py
class AbslHashMapPrinter:
    """Pretty printer for absl::container_internal::raw_hash_map."""

    def __init__(self, valobj, internal_dict):
        """Store the valobj and get the value of the hash_map."""
        #print("Constructor: %s" % (valobj.GetID()))
        self.valobj = valobj
        # print("Loc1 %s" % (self.valobj.GetLocation()))
        # print("Loc2 %s" % (self.valobj.GetChildMemberWithName("slots_").GetLocation()))
        # print("Loc3 %s" % (self.valobj.GetChildMemberWithName("slots_").GetChildAtIndex(0).GetLocation()))
        # print("Loc4 %s" % (self.valobj.GetChildMemberWithName("slots_").GetChildAtIndex(0).GetChildMemberWithName("first").GetLocation()))
        # a = self.valobj.GetChildMemberWithName("slots_").GetChildAtIndex(0).GetChildMemberWithName("first").GetSummary()
        # print("FOO %s" % (len(a)))
        # print("FOO %s" % (a))
        # print("FOO %s  -" % (''.join(hex(ord(c)) for c in a)))

        print("c1aaaaa: %s" %( valobj.GetNumChildren()))

    def num_children(self):  # pylint: no-method-argument
        """Match LLDB's expected API."""
        return self.valobj.GetChildMemberWithName("size_").GetValueAsUnsigned()
        #return 1

    def get_child_index(self, name):  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        return None

    def get_child_at_index(self, index):  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        pos = 0
        count = 0
        ctrl = self.valobj.GetChildMemberWithName("ctrl_")
        if index == 0 and ctrl.GetChildAtIndex(pos,False,True).GetValueAsSigned() > 0:
            pos = 1
        else:
            while count <= index and pos <= self.capacity:
                slot = ctrl.GetChildAtIndex(pos,False,True).GetValueAsSigned()
                if slot >= 0:
                    count += 1

                pos += 1

        value = self.valobj.GetChildMemberWithName("slots_").GetChildAtIndex(pos - 1,False,True).Dereference()

        # print("pos: %s" % (pos))
        # print("t: %s" %( self.data_type))
        # print("s: %s" %( value.IsSynthetic()))
        # print("s: %s" %( value.IsDynamic()))
        # print("v: %s" %( value.IsValid()))
        # print("p: %s" %( value.TypeIsPointerType()))
        # print("s: %s" %( value.GetID()))
        # print("s: %s" %( value.GetType()))
        # print("s: %s" %( value.GetTypeName()))
        # print("t: %s" %( value.Dereference().GetTypeName()))
        # print("c1: %s" %( value.GetNumChildren()))
        # #print("v1a: %s" %( value.Dereference().GetChildMemberWithName("first")))
        # print("v1: %s" %( value.GetChildMemberWithName("first")))
        # print("v: %s" %( value.GetChildMemberWithName("first").GetName()))
        # print("v: %s" %( value.GetChildMemberWithName("first").GetTypeName()))
        # print("l: %s" %( value.GetChildMemberWithName("first").GetLocation()))
        # print("l: %s" %( value.GetChildMemberWithName("first").AddressOf()))
        # print("v: %s" %( value.GetChildMemberWithName("first").GetValue()))
        # print("d: %s"  %( value.GetChildMemberWithName("first").GetData()))
        # print("d: %s"  %( value.GetChildMemberWithName("first").GetSummary()))

        # print("v2: %s" %( value.GetChildMemberWithName("second")))
        # print("v: %s" %( value.GetChildMemberWithName("second").GetName()))
        # print("v: %s" %( value.GetChildMemberWithName("second").GetTypeName()))
        # print("l: %s" %( value.GetChildMemberWithName("second").GetLocation()))
        # print("l: %s" %( value.GetChildMemberWithName("second").AddressOf()))
        # print("v: %s" %( value.GetChildMemberWithName("second").GetValue()))
        # print("d: %s"  %( value.GetChildMemberWithName("second").GetData()))
        # print("d: %s"  %( value.GetChildMemberWithName("second").GetSummary()))

        #o = self.valobj.GetChildMemberWithName("slots_").CreateChildAtOffset("[" + str(value.GetChildMemberWithName("key").GetValue()) + "]", self.data_size * (pos - 1), self.data_type)
        print("OOO: %s - %s" % (index, pos));

        second = value.GetChildMemberWithName("second")
        #return self.valobj.CreateValueFromData('[%s]a' % (value.GetChildMemberWithName("first").GetSummary()), second.GetData(), second.GetType())

        print("l: %s" %( self.valobj.GetChildMemberWithName("slots_").GetChildAtIndex(0, False, True).GetLocation() ))
        print("l: %s" %( value.GetLocation()))
        print("vv1: %s" %( value.GetChildMemberWithName("first")))
        print("vv1: %s" %( value.Dereference().GetChildMemberWithName("first")))
        print("vv1: %s" %( value.Dereference().Dereference().GetChildMemberWithName("first")))

        value = self.valobj.GetChildMemberWithName("slots_").CreateChildAtOffset("abcd + %s" % (pos - 1), (pos - 1) * self.data_size , self.data_type   )
        #print("s: %s" %( value.Dereference().GetType().GetTypedefedType().GetTypedefedType().GetTypedefedType()))
        print("s: %s" %( value.Dereference().GetType().GetCanonicalType().GetPointeeType()))
        print("s: %s" %( value.Dereference().GetType().GetCanonicalType().GetPointeeType().GetNumberOfFields()))
        print("v1: %s" %( value.IsValid()))
        print("v1: %s" %( value.GetChildMemberWithName("first").IsValid()))
        print("v1: %s" %( value.GetChildMemberWithName("first2").IsValid()))
        print("v1: %s" %( value.Dereference().GetChildMemberWithName("first")))
        print("v1: %s" %( value.Dereference().Dereference().GetChildMemberWithName("first")))

        return self.valobj.GetChildMemberWithName("slots_").GetChildAtIndex(pos - 1,False,True).Dereference()
        #return value


    def has_children():  # pylint: disable=no-self-use,no-method-argument
        """Match LLDB's expected API."""
        return True

    def update(self):
        self.capacity = self.valobj.GetChildMemberWithName("capacity_").GetValueAsUnsigned()
        self.data_type = self.valobj.GetChildMemberWithName("slots_").GetType()
        self.data_size = self.data_type.GetByteSize()

