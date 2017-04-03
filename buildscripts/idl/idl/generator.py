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
"""IDL C++ Code Generator."""

from __future__ import absolute_import, print_function, unicode_literals

import io
import os
import string
import sys
import textwrap
from typing import List, Mapping, Union

from . import ast
from . import bson

# Number of spaces to indent code
_INDENT_SPACE_COUNT = 4


def _title_case(name):
    # type: (unicode) -> unicode
    """Return a CapitalCased version of a string."""
    return name[0:1].upper() + name[1:]


def _camel_case(name):
    # type: (unicode) -> unicode
    """Return a camelCased version of a string."""
    return name[0:1].lower() + name[1:]


def _get_method_name(name):
    # type: (unicode) -> unicode
    """Get a method name from a fully qualified method name."""
    pos = name.rfind('::')
    if pos == -1:
        return name
    return name[pos + 2:]


def _get_method_name_from_qualified_method_name(name):
    # type: (unicode) -> unicode
    # pylint: disable=invalid-name
    """Get a method name from a fully qualified method name."""
    # TODO: in the future, we may want to support full-qualified calls to static methods
    prefix = 'mongo::'
    pos = name.find(prefix)
    if pos == -1:
        return name

    return name[len(prefix):]


def _is_primitive_type(cpp_type):
    # type: (unicode) -> bool
    """Return True if a cpp_type is a primitive type and should not be returned as reference."""
    return cpp_type in [
        'bool', 'double', 'std::int32_t', 'std::uint32_t', 'std::uint64_t', 'std::int64_t'
    ]


def _is_view_type(cpp_type):
    # type: (unicode) -> bool
    """Return True if a cpp_type should be returned as a view type from an IDL class."""
    if cpp_type == 'std::string':
        return True

    if cpp_type == 'std::vector<std::uint8_t>':
        return True

    return False


def _get_view_type(cpp_type):
    # type: (unicode) -> unicode
    """Map a C++ type to its C++ view type if needed."""
    if cpp_type == 'std::string':
        cpp_type = 'StringData'

    if cpp_type == 'std::vector<std::uint8_t>':
        cpp_type = 'mongo::ConstDataRange'

    return cpp_type


def _get_view_type_to_base_method(cpp_type):
    # type: (unicode) -> unicode
    """Map a C++ View type to its C++ base type."""
    assert _is_view_type(cpp_type)

    if cpp_type == 'std::vector<std::uint8_t>':
        return "True"

    return "toString"


def _get_field_cpp_type(field):
    # type: (ast.Field) -> unicode
    """Get the C++ type name for a field."""
    assert field.cpp_type is not None or field.struct_type is not None

    if field.struct_type:
        cpp_type = _title_case(field.struct_type)
    else:
        cpp_type = field.cpp_type

    return cpp_type


def _qualify_optional_type(cpp_type, field):
    # type: (unicode, ast.Field) -> unicode
    """Qualify the type if the field is optional."""
    if field.optional:
        return 'boost::optional<%s>' % (cpp_type)

    return cpp_type


def _qualify_array_type(cpp_type, field):
    # type: (unicode, ast.Field) -> unicode
    """Qualify the type if the field is an array."""
    if field.array:
        cpp_type = "std::vector<%s>" % (cpp_type)

    return cpp_type


def _get_field_getter_setter_type(field):
    # type: (ast.Field) -> unicode
    """Get the C++ type name for the getter/setter parameter for a field."""
    assert field.cpp_type is not None or field.struct_type is not None

    cpp_type = _get_field_cpp_type(field)

    cpp_type = _get_view_type(cpp_type)

    cpp_type = _qualify_array_type(cpp_type, field)

    return _qualify_optional_type(cpp_type, field)


def _get_field_storage_type(field):
    # type: (ast.Field) -> unicode
    """Get the C++ type name for the storage of class member for a field."""
    cpp_type = _get_field_cpp_type(field)

    cpp_type = _qualify_array_type(cpp_type, field)

    return _qualify_optional_type(cpp_type, field)


def _get_field_member_name(field):
    # type: (ast.Field) -> unicode
    """Get the C++ class member name for a field."""
    return '_%s' % (_camel_case(field.name))


def _get_return_by_reference(field):
    # type: (ast.Field) -> bool
    """Return True if the type should be returned by reference."""
    # For non-view types, return a reference for types:
    #  1. arrays
    #  2. nested structs
    # But do not return a reference for:
    #  1. std::int32_t and other primitive types
    #  2. optional types
    cpp_type = _get_field_cpp_type(field)

    if not _is_view_type(cpp_type) and (not field.optional and
                                        (not _is_primitive_type(cpp_type) or field.array)):
        return True

    return False


