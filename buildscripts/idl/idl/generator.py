"""Generator"""
from __future__ import absolute_import, print_function, unicode_literals

import io
import os
import string
from typing import Union

from . import ast
from . import bson

INDENT_SPACE_COUNT = 4


def camel_case(name):
    # type: (unicode) -> unicode
    """Return a camelCased version of a string."""
    if len(name) > 1:
        name = string.upper(name[0:1]) + name[1:]
    return name


def get_method_name(name):
    # type: (unicode) -> unicode
    # TODO: in the future, we may want to support full-qualified calls to static methods
    pos = name.rfind("::")
    if pos == -1:
        return name
    return name[pos + 2:]


def is_view_type(cpp_type):
    # type: (unicode) -> bool
    """Return True if a cpp_type should be returned as a view type from an IDL class."""
    if cpp_type == "std::string":
        return True
    return False


def get_view_type(cpp_type):
    # type: (unicode) -> unicode
    """Map a C++ type to its C++ view type if needed."""
    if cpp_type == "std::string":
        cpp_type = "StringData"

    return cpp_type


class IndentedTextWriter(object):
    """
    A simple class to manage writing indented lines of text.

    Supports both writing indented lines, and unindented lines.
    Use write_empty_line() instead of write_line('') to avoid lines
    full of blank spaces.
    """

    def __init__(self, stream):
        # type: (io.StringIO) -> None
        self._stream = stream
        self._indent = 0

    def write_unindented_line(self, msg):
        # type: (unicode) -> None
        """Write a line to the stream"""
        self._stream.write(msg)
        self._stream.write(u"\n")

    def indent(self):
        # type: () -> None
        """Indent the text by one level."""
        self._indent += 1

    def unindent(self):
        # type: () -> None
        """Unindent the text by one level."""
        assert self._indent > 0
        self._indent -= 1

    def write_line(self, msg):
        # type: (unicode) -> None
        """Write a line to the stream."""
        fill = ''
        for _ in range(self._indent * INDENT_SPACE_COUNT):
            fill += ' '

        self._stream.write(fill)
        self._stream.write(msg)
        self._stream.write(u"\n")

    def write_empty_line(self):
        # type: () -> None
        """Write a line to the stream."""
        self._stream.write(u"\n")


class EmptyBlock(object):
    """Do not generate an indented block stuff."""

    def __init__(self):
        # type: () -> None
        pass

    def __enter__(self):
        # type: () -> None
        pass

    def __exit__(self, *args):
        # type: (*str) -> None
        pass


class ScopedBlock(object):
    def __init__(self, writer, opening, closing):
        # type: (IndentedTextWriter, unicode, unicode) -> None
        self._writer = writer
        self._opening = opening
        self._closing = closing

    def __enter__(self):
        # type: () -> None
        self._writer.write_unindented_line(self._opening)

    def __exit__(self, *args):
        # type: (*str) -> None
        self._writer.write_unindented_line(self._closing)


class IndentedScopedBlock(object):
    def __init__(self, writer, opening, closing):
        # type: (IndentedTextWriter, unicode, unicode) -> None
        self._writer = writer
        self._opening = opening
        self._closing = closing

    def __enter__(self):
        # type: () -> None
        self._writer.write_line(self._opening)
        self._writer.indent()

    def __exit__(self, *args):
        # type: (*str) -> None
        self._writer.unindent()
        self._writer.write_line(self._closing)


class FieldUsageChecker(object):
    # Check for duplicate fields, and required fields as needed

    def __init__(self, writer):
        # type: (IndentedTextWriter) -> None
        self._writer = writer  # type: IndentedTextWriter
        self.fields = set()  # type: Set[ast.Field]

        self._writer.write_line("std::set<StringData> usedFields;")

    def add_store(self):
        # type: () -> None
        self._writer.write_line('auto push_result = usedFields.insert(fieldName);')
        with IndentedScopedBlock(self._writer, "if (push_result.second == false) {", "}"):
            self._writer.write_line('ctxt.throwDuplicateField(element);')

    def add(self, field):
        # type: (ast.Field) -> None
        self.fields.add(field)

    def add_final_checks(self):
        # type: () -> None
        for field in self.fields:
            if (not field.optional) and (not field.ignore):
                # TODO: improve if we know the storage is optional
                with IndentedScopedBlock(self._writer,
                                         'if (usedFields.find("%s") == usedFields.end()) {' %
                                         field.name, "}"):
                    self._writer.write_line('ctxt.throwMissingField("%s");' % field.name)


