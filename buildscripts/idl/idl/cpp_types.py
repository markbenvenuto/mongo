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

from abc import ABCMeta, abstractmethod
import string
import textwrap
from typing import Any, List, Mapping, Union, cast

from . import ast
from . import common



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
        cpp_type = common.title_case(field.struct_type)
    else:
        cpp_type = field.cpp_type

    return cpp_type


def _qualify_optional_type(cpp_type):
    # type: (unicode) -> unicode
    """Qualify the type as optional."""
    return 'boost::optional<%s>' % (cpp_type)


def _qualify_array_type(cpp_type):
    # type: (unicode) -> unicode
    """Qualify the type if the field is an array."""
    return "std::vector<%s>" % (cpp_type)


def _get_field_getter_setter_type(field):
    # type: (ast.Field) -> unicode
    """Get the C++ type name for the getter/setter parameter for a field."""
    assert field.cpp_type is not None or field.struct_type is not None

    cpp_type = _get_field_cpp_type(field)

    return _get_view_type(cpp_type)


def _get_field_storage_type(field):
    # type: (ast.Field) -> unicode
    """Get the C++ type name for the storage of class member for a field."""
    return _get_field_cpp_type(field)


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


class CppTypeBase(object):
    """Base type for C++ Type information."""

    __metaclass__ = ABCMeta

    def __init__(self, field):
        # type: (ast.Field) -> None
        self._field = field

    @abstractmethod
    def get_type_name(self):
        # type: () -> unicode
        pass

    @abstractmethod
    def get_storage_type(self):
        # type: () -> unicode
        # _get_field_storage_type
        pass

    @abstractmethod
    def get_getter_setter_type(self):
        # type: () -> unicode
        # _get_field_getter_setter_type
        pass

    @abstractmethod
    def return_by_reference(self):
        # type: () -> bool
        # _get_return_by_reference
        pass

    @abstractmethod
    def disable_xvalue(self):
        # type: () -> bool
        # _get_disable_xvalue
        pass

    @abstractmethod
    def is_view_type(self):
        # type: () -> bool
        # _is_view_type
        pass
    
    @abstractmethod
    def get_view_type_to_base_method(self):
        # type: () -> unicode
        # _get_view_type_to_base_method
        pass

    @abstractmethod
    def needs_cast_to(self):
        # type: () -> bool
        return False

    @abstractmethod
    def get_getter_body(self, member_name):
        # type: (unicode) -> unicode
        pass


class CppTypeBasic(CppTypeBase):
    """Base type for C++ Type information."""

    def __init__(self, field):
        # type: (ast.Field) -> None
        self._field = field

    def get_type_name(self):
        # type: () -> unicode
        return _get_field_cpp_type(self._field)

    def get_storage_type(self):
        # type: () -> unicode
        # _get_field_storage_type
        return _get_field_storage_type(self._field)

    def get_getter_setter_type(self):
        # type: () -> unicode
        # _get_field_getter_setter_type
        return _get_field_getter_setter_type(self._field)

    def return_by_reference(self):
        # type: () -> bool
        # _get_return_by_reference
        return not _is_primitive_type(self.get_type_name())

    def disable_xvalue(self):
        # type: () -> bool
        # _get_disable_xvalue
        return False

    def is_view_type(self):
        # type: () -> bool
        # _is_view_type
        return _is_view_type(self.get_type_name())
    
    def get_view_type_to_base_method(self):
        # type: () -> unicode
        # _get_view_type_to_base_method
        return _get_view_type_to_base_method(self.get_type_name())


    def needs_cast_to(self):
        # type: () -> bool
        return False

    def get_getter_body(self, member_name):
        # type: (unicode) -> unicode
        return common.template_args('return $member_name;', member_name=member_name)

class CppTypeView(CppTypeBase):
    """Base type for C++ Type information."""

    def __init__(self, field, storage_type, view_type):
        # type: (ast.Field, unicode, unicode) -> None
        self._storage_type = storage_type
        self._view_type = view_type
        super(CppTypeView, self).__init__(field)

    def get_type_name(self):
        # type: () -> unicode
        return self._storage_type

    def get_storage_type(self):
        # type: () -> unicode
        # _get_field_storage_type
        return self._storage_type

    def get_getter_setter_type(self):
        # type: () -> unicode
        # _get_field_getter_setter_type
        return self._view_type

    def return_by_reference(self):
        # type: () -> bool
        # _get_return_by_reference
        return False

    def disable_xvalue(self):
        # type: () -> bool
        # _get_disable_xvalue
        return True

    def is_view_type(self):
        # type: () -> bool
        # _is_view_type
        return True
    
    def get_view_type_to_base_method(self):
        # type: () -> unicode
        # _get_view_type_to_base_method
        return "toString"

    def needs_cast_to(self):
        # type: () -> bool
        return True

    def get_getter_body(self, member_name):
        # type: (unicode) -> unicode
        return common.template_args('return $member_name;', member_name=member_name)