def _get_disable_xvalue(field):
    # type: (ast.Field) -> bool
    """Return True if the type should have the xvalue getter disabled."""
    # Any we return references or view types, we should disable the xvalue.
    # For view types like StringData, the return type and member storage types are different
    # so returning a reference is not supported.
    cpp_type = _get_field_cpp_type(field)

    return _is_view_type(cpp_type) or _get_return_by_reference(field)


def _get_bson_type_check(bson_element, ctxt_name, field):
    # type: (unicode, unicode, ast.Field) -> unicode
    """Get the C++ bson type check for a field."""
    bson_types = field.bson_serialization_type
    if len(bson_types) == 1:
        if bson_types[0] == 'any':
            # Skip BSON valiation when any
            return None

        if not bson_types[0] == 'bindata':
            return '%s.checkAndAssertType(%s, %s)' % (ctxt_name, bson_element,
                                                      bson.cpp_bson_type_name(bson_types[0]))
        else:
            return '%s.checkAndAssertBinDataType(%s, %s)' % (
                ctxt_name, bson_element, bson.cpp_bindata_subtype_type_name(field.bindata_subtype))
    else:
        type_list = '{%s}' % (', '.join([bson.cpp_bson_type_name(b) for b in bson_types]))
        return '%s.checkAndAssertTypes(%s, %s)' % (ctxt_name, bson_element, type_list)


def _access_member(field):
    # type: (ast.Field) -> unicode
    """Get the declaration to access a member for a field."""
    member_name = _get_field_member_name(field)

    if not field.optional:
        return '%s' % (member_name)

    # optional types need a method call to access their values
    return '%s.get()' % (member_name)


def fill_spaces(count):
    # type: (int) -> unicode
    """Fill a string full of spaces."""
    fill = ''
    for _ in range(count * _INDENT_SPACE_COUNT):
        fill += ' '

    return fill


def _template_format(template, template_params):
    # type: (unicode, Mapping[unicode,unicode]) -> unicode
    """Write a template to the stream."""
    # Ignore the types since we use unicode literals and this expects str but works fine with
    # unicode.
    # See https://docs.python.org/2/library/string.html#template-strings
    return string.Template(template).substitute(template_params)  # type: ignore


def indent_text(count, unindented_text):
    # type: (int, unicode) -> unicode
    """Indent each line of a multi-line string."""
    lines = unindented_text.splitlines()
    fill = fill_spaces(count)
    return '\n'.join(fill + line for line in lines)


class _IndentedTextWriter(object):
    """
    A simple class to manage writing indented lines of text.

    Supports both writing indented lines, and unindented lines.
    Use write_empty_line() instead of write_line('') to avoid lines
    full of blank spaces.
    """

    def __init__(self, stream):
        # type: (io.StringIO) -> None
        """Create an indented text writer."""
        self._stream = stream
        self._indent = 0
        self._template_context = None  # type: Mapping[unicode, unicode]

    def write_unindented_line(self, msg):
        # type: (unicode) -> None
        """Write an unindented line to the stream, no template formattin applied."""
        self._stream.write(msg)
        self._stream.write("\n")

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
        """Write a line to the stream, no template formattin applied."""
        self._stream.write(indent_text(self._indent, msg))
        self._stream.write("\n")

    def set_template_mapping(self, template_params):
        # type: (Mapping[unicode,unicode]) -> None
        """Set the current template mapping parameters for string.Template formatting."""
        assert not self._template_context
        self._template_context = template_params

    def clear_template_mapping(self):
        # type: () -> None
        """Clear the current template mapping parameters for string.Template formatting."""
        assert self._template_context
        self._template_context = None

    def write_template(self, template):
        # type: (unicode) -> None
        """Write a template to the stream."""
        msg = _template_format(template, self._template_context)
        self._stream.write(indent_text(self._indent, msg))
        self._stream.write("\n")

    def write_empty_line(self):
        # type: () -> None
        """Write a line to the stream."""
        self._stream.write("\n")


class _TemplateContext(object):
    """Set the template context for the writer."""

    def __init__(self, writer, template_params):
        # type: (_IndentedTextWriter, Mapping[unicode,unicode]) -> None
        """Create a template context."""
        self._writer = writer
        self._template_context = template_params

    def __enter__(self):
        # type: () -> None
        """Set the template mapping for the writer."""
        self._writer.set_template_mapping(self._template_context)

    def __exit__(self, *args):
        # type: (*str) -> None
        """Clear the template mapping for the writer."""
        self._writer.clear_template_mapping()