class CppFileWriter(object):
    """
    C++ File writer.

    Encapsulates low level knowledge of how to print a file.
    Relies on caller to orchestrate calls correctly though.
    """

    def __init__(self, writer):
        # type: (IndentedTextWriter) -> None
        self._writer = writer  # type: IndentedTextWriter

    def write_unindented_line(self, msg):
        # type: (unicode) -> None
        self._writer.write_unindented_line(msg)

    def write_empty_line(self):
        # type: () -> None
        self._writer.write_empty_line()

    def gen_include(self, include):
        # type: (unicode) -> None
        """Generate a C++ include line."""
        self._writer.write_unindented_line('#include "%s"' % include)

    def gen_namespace(self, namespace):
        # type: (unicode) -> ScopedBlock
        return ScopedBlock(self._writer, "namespace %s {" % namespace,
                           "}  // namespace %s" % namespace)

    def gen_class_declaration(self, class_name):
        # type: (unicode) -> IndentedScopedBlock
        return IndentedScopedBlock(self._writer, "class %s {" % camel_case(class_name), "};")

    def gen_serializer_methods(self, class_name):
        # type: (unicode) -> None
        self._writer.write_line(
            "static %s parse(IDLParserErrorContext& ctxt, const BSONObj& object);" %
            camel_case(class_name))
        self._writer.write_line("void serialize(BSONObjBuilder* builder) const;")
        self._writer.write_empty_line()

    def _get_field_cpp_type(self, field):
        # type: (ast.Field) -> unicode
        assert field.cpp_type is not None or field.struct_type is not None

        if field.struct_type:
            cpp_type = camel_case(field.struct_type)
        else:
            cpp_type = field.cpp_type
            if cpp_type == "std::string":
                cpp_type = "StringData"

        return cpp_type

    def _get_field_parameter_type(self, field):
        # type: (ast.Field) -> unicode
        assert field.cpp_type is not None or field.struct_type is not None

        cpp_type = self._get_field_cpp_type(field)

        if field.optional:
            return "boost::optional<%s>" % cpp_type

        return cpp_type

    def _get_field_member_type(self, field):
        # type: (ast.Field) -> unicode
        return self._get_field_parameter_type(field)

    def _get_field_member_name(self, field):
        # type: (ast.Field) -> unicode
        return "_" + field.name

    def gen_getter(self, field):
        # type: (ast.Field) -> None
        cpp_type = self._get_field_cpp_type(field)
        param_type = self._get_field_parameter_type(field)
        member_name = self._get_field_member_name(field)

        if not is_view_type(cpp_type):
            optional_ampersand = "&"
            body = "return %s;" % (member_name)
        else:
            optional_ampersand = ""
            body = "return %s(%s);" % (param_type, member_name)

        # TODO: generate xvalue 
        #  WriteConcernWriteField getW() && { return std::move(_w); } = delete

        self._writer.write_line("const %s%s get%s() const { %s }" %
                                (param_type, optional_ampersand, camel_case(field.name), body))

    def gen_setter(self, field):
        # type: (ast.Field) -> None
        param_type = self._get_field_parameter_type(field)
        member_name = self._get_field_member_name(field)

        self._writer.write_line("void set%s(%s value) { %s = std::move(value); }" %
                                (camel_case(field.name), param_type, member_name))
        self._writer.write_empty_line()

    def gen_member(self, field):
        # type: (ast.Field) -> None
        member_type = self._get_field_member_type(field)
        member_name = self._get_field_member_name(field)

        self._writer.write_line("%s %s;" % (member_type, member_name))

    def gen_bson_type_check(self, field):
        # type: (ast.Field) -> unicode
        bson_types = field.bson_serialization_type
        if len(bson_types) == 1:
            if bson_types[0] == "any":
                # Skip BSON valiation when any
                return None

            if not bson_types[0] == "bindata":
                return 'ctxt.checkAndAssertType(element, %s)' % bson.cpp_bson_type_name(
                    bson_types[0])
            else:
                return 'ctxt.assertBinDataType(element, %s)' % bson.cpp_bindata_subtype_type_name(
                    field.bindata_subtype)
        else:
            type_list = "{%s}" % (', '.join([bson.cpp_bson_type_name(b) for b in bson_types]))
            return 'ctxt.checkAndAssertTypes(element, %s)' % type_list

    def _access_member(self, field):
        # type: (ast.Field) -> unicode
        member_name = self._get_field_member_name(field)
        if not field.optional:
            return "%s" % member_name
        # optional
        return "%s.get()" % member_name

    def _block(self, opening, closing):
        # type: (unicode, unicode) -> Union[IndentedScopedBlock,EmptyBlock]
        if not opening:
            return EmptyBlock()

        return IndentedScopedBlock(self._writer, opening, closing)

    def _predicate(self, check_str, use_else_if=False):
        # type: (unicode, bool) -> Union[IndentedScopedBlock,EmptyBlock]
        if not check_str:
            return EmptyBlock()

        conditional = "if"
        if use_else_if:
            conditional = "else if"

        return IndentedScopedBlock(self._writer, "%s (%s) {" % (conditional, check_str), "}")

    def gen_field_deserializer(self, field):
        # type: (ast.Field) -> None
        # May be an empty block if the type is any
        type_predicate = self.gen_bson_type_check(field)

        with self._predicate(type_predicate):

            if field.struct_type:
                self._writer.write_line(
                    'object.%s = %s::parse(IDLParserErrorContext("%s", &ctxt), element.Obj());' %
                    (self._get_field_member_name(field), camel_case(field.struct_type), field.name))
            elif "BSONElement::" in field.deserializer:
                method_name = get_method_name(field.deserializer)
                self._writer.write_line("object.%s = element.%s();" %
                                        (self._get_field_member_name(field), method_name))
            else:
                # Custom method, call the method on object
                # TODO: avoid this string hack in the future
                if len(field.bson_serialization_type) == 1 and field.bson_serialization_type[
                        0] == "string":
                    self._writer.write_line("auto tempValue = element.valueStringData();")

                    method_name = get_method_name(field.deserializer)
                    self._writer.write_line("object.%s = %s(tempValue);" %
                                            (self._get_field_member_name(field), method_name))
                else:
                    self._writer.write_line("object.%s = TODO %s(tempValue);" %
                                            (self._get_field_member_name(field), method_name))

    def gen_deserializer_method(self, struct):
        # type: (ast.Struct) -> None

        with self._block("%s %s::parse(IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {" %
                         (camel_case(struct.name), camel_case(struct.name)), "}"):

            # Generate a check to ensure the object is not empty
            # TODO: handle objects which are entirely optional
            #with self._predicate("bsonObject.isEmpty()"):
            #self._writer.write_line('ctxt.throwNotEmptyObject();')
            #self._writer.write_line('ctxt.throwNotEmptyObject("%s");' % struct.name)
            #self._writer.write_empty_line()

            self._writer.write_line("%s object;" % camel_case(struct.name))

            field_usage_check = FieldUsageChecker(self._writer)
            self._writer.write_empty_line()

            with self._block("for (const auto&& element : bsonObject) {", "}"):

                self._writer.write_line("const auto& fieldName = element.fieldNameStringData();")
                self._writer.write_empty_line()

                # TODO: generate command namespace string check
                field_usage_check.add_store()
                self._writer.write_empty_line()

                first_field = True
                for field in struct.fields:
                    field_predicate = 'fieldName == "%s"' % field.name
                    field_usage_check.add(field)

                    with self._predicate(field_predicate, not first_field):
                        if field.ignore:
                            self._writer.write_line("// ignore field")
                        else:
                            self.gen_field_deserializer(field)

                    if first_field:
                        first_field = False

                # End of for fields
                # Generate strict check for extranous fields
                if struct.strict:
                    with self._block("else {", "}"):
                        self._writer.write_line("ctxt.throwUnknownField(fieldName);")

            self._writer.write_empty_line()

            # Check for required fields
            field_usage_check.add_final_checks()
            self._writer.write_empty_line()

            # TODO: default values

            self._writer.write_line("return object;")

    def gen_serializer_method(self, struct):
        # type: (ast.Struct) -> None

        with self._block("void %s::serialize(BSONObjBuilder* builder) const {" %
                         camel_case(struct.name), "}"):

            for field in struct.fields:
                # If fields are meant to be ignored during deserialization, there is not need to serialize them
                if field.ignore:
                    continue

                member_name = self._get_field_member_name(field)

                optional_predicate = None
                if field.optional:
                    optional_predicate = member_name

                with self._predicate(optional_predicate):

                    if not field.struct_type:
                        if field.serializer:
                            # Generate custom serialization
                            method_name = get_method_name(field.serializer)

                            if len(field.bson_serialization_type
                                   ) == 1 and field.bson_serialization_type[0] == "string":
                                self._writer.write_line('auto tempValue = %s.%s();' %
                                                        (self._access_member(field), method_name))
                                self._writer.write_line('builder->append("%s", tempValue);' %
                                                        (field.name))
                            else:
                                self._writer.write_line('%s.%s(builder);' %
                                                        (self._access_member(field), method_name))

                        else:
                            # Generate default serialization using BSONObjBuilder::append
                            self._writer.write_line('builder->append("%s", %s);' %
                                                    (field.name, self._access_member(field)))

                    else:
                        self._writer.write_line(
                            'BSONObjBuilder subObjBuilder(builder->subobjStart("%s"));' %
                            (field.name))
                        self._writer.write_line('%s.serialize(&subObjBuilder);' %
                                                (self._access_member(field)))

                    self._writer.write_empty_line()