class CppTypeDelegating(CppTypeBase):
    """Base type for C++ Type information."""

    def __init__(self, base, field):
        # type: (CppTypeBase, ast.Field) -> None
        self._base = base
        super(CppTypeDelegating, self).__init__(field)

    def get_type_name(self):
        # type: () -> unicode
        return self._base.get_type_name()

    def get_storage_type(self):
        # type: () -> unicode
        return self._base.get_storage_type()

    def get_getter_setter_type(self):
        # type: () -> unicode
        return self._base.get_getter_setter_type()

    def return_by_reference(self):
        # type: () -> bool
        return self._base.return_by_reference()

    def disable_xvalue(self):
        # type: () -> bool
        return self._base.disable_xvalue()

    def is_view_type(self):
        # type: () -> bool
        return self._base.is_view_type()
    
    def get_view_type_to_base_method(self):
        # type: () -> unicode
        return self._base.get_view_type_to_base_method()

    def needs_cast_to(self):
        # type: () -> bool
        return self._base.needs_cast_to()
        
    def get_getter_body(self, member_name):
        # type: (unicode) -> unicode
        return self._base.get_getter_body(member_name)

class CppTypeArray(CppTypeDelegating):
    """Base type for C++ Type information."""

    def __init__(self, base, field):
        # type: (CppTypeBase, ast.Field) -> None
        super(CppTypeArray, self).__init__(base, field)

    def get_storage_type(self):
        # type: () -> unicode
        # _get_field_storage_type
        return _qualify_array_type(self._base.get_storage_type())

    def get_getter_setter_type(self):
        # type: () -> unicode
        # _get_field_getter_setter_type
        return _qualify_array_type(self._base.get_getter_setter_type())

    def return_by_reference(self):
        # type: () -> bool
        if self._base.is_view_type():
            return False
        return True

    def disable_xvalue(self):
        # type: () -> bool
        return True

    def get_getter_body(self, member_name):
        # type: (unicode) -> unicode
        convert = self.get_vector_transform()
        if convert:
            return common.template_args(
                'return ${convert}(${member_name});', convert=convert, member_name=member_name)
        else:
            return self._base.get_getter_body(member_name)

    def get_vector_transform(self):
        # type: () -> unicode
        if( self._base.get_storage_type() != self._base.get_getter_setter_type()):
            return common.template_args(
                'transformVector<${storage_type}, ${param_type}>', 
                storage_type = self._base.get_storage_type(),
                param_type = self._base.get_getter_setter_type(),
                )
        else:
            return None

class CppTypeOptional(CppTypeDelegating):
    """Base type for C++ Type information."""

    def __init__(self, base, field):
        # type: (CppTypeBase, ast.Field) -> None
        super(CppTypeOptional, self).__init__(base, field)

    def get_storage_type(self):
        # type: () -> unicode
        # _get_field_storage_type
        return _qualify_optional_type(self._base.get_storage_type())

    def get_getter_setter_type(self):
        # type: () -> unicode
        # _get_field_getter_setter_type
        return _qualify_optional_type(self._base.get_getter_setter_type())

    def disable_xvalue(self):
        # type: () -> bool
        return True

    def return_by_reference(self):
        # type: () -> bool
        return False

    def get_getter_body(self, member_name):
        # type: (unicode) -> unicode
        if self._field.array:
            array_type = cast(CppTypeArray, self._base)
            return common.template_args(
                textwrap.dedent("""\
                if (${member_name}.is_initialized()) {
                    return ${convert}(${member_name}.get());
                } else {
                    return boost::none;
                }
                """), convert = array_type.get_vector_transform(), member_name=member_name)
        else:
            return common.template_args('return ${param_type}{${member_name}};', 
                param_type = self.get_getter_setter_type(),
                member_name=member_name)


def get_cpp_type(field):
    # type: (ast.Field) -> CppTypeBase
    """Get the C++ Type information for the given field."""

    cpp_type_info = None # type: Any

    if field.cpp_type == 'std::string':
        cpp_type_info = CppTypeView(field, 'std::string', 'StringData')
    #elif field.cpp_type == 'std::vector<std::uint8_t>':
    #    cpp_type_info = CppTypeView(field, 'std::vector<std::uint8_t>', 'ConstDataRange')
    else:
        cpp_type_info = CppTypeBasic(field)

    if field.array:
        cpp_type_info = CppTypeArray(cpp_type_info, field)

    if field.optional:
        cpp_type_info = CppTypeOptional(cpp_type_info, field)

    return cpp_type_info