class _EmptyBlock(object):
    """Do not generate an indented block."""

    def __init__(self):
        # type: () -> None
        """Create an empty block."""
        pass

    def __enter__(self):
        # type: () -> None
        """Do nothing."""
        pass

    def __exit__(self, *args):
        # type: (*str) -> None
        """Do nothing."""
        pass


class _UnindentedScopedBlock(object):
    """Generate an unindented block, and do not indent the contents."""

    def __init__(self, writer, opening, closing):
        # type: (_IndentedTextWriter, unicode, unicode) -> None
        """Create a block."""
        self._writer = writer
        self._opening = opening
        self._closing = closing

    def __enter__(self):
        # type: () -> None
        """Write the beginning of the block and do not indent."""
        self._writer.write_unindented_line(self._opening)

    def __exit__(self, *args):
        # type: (*str) -> None
        """Write the end of the block and do not change indentation."""
        self._writer.write_unindented_line(self._closing)


class _IndentedScopedBlock(object):
    """Generate a block, template the parameters, and indent the contents."""

    def __init__(self, writer, opening, closing):
        # type: (_IndentedTextWriter, unicode, unicode) -> None
        """Create a block."""
        self._writer = writer
        self._opening = opening
        self._closing = closing

    def __enter__(self):
        # type: () -> None
        """Write the beginning of the block and then indent."""
        self._writer.write_template(self._opening)
        self._writer.indent()

    def __exit__(self, *args):
        # type: (*str) -> None
        """Unindent the block and print the ending."""
        self._writer.unindent()
        self._writer.write_template(self._closing)


class _FieldUsageChecker(object):
    """Check for duplicate fields, and required fields as needed."""

    def __init__(self, writer):
        # type: (_IndentedTextWriter) -> None
        """Create a field usage checker."""
        self._writer = writer  # type: _IndentedTextWriter
        self.fields = []  # type: List[ast.Field]

        # TODO: use a more optimal data type
        self._writer.write_line('std::set<StringData> usedFields;')

    def add_store(self):
        # type: () -> None
        """Create the C++ field store initialization code."""
        self._writer.write_line('auto push_result = usedFields.insert(fieldName);')
        with _IndentedScopedBlock(self._writer, 'if (push_result.second == false) {', '}'):
            self._writer.write_line('ctxt.throwDuplicateField(element);')

    def add(self, field):
        # type: (ast.Field) -> None
        """Add a field to track."""
        self.fields.append(field)

    def add_final_checks(self):
        # type: () -> None
        """Output the code to check for missing fields."""
        for field in self.fields:
            if (not field.optional) and (not field.ignore):
                with _IndentedScopedBlock(self._writer,
                                          'if (usedFields.find("%s") == usedFields.end()) {' %
                                          (field.name), '}'):
                    if field.default:
                        self._writer.write_line('object.%s = %s;' %
                                                (_get_field_member_name(field), field.default))
                    else:
                        self._writer.write_line('ctxt.throwMissingField("%s");' % (field.name))


class _CppFileWriterBase(object):
    """
    C++ File writer.

    Encapsulates low level knowledge of how to print a C++ file.
    Relies on caller to orchestrate calls correctly though.
    """

    def __init__(self, writer):
        # type: (_IndentedTextWriter) -> None
        """Create a C++ code writer."""
        self._writer = writer  # type: _IndentedTextWriter

    def write_unindented_line(self, msg):
        # type: (unicode) -> None
        """Write an unindented line to the stream."""
        self._writer.write_unindented_line(msg)

    def write_empty_line(self):
        # type: () -> None
        """Write an empty line to the stream."""
        self._writer.write_empty_line()

    def gen_file_header(self):
        # type: () -> None
        """Generate a file header saying the file is generated."""
        self._writer.write_unindented_line(
            textwrap.dedent("""\
        /**
         * WARNING: This is a generated file. Do not modify.
         *
         * Source: %s
         */
            """ % (" ".join(sys.argv))))

    def gen_system_include(self, include):
        # type: (unicode) -> None
        """Generate a system C++ include line."""
        self._writer.write_unindented_line('#include <%s>' % (include))

    def gen_include(self, include):
        # type: (unicode) -> None
        """Generate a non-system C++ include line."""
        self._writer.write_unindented_line('#include "%s"' % (include))

    def gen_namespace_block(self, namespace):
        # type: (unicode) -> _UnindentedScopedBlock
        """Generate a namespace block."""
        # TODO: support namespace strings which consist of '::' delimited namespaces
        return _UnindentedScopedBlock(self._writer, 'namespace %s {' % (namespace),
                                      '}  // namespace %s' % (namespace))

    def gen_description_comment(self, description):
        # type: (unicode) -> None
        """Generate a multiline comment with the description from the IDL."""
        self._writer.write_line(
            textwrap.dedent("""\
        /**
         * %s
         */""" % (description)))

    def _with_template(self, template_params):
        # type: (Mapping[unicode,unicode]) -> _TemplateContext
        """Generate a template context for the current parameters."""
        return _TemplateContext(self._writer, template_params)

    def _block(self, opening, closing):
        # type: (unicode, unicode) -> Union[_IndentedScopedBlock,_EmptyBlock]
        """Generate an indented block if opening is not empty."""
        if not opening:
            return _EmptyBlock()

        return _IndentedScopedBlock(self._writer, opening, closing)

    def _predicate(self, check_str, use_else_if=False):
        # type: (unicode, bool) -> Union[_IndentedScopedBlock,_EmptyBlock]
        """
        Generate an if block if the condition is not-empty.

        Generate 'else if' instead of use_else_if is True.
        """
        if not check_str:
            return _EmptyBlock()

        conditional = 'if'
        if use_else_if:
            conditional = 'else if'

        return _IndentedScopedBlock(self._writer, '%s (%s) {' % (conditional, check_str), '}')


