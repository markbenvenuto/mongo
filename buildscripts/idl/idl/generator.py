"""Generator"""
from __future__ import absolute_import, print_function, unicode_literals

import ast
import io

INDENT_SPACE_COUNT=4

class IndentedTextWriter(object):
    def __init__(self, stream, *args, **kwargs):
        # type (io.IOBase, *str, **bool) -> None
        self._stream = stream
        self._indent = 0
        #super(IndentedTextWriter, self).__init__(*args, **kwargs)

    def write_unindented_line(self, msg):
        # type: (unicode) -> None
        """Write a line to the stream"""
        self._stream.write(msg)
        self._stream.write(u"\n")

    def indent(self):
        # type: () -> None
        self._indent += 1

    def unindent(self):
        # type: () -> None
        self._indent -= 1
        
    def write_line(self, msg):
        # type: (unicode) -> None
        """Write a line to the stream"""
        fill = ''
        for x in range(self._indent *  INDENT_SPACE_COUNT):
            fill += ' '

        self._stream.write(fill)
        self._stream.write(msg)
        self._stream.write(u"\n")

class CppFileWriter(object):
    """C++ File writer. Encapsulates low level knowledge of how to print a file.
       Relies on caller to orchestrate calls correctly though.
    """
    def __init__(self, writer, *args, **kwargs):
        # type: (IndentedTextWriter, *str, **bool) -> None
        self._writer = writer # type: IndentedTextWriter
        #super(CppFileWriter, self).__init__(*args, **kwargs)

    def write_unindented_line(self, msg):
        # type (unicode) -> None
        self._writer.write_unindented_line(msg)

    def gen_include(self, include):
        # type (unicode) -> None
        """Generate a C++ include line"""
        self._writer.write_unindented_line('#include "%s"' % include)

    def gen_namespace(self, namespace):
        # type (unicode) -> ScopedBlock
        return ScopedBlock(self._writer, "namespace %s {" % namespace, "}  // namespace %s" % namespace)

    def gen_class_declaration(self, class_name):
        # type (unicode) -> ScopedBlock
        return IndentedScopedBlock(self._writer, "class %s {" % class_name.capitalize(), "};")

    def gen_serializer_methods(self, class_name):
        # type (unicode) -> None
        self._writer.write_line("static %s parse(const BSONObj& object);" % class_name.capitalize())
        self._writer.write_line("void serialize(BSONObjBuilder* builder) const;")
        self._writer.write_unindented_line("")

    def _get_field_parameter_type(self, field):
        # type (ast.Field) -> unicode
        if field.required == False:
            return "boost::optional<%s>" % field.cpp_type

        return field.cpp_type

    def _get_field_member_name(self, field):
        # type (ast.Field) -> unicode
        return "_" + field.name

    def gen_getter(self, field):
        # type (ast.Field) -> None
        param_type = self._get_field_parameter_type(field)
        member_name = self._get_field_member_name(field)

        self._writer.write_line("const %s& get%s() const { return %s}" % (param_type, field.name.capitalize(), member_name))
        self._writer.write_unindented_line("")

    def gen_setter(self, field):
        # type (ast.Field) -> None
        self._writer.write_line("void set%s(%s value) { %s = std::move(value); }" % (field.name.capitalize(), param_type, member_name))
        self._writer.write_unindented_line("")

class ScopedBlock(object):
    def __init__(self, writer, opening, closing):
        # type: (IndentedTextWriter, unicode, unicode) -> None
        self._writer = writer
        self._opening = opening
        self._closing = closing

    def __enter__(self):
        self._writer.write_unindented_line(self._opening)

    def __exit__(self, *args):
        self._writer.write_unindented_line(self._closing)

class IndentedScopedBlock(object):
    def __init__(self, writer, opening, closing):
        # type: (IndentedTextWriter, unicode, unicode) -> None
        self._writer = writer
        self._opening = opening
        self._closing = closing

    def __enter__(self):
        self._writer.write_line(self._opening)
        self._writer.indent()

    def __exit__(self, *args):
        self._writer.unindent()
        self._writer.write_line(self._closing)

def generate_code(spec, file_prefix):
    # type (ast.IDLSpec, unicode) -> None
    #self._stream = io.open(file_name, mode="w")

    stream = io.StringIO()
    text_writer = IndentedTextWriter(stream)

    header = CppFileWriter(text_writer)

    # Generate includes
    for include in spec.globals.cpp_includes:
        header.gen_include(include)

    # Generate namesapce
    with header.gen_namespace("mongo"):
        for struct in spec.symbols.structs:
            with header.gen_class_declaration(struct.name):
                header.write_unindented_line("public:")

                # Write constructor
                header.gen_serializer_methods(struct.name)

                # Write getters & setters
                for field in struct.fields:
                    header.gen_getter(field)
                    header.gen_setter(field)

                header.write_unindented_line("private:")

                # Write member variables
    # Generate structs
    print(stream.getvalue())