def generate_header(spec, file_name):
    # type: (ast.IDLAST, unicode) -> None
    stream = io.StringIO()
    text_writer = IndentedTextWriter(stream)

    header = CppFileWriter(text_writer)

    # TODO: sort headers
    # Add system includes
    header.gen_include("mongo/bson/bsonobj.h")
    header.gen_include("mongo/idl/idl_parser.h")

    # Generate includes
    for include in spec.globals.cpp_includes:
        header.gen_include(include)

    header.write_empty_line()

    # Generate namesapce
    with header.gen_namespace("mongo"):
        header.write_empty_line()

        for struct in spec.structs:
            with header.gen_class_declaration(struct.name):
                header.write_unindented_line("public:")

                # Write constructor
                header.gen_serializer_methods(struct.name)

                # Write getters & setters
                for field in struct.fields:
                    if not field.ignore:
                        header.gen_getter(field)
                        header.gen_setter(field)

                header.write_unindented_line("private:")

                # Write member variables
                for field in struct.fields:
                    if not field.ignore:
                        header.gen_member(field)

            header.write_empty_line()

    # Generate structs
    print(stream.getvalue())

    print("Writing header to: %s" % file_name)
    file_handle = io.open(file_name, mode="wb")
    file_handle.write(stream.getvalue())


def generate_source(spec, file_name, header_file_name):
    # type: (ast.IDLAST, unicode, unicode) -> None
    stream = io.StringIO()
    text_writer = IndentedTextWriter(stream)

    source = CppFileWriter(text_writer)

    # Generate includes
    source.gen_include(os.path.abspath(header_file_name))

    # Add system includes
    source.gen_include("mongo/bson/bsonobjbuilder.h")
    source.write_empty_line()

    # Generate namesapce
    with source.gen_namespace("mongo"):
        source.write_empty_line()

        for struct in spec.structs:
            # Write deserializer
            source.gen_deserializer_method(struct)
            source.write_empty_line()

            # Write serializer
            source.gen_serializer_method(struct)
            source.write_empty_line()

    # Generate structs
    print(stream.getvalue())

    print("Writing code to: %s" % file_name)
    file_handle = io.open(file_name, mode="wb")
    file_handle.write(stream.getvalue())


def generate_code(spec, header_file_name, source_file_name):
    # type: (ast.IDLAST, unicode, unicode) -> None
    #self._stream = io.open(file_name, mode="w")

    generate_header(spec, header_file_name)

    generate_source(spec, source_file_name, header_file_name)