class _CppHeaderFileWriter(_CppFileWriterBase):
    """C++ .h File writer."""

    def __init__(self, writer):
        # type: (_IndentedTextWriter) -> None
        """Create a C++ .cpp file code writer."""
        super(_CppHeaderFileWriter, self).__init__(writer)

    def gen_class_declaration_block(self, class_name):
        # type: (unicode) -> _IndentedScopedBlock
        """Generate a class declaration block."""
        return _IndentedScopedBlock(self._writer, 'class %s {' % _title_case(class_name), '};')

    def gen_serializer_methods(self, class_name):
        # type: (unicode) -> None
        """Generate a serializer method declarations."""
        self._writer.write_line(
            'static %s parse(const IDLParserErrorContext& ctxt, const BSONObj& object);' %
            (_title_case(class_name)))
        self._writer.write_line('void serialize(BSONObjBuilder* builder) const;')
        self._writer.write_empty_line()

    def gen_getter(self, field):
        # type: (ast.Field) -> None
        """Generate the C++ getter definition for a field."""
        cpp_type = _get_field_cpp_type(field)
        param_type = _get_field_getter_setter_type(field)
        member_name = _get_field_member_name(field)

        optional_ampersand = ""
        if _get_return_by_reference(field):
            optional_ampersand = "&"

        disable_xvalue = _get_disable_xvalue(field)

        if not _is_view_type(cpp_type):
            body_template = 'return $member_name;'
        else:
            if field.array:
                # Delegate to a function to the do the transformation between vectors.
                if field.optional:
                    body_template = """\
                    if (${member_name}.is_initialized()) {
                        return transformVector<std::string, StringData>(${member_name}.get());
                    } else {
                        return boost::none;
                    }
                    """
                else:
                    body_template = 'return transformVector<std::string, StringData>(${member_name});'
            else:
                body_template = 'return ${param_type}{${member_name}};'

        template_params = {
            'method_name': _title_case(field.name),
            'member_name': member_name,
            'optional_ampersand': optional_ampersand,
            'param_type': param_type,
        }

        body = _template_format(body_template, template_params)

        # Generate a getter that disables xvalue for view types (i.e. StringData), constructed
        # optional types, and non-primitive types.
        template_params['body'] = body

        with self._with_template(template_params):

            if disable_xvalue:
                self._writer.write_template(
                    'const ${param_type}${optional_ampersand} get${method_name}() const& { ${body} }'
                )
                self._writer.write_template(
                    'const ${param_type}${optional_ampersand} get${method_name}() && = delete;')
            else:
                self._writer.write_template(
                    'const ${param_type}${optional_ampersand} get${method_name}() const { ${body} }')

    def gen_setter(self, field):
        # type: (ast.Field) -> None
        """Generate the C++ setter definition for a field."""
        cpp_type = _get_field_cpp_type(field)
        param_type = _get_field_getter_setter_type(field)
        member_name = _get_field_member_name(field)

        template_params = {
            'method_name': _title_case(field.name),
            'member_name': member_name,
            'param_type': param_type,
        }

        if _is_view_type(cpp_type):
            template_params['view_to_base_method'] = _get_view_type_to_base_method(cpp_type)

            with self._with_template(template_params):

                if field.array:
                    if not field.optional:
                        self._writer.write_template(
                            'void set${method_name}(${param_type} value) & { ${member_name} = transformVector<StringData, std::string>(value); }'
                        )

                    else:
                        # We need to convert between two different types of optional<T> and yet provide
                        # the ability for the user to specific an uninitialized optional. This occurs
                        # for vector<mongo::StringData> and vector<std::string> paired together.
                        with self._block('void set${method_name}(${param_type} value) & {', "}"):
                            self._writer.write_template(
                                textwrap.dedent("""\
                            if (value.is_initialized()) {
                                ${member_name} = transformVector<StringData, std::string>(value.get());
                            } else {
                                ${member_name} = boost::none;
                            }
                            """))
                else:
                    if not field.optional:
                        self._writer.write_template(
                            'void set${method_name}(${param_type} value) & { ${member_name} = value.${view_to_base_method}(); }'
                        )

                    else:
                        # We need to convert between two different types of optional<T> and yet provide
                        # the ability for the user to specific an uninitialized optional. This occurs
                        # for mongo::StringData and std::string paired together.
                        with self._block('void set${method_name}(${param_type} value) & {', "}"):
                            self._writer.write_template(
                                textwrap.dedent("""\
                            if (value.is_initialized()) {
                                ${member_name} = value.get().${view_to_base_method}();
                            } else {
                                ${member_name} = boost::none;
                            }
                            """))

        else:
            with self._with_template(template_params):
                self._writer.write_template(
                    'void set${method_name}(${param_type} value) & { ${member_name} = std::move(value); }'
                )

        self._writer.write_empty_line()

    def gen_member(self, field):
        # type: (ast.Field) -> None
        """Generate the C++ class member definition for a field."""
        member_type = _get_field_storage_type(field)
        member_name = _get_field_member_name(field)

        # TODO: make this smarter
        optional_initializer = ""
        if "mongo::ConstDataRange" == member_type:
            optional_initializer = "{nullptr, nullptr}"

        self._writer.write_line('%s %s%s;' % (member_type, member_name, optional_initializer))

    def generate(self, spec):
        # type: (ast.IDLAST) -> None
        """Generate the C++ header to a stream."""
        self.gen_file_header()

        self._writer.write_unindented_line('#pragma once')
        self.write_empty_line()

        # Generate system includes first
        header_list = [
            'algorithm',
            'boost/optional.hpp',
            'cstdint',
            'string',
            'vector',
        ]

        header_list.sort()

        for include in header_list:
            self.gen_system_include(include)

        self.write_empty_line()

        # Generate user includes second
        header_list = [
            'mongo/base/string_data.h',
            'mongo/bson/bsonobj.h',
            'mongo/bson/bsonobjbuilder.h',
            'mongo/idl/idl_parser.h',
        ] + spec.globals.cpp_includes

        header_list.sort()

        for include in header_list:
            self.gen_include(include)

        self.write_empty_line()

        # Generate namesapce
        with self.gen_namespace_block(spec.globals.cpp_namespace):
            self.write_empty_line()

            for struct in spec.structs:
                self.gen_description_comment(struct.description)
                with self.gen_class_declaration_block(struct.name):
                    self.write_unindented_line('public:')

                    # Write constructor
                    self.gen_serializer_methods(struct.name)

                    # Write getters & setters
                    for field in struct.fields:
                        if not field.ignore:
                            if field.description:
                                self.gen_description_comment(field.description)
                            self.gen_getter(field)
                            self.gen_setter(field)

                    self.write_unindented_line('private:')

                    # Write member variables
                    for field in struct.fields:
                        if not field.ignore:
                            self.gen_member(field)

                self.write_empty_line()


