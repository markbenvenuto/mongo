"""Generator"""
from __future__ import absolute_import, print_function, unicode_literals

from . import ast
import io
import os
import string

INDENT_SPACE_COUNT = 4


def camel_case(name):
    # type: (unicode) -> unicode
    """Return a camelCased version of a string."""
    if len(name) > 1:
        name = string.upper(name[0:1]) + name[1:]
    return name


def get_method_name(name):
    # type: (unicode) -> unicode
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
        self._writer.write_line("static %s parse(IDLParserErrorContext& ctxt, const BSONObj& object);" % camel_case(class_name))
        self._writer.write_line("void serialize(BSONObjBuilder* builder) const;")
        self._writer.write_empty_line()

    def _get_field_cpp_type(self, field):
        # type: (ast.Field) -> unicode
        assert field.cpp_type is not None or field.struct_type is not None

        if field.struct_type:
            cpp_type = field.struct_type
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
        # type: (ast.Field) -> None
        bson_types = field.bson_serialization_type
        if len(bson_types) == 1:
            if bson_types[0] == "any":
                # Skip BSON valiation when any
                return

            if not bson_types[0] == "bindata":
                self._writer.write_line('ctxt.assertType(element, %s);' % bson_types[0])
            else:
                self._writer.write_line('ctxt.assertBinDataType(element, %s);' % field.bindata_subtype)
        else:
            type_list = "{%s}" % (','.join(bson_types))
            self._writer.write_line('ctxt.assertTypes(element, %s);' % type_list)
            

    def _access_member(self, field):
        # type: (ast.Field) -> unicode
        member_name = self._get_field_member_name(field)
        if not field.optional:
            return "%s" % member_name
        # optional
        return "%s.get()" % member_name

    def _block(self, opening, closing):
        # type: (unicode, unicode) -> IndentedScopedBlock
        return IndentedScopedBlock(self._writer, opening, closing)

    def gen_deserializer(self, struct):
        # type: (ast.Struct) -> None

        with self._block("%s %s::parse(IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {" %
                         (camel_case(struct.name), camel_case(struct.name)), "}"):

            # Generate a check to ensure the object is not empty
            # TODO: handle objects which are entirely optional
            with self._block("if (bsonObject.isEmpty()) {", "}"):
                self._writer.write_line('ctxt.throwNotEmptyObject();')
                #self._writer.write_line('ctxt.throwNotEmptyObject("%s");' % struct.name)
            self._writer.write_empty_line()

            self._writer.write_line("%s object;" % camel_case(struct.name))

            with self._block("for (const auto&& element : bsonObject) {", "}"):

                self._writer.write_line("const auto& fieldName = element.fieldNameStringData();")

                # TODO: generate command namespace string check

                first_field = True
                for field in struct.fields:
                    predicate = 'else if (fieldName == "%s") {' % field.name
                    if first_field:
                        predicate = 'if (fieldName == "%s") {' % field.name
                        first_field = False

                    with self._block(predicate, "}"):
                        # TODO: check type of field
                        # TODO: check for duplicates
                        if field.ignore:
                            self._writer.write_line("// ignore field")
                        else:
                            # TODO: handle bindata
                            self.gen_bson_type_check(field)
                            self._writer.write_empty_line()

                            if field.struct_type:
                                self._writer.write_line('object.%s = %s::parse(IDLParserErrorContext("%s", &ctxt), element.Obj());' %
                                                        (self._get_field_member_name(field),
                                                         camel_case(field.struct_type),
                                                         field.name))
                            elif "BSONElement::" in field.deserializer:
                                method_name = get_method_name(field.deserializer)
                                self._writer.write_line("object.%s = element.%s();" % (
                                    self._get_field_member_name(field), method_name))
                            else:
                                # Custom method, call the method on object
                                method_name = get_method_name(field.deserializer)
                                self._writer.write_line("object.%s.%s(element);" % (
                                    self._get_field_member_name(field), method_name))

                # TODO: generate strict check for extranous fields

                # TODO: default values

                # TODO: required fields

            self._writer.write_line("return object;")

    def gen_serializer(self, struct):
        # type: (ast.Struct) -> None

        with self._block("void %s::serialize(BSONObjBuilder* builder) const {" %
                         camel_case(struct.name), "}"):

            for field in struct.fields:
                if field.ignore:
                    continue

                member_name = self._get_field_member_name(field)
                if not field.struct_type:
                    # if field.serializer:
                    #     # Generate custom serialization
                    #     method_name = get_method_name(field.deserializer)
                    #     if field.required:
                    #         self._writer.write_line('builder->FOOOO("%s", %s);' % (field.name, member_name))
                    #     else:
                    #         with self._block("if (%s) {" % member_name, "}"):
                    #             self._writer.write_line('builder->FOOOO("%s", %s.get());' % (field.name, member_name))
                    # - 
                    #             self._writer.write_line('builder->FOOOO("%s", %s.get());' % (field.name, member_name))

                    # else:
                    # Generate default serialization using BSONObjBuilder::append
                    if not field.optional:
                        self._writer.write_line('builder->append("%s", %s);' %
                                                (field.name, member_name))
                    else:
                        with self._block("if (%s) {" % member_name, "}"):
                            self._writer.write_line('builder->append("%s", %s.get());' %
                                                    (field.name, member_name))
                else:
                    predicate = "{"
                    if field.optional:
                        predicate = "if (%s) {" % member_name

                    with self._block(predicate, "}"):
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
    file_handle = io.open(file_name, mode="wb")
    file_handle.write(stream.getvalue())


def generate_source(spec, file_name):
    # type: (ast.IDLAST, unicode) -> None
    stream = io.StringIO()
    text_writer = IndentedTextWriter(stream)

    source = CppFileWriter(text_writer)

    # Generate includes
    # TODO: refine
    source.gen_include(os.path.abspath(file_name.replace(".cc", ".hpp")))

    # Add system includes
    source.gen_include("mongo/bson/bsonobjbuilder.h")
    source.write_empty_line()

    # Generate namesapce
    with source.gen_namespace("mongo"):
        source.write_empty_line()

        for struct in spec.structs:
            # Write deserializer
            source.gen_deserializer(struct)
            source.write_empty_line()

            # Write serializer
            source.gen_serializer(struct)
            source.write_empty_line()

    # Generate structs
    print(stream.getvalue())

    file_handle = io.open(file_name, mode="wb")
    file_handle.write(stream.getvalue())


def generate_code(spec, file_prefix):
    # type: (ast.IDLAST, unicode) -> None
    #self._stream = io.open(file_name, mode="w")

    assert '.' not in file_prefix

    generate_header(spec, file_prefix + ".hpp")

    generate_source(spec, file_prefix + ".cc")