class _CppSourceFileWriter(_CppFileWriterBase):
    """C++ .cpp File writer."""

    def __init__(self, writer):
        # type: (_IndentedTextWriter) -> None
        """Create a C++ .cpp file code writer."""
        super(_CppSourceFileWriter, self).__init__(writer)

    def _gen_field_deserializer_top(self, element_name, field):
        # type: (unicode, ast.Field) -> unicode
        """
        Generate the C++ deserializer piece for a field.

        Make print multiple lines into the generated file.
        Returns the final statement to access the deserialized value.
        """

        if field.struct_type:
            self._writer.write_line('IDLParserErrorContext tempContext("%s", &ctxt);' %
                                    (field.name))
            self._writer.write_line('const auto localObject = %s.Obj();' % (element_name))
            return '%s::parse(tempContext, localObject);' % (_title_case(field.struct_type))
        elif 'BSONElement::' in field.deserializer:
            method_name = _get_method_name(field.deserializer)
            return '%s.%s()' % (element_name, method_name)
        else:
            # Custom method, call the method on object.
            # TODO: avoid this string hack in the future
            if len(field.bson_serialization_type) == 1 and field.bson_serialization_type[
                    0] == 'string':
                # Call a method like: Class::method(StringData value)
                self._writer.write_line('auto tempValue = %s.valueStringData();' % (element_name))

                method_name = _get_method_name(field.deserializer)

                return '%s(tempValue)' % (method_name)
            elif len(field.bson_serialization_type) == 1 and field.bson_serialization_type[
                    0] == 'object':
                # Call a method like: Class::method(const BSONObj& value)
                method_name = _get_method_name_from_qualified_method_name(field.deserializer)
                self._writer.write_line('const BSONObj localObject = %s.Obj();' % (element_name))
                return '%s(localObject)' % (method_name)
            else:
                # Call a method like: Class::method(const BSONElement& value)
                method_name = _get_method_name_from_qualified_method_name(field.deserializer)

                return '%s(%s)' % (method_name, element_name)

    def _gen_array_deserializer(self, field):
        # type: (ast.Field) -> None
        """Generate the C++ deserializer piece for an array field."""
        cpp_type = _get_field_cpp_type(field)

        self._writer.write_line('std::uint32_t expectedFieldNumber{0};')
        self._writer.write_line('const IDLParserErrorContext arrayCtxt("%s", &ctxt);' %
                                (field.name))
        self._writer.write_line('std::vector<%s> values;' % (cpp_type))
        self._writer.write_empty_line()

        self._writer.write_line('const BSONObj arrayObject = element.Obj();')

        with self._block('for (const auto& arrayElement : arrayObject) {', '}'):

            self._writer.write_line(
                'const auto arrayFieldName = arrayElement.fieldNameStringData();')
            self._writer.write_line('std::uint32_t fieldNumber;')
            self._writer.write_empty_line()

            # Check the array field names are integers
            self._writer.write_line(
                'Status status = parseNumberFromString(arrayFieldName, &fieldNumber);')
            with self._predicate('status.isOK()'):

                # Check that the array field names are sequential
                with self._predicate('fieldNumber != expectedFieldNumber'):
                    self._writer.write_line('arrayCtxt.throwBadArrayFieldNumberSequence(' +
                                            'fieldNumber, expectedFieldNumber);')

                with self._predicate(_get_bson_type_check('arrayElement', 'arrayCtxt', field)):
                    array_value = self._gen_field_deserializer_top('arrayElement', field)
                    self._writer.write_line('values.emplace_back(%s);' % (array_value))
            with self._block('else {', '}'):
                self._writer.write_line('arrayCtxt.throwBadArrayFieldNumberValue(arrayFieldName);')
            self._writer.write_line('++expectedFieldNumber;')

        self._writer.write_line('object.%s = std::move(values);' % (_get_field_member_name(field)))

    def gen_field_deserializer(self, field):
        # type: (ast.Field) -> None
        """Generate the C++ deserializer piece for a field."""
        if field.array:
            self._gen_array_deserializer(field)
            return

        # May be an empty block if the type is any
        with self._predicate(_get_bson_type_check('element', 'ctxt', field)):

            object_value = self._gen_field_deserializer_top('element', field)
            self._writer.write_line('object.%s = %s;' %
                                    (_get_field_member_name(field), object_value))

    def gen_deserializer_method(self, struct):
        # type: (ast.Struct) -> None
        """Generate the C++ deserializer method definition."""

        func_def = '%s %s::parse(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject)' % (
            _title_case(struct.name), _title_case(struct.name))
        with self._block('%s {' % (func_def), '}'):

            self._writer.write_line('%s object;' % _title_case(struct.name))

            field_usage_check = _FieldUsageChecker(self._writer)
            self._writer.write_empty_line()

            with self._block('for (const auto& element : bsonObject) {', '}'):

                self._writer.write_line('const auto fieldName = element.fieldNameStringData();')
                self._writer.write_empty_line()

                # TODO: generate command namespace string check
                field_usage_check.add_store()
                self._writer.write_empty_line()

                first_field = True
                for field in struct.fields:
                    field_predicate = 'fieldName == "%s"' % (field.name)
                    field_usage_check.add(field)

                    with self._predicate(field_predicate, not first_field):
                        if field.ignore:
                            self._writer.write_line('// ignore field')
                        else:
                            self.gen_field_deserializer(field)

                    if first_field:
                        first_field = False

                # End of for fields
                # Generate strict check for extranous fields
                if struct.strict:
                    with self._block('else {', '}'):
                        self._writer.write_line('ctxt.throwUnknownField(fieldName);')

            self._writer.write_empty_line()

            # Check for required fields
            field_usage_check.add_final_checks()
            self._writer.write_empty_line()

            self._writer.write_line('return object;')

    def _gen_serializer_method_custom(self, field):
        # type: (ast.Field) -> None
        """Generate the serialize method definition for a custom type."""

        # Generate custom serialization
        method_name = _get_method_name(field.serializer)

        template_params = {
            'field_name': field.name,
            'method_name': method_name,
            'access_member': _access_member(field),
        }

        with self._with_template(template_params):

            if len(field.bson_serialization_type) == 1 and \
                field.bson_serialization_type[0] == 'string':
                # TODO: expand this out to be less then a string only hack

                if field.array:
                    self._writer.write_template(
                        'BSONArrayBuilder arrayBuilder(builder->subarrayStart("${field_name}"));')
                    with self._block('for (const auto& item : ${access_member}) {', '}'):
                        self._writer.write_template('auto tempValue = item.${method_name}();')
                        self._writer.write_line('arrayBuilder.append(std::move(tempValue));')
                else:
                    self._writer.write_template(
                        'auto tempValue = ${access_member}.${method_name}();')
                    self._writer.write_template(
                        'builder->append("${field_name}", std::move(tempValue));')
            elif len(field.bson_serialization_type) == 1 and \
                field.bson_serialization_type[0] == 'bindata':
                # TODO: expand this out to be less then a bindata only hack
                self._writer.write_line('ConstDataRange tempCDR = %s.%s();' %
                                        (_access_member(field), method_name))
                self._writer.write_line(
                    'builder->appendBinData("%s", tempCDR.length(), %s, tempCDR.data());' 
                        % (field.name, bson.cpp_bindata_subtype_type_name(field.bindata_subtype)))
            else:
                if field.array:
                    self._writer.write_template(
                        'BSONArrayBuilder arrayBuilder(builder->subarrayStart("${field_name}"));')
                    with self._block('for (const auto& item : ${access_member}) {', '}'):
                        self._writer.write_line(
                            'BSONObjBuilder subObjBuilder(arrayBuilder.subobjStart());')
                        self._writer.write_template('item.${method_name}(&subObjBuilder);')
                else:
                    if field.bson_serialization_type[0] == 'bindata':
                        # Generate default serialization using BSONObjBuilder::append
                        if field.cpp_type == "mongo::ConstDataRange":
                            self._writer.write_line('builder->appendBinData("%s", %s.length(), %s, %s.data());' %
                                                (field.name, _access_member(field), bson.cpp_bindata_subtype_type_name(field.bindata_subtype),_access_member(field) ))
                        else:
                            self._writer.write_line('builder->appendBinData("%s", %s.size(), %s, %s.data());' %
                                                (field.name, _access_member(field), bson.cpp_bindata_subtype_type_name(field.bindata_subtype),_access_member(field) ))
                            
                    else:
                        self._writer.write_template('${access_member}.${method_name}(builder);')

    def _gen_serializer_method_struct(self, field):
        # type: (ast.Field) -> None
        """Generate the serialize method definition for a struct type."""

        template_params = {
            'field_name': field.name,
            'access_member': _access_member(field),
        }

        with self._with_template(template_params):

            if field.array:
                self._writer.write_template(
                    'BSONArrayBuilder arrayBuilder(builder->subarrayStart(""${field_name}"));')
                with self._block('for (const auto& item : ${access_member}) {', '}'):
                    self._writer.write_line(
                        'BSONObjBuilder subObjBuilder(arrayBuilder.subobjStart());')
                    self._writer.write_line('item.serialize(&subObjBuilder);')
            else:
                self._writer.write_template(
                    'BSONObjBuilder subObjBuilder(builder->subobjStart("${field_name}"));')
                self._writer.write_template('${access_member}.serialize(&subObjBuilder);')

    def gen_serializer_method(self, struct):
        # type: (ast.Struct) -> None
        """Generate the serialize method definition."""

        with self._block('void %s::serialize(BSONObjBuilder* builder) const {' %
                         _title_case(struct.name), '}'):

            for field in struct.fields:
                # If fields are meant to be ignored during deserialization, there is not need to serialize them
                if field.ignore:
                    continue

                member_name = _get_field_member_name(field)

                optional_block_start = None
                if field.optional:
                    optional_block_start = 'if (%s) {' % (member_name)
                elif field.struct_type or field.serializer:
                    # Introduce a new scope for required nested object serialization.
                    optional_block_start = '{'

                with self._block(optional_block_start, '}'):

                    if not field.struct_type:
                        if field.serializer:
<<<<<<< HEAD
                            self._gen_serializer_method_custom(field)
                        else:
                            # Generate default serialization using BSONObjBuilder::append
                            # Note: BSONObjBuilder::append has overrides for std::vector also
                            self._writer.write_line('builder->append("%s", %s);' %
=======
                            # Generate custom serialization
                            method_name = _get_method_name(field.serializer)

                            if len(field.bson_serialization_type) == 1 and \
                                field.bson_serialization_type[0] == 'string':
                                # TODO: expand this out to be less then a string only hack
                                self._writer.write_line('auto tempValue = %s.%s();' %
                                                        (_access_member(field), method_name))
                                self._writer.write_line(
                                    'builder->append("%s", std::move(tempValue));' % (field.name))
                            elif len(field.bson_serialization_type) == 1 and \
                                field.bson_serialization_type[0] == 'bindata':
                                # TODO: expand this out to be less then a bindata only hack
                                self._writer.write_line('ConstDataRange tempCDR = %s.%s();' %
                                                        (_access_member(field), method_name))
                                self._writer.write_line(
                                    'builder->appendBinData("%s", tempCDR.length(), %s, tempCDR.data());' 
                                        % (field.name, bson.cpp_bindata_subtype_type_name(field.bindata_subtype)))
                            else:
                                self._writer.write_line('%s.%s(builder);' %
                                                        (_access_member(field), method_name))

                        else:
                            if field.bson_serialization_type[0] == 'bindata':
                                # Generate default serialization using BSONObjBuilder::append
                                if field.cpp_type == "mongo::ConstDataRange":
                                    self._writer.write_line('builder->appendBinData("%s", %s.length(), %s, %s.data());' %
                                                        (field.name, _access_member(field), bson.cpp_bindata_subtype_type_name(field.bindata_subtype),_access_member(field) ))
                                else:
                                    self._writer.write_line('builder->appendBinData("%s", %s.size(), %s, %s.data());' %
                                                        (field.name, _access_member(field), bson.cpp_bindata_subtype_type_name(field.bindata_subtype),_access_member(field) ))
                                    
                            else:
                                # Generate default serialization using BSONObjBuilder::append
                                self._writer.write_line('builder->append("%s", %s);' %
>>>>>>> ba89460... BinData for IDL
                                                    (field.name, _access_member(field)))
                    else:
                        self._gen_serializer_method_struct(field)

                # Add a blank line after each block
                self._writer.write_empty_line()

    def generate(self, spec, header_file_name):
        # type: (ast.IDLAST, unicode) -> None
        """Generate the C++ header to a stream."""
        self.gen_file_header()

        # Generate include for generated header first
        self.gen_include(header_file_name)
        self.write_empty_line()

        # Generate system includes second
        self.gen_system_include('set')
        self.write_empty_line()

        # Generate mongo includes third
        self.gen_include('mongo/bson/bsonobjbuilder.h')
        self.write_empty_line()

        # Generate namesapce
        with self.gen_namespace_block(spec.globals.cpp_namespace):
            self.write_empty_line()

            for struct in spec.structs:
                # Write deserializer
                self.gen_deserializer_method(struct)
                self.write_empty_line()

                # Write serializer
                self.gen_serializer_method(struct)
                self.write_empty_line()


def _generate_header(spec, file_name):
    # type: (ast.IDLAST, unicode) -> None
    """Generate a C++ header."""
    stream = io.StringIO()
    text_writer = _IndentedTextWriter(stream)

    header = _CppHeaderFileWriter(text_writer)

    header.generate(spec)

    # Generate structs
    with io.open(file_name, mode='wb') as file_handle:
        file_handle.write(stream.getvalue().encode())


def _generate_source(spec, file_name, header_file_name):
    # type: (ast.IDLAST, unicode, unicode) -> None
    """Generate a C++ source file."""
    stream = io.StringIO()
    text_writer = _IndentedTextWriter(stream)

    source = _CppSourceFileWriter(text_writer)

    source.generate(spec, header_file_name)

    # Generate structs
    with io.open(file_name, mode='wb') as file_handle:
        file_handle.write(stream.getvalue().encode())


def generate_code(spec, output_base_dir, header_file_name, source_file_name):
    # type: (ast.IDLAST, unicode, unicode, unicode) -> None
    """Generate a C++ header and source file from an idl.ast tree."""

    _generate_header(spec, header_file_name)

    if output_base_dir:
        include_h_file_name = os.path.relpath(
            os.path.normpath(header_file_name), os.path.normpath(output_base_dir))
    else:
        include_h_file_name = os.path.abspath(header_file_name)

    # Normalize to POSIX style for consistency across Windows and POSIX.
    include_h_file_name = include_h_file_name.replace("\\", "/")

    _generate_source(spec, source_file_name, include_h_file_name)
