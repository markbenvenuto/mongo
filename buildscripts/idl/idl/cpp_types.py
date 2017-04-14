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
from typing import Any, Optional

from . import ast
from . import common



def _is_primitive_type(cpp_type):
    # type: (unicode) -> bool
    """Return True if a cpp_type is a primitive type and should not be returned as reference."""
    return cpp_type in [
        'bool', 'double', 'std::int32_t', 'std::uint32_t', 'std::uint64_t', 'std::int64_t'
    ]


def _get_view_type_to_base_method(cpp_type):
    # type: (unicode) -> unicode
    """Map a C++ View type to its C++ base type."""
    if cpp_type == 'std::vector<std::uint8_t>':
        return "True"

    return "toString"


def _qualify_optional_type(cpp_type):
    # type: (unicode) -> unicode
    """Qualify the type as optional."""
    return 'boost::optional<%s>' % (cpp_type)


def _qualify_array_type(cpp_type):
    # type: (unicode) -> unicode
    """Qualify the type if the field is an array."""
    return "std::vector<%s>" % (cpp_type)


class CppTypeBase(object):
    """Base type for C++ Type information."""

    __metaclass__ = ABCMeta

    def __init__(self, field):
        # type: (ast.Field) -> None
        """Construct a CppTypeBase."""
        self._field = field

    @abstractmethod
    def get_type_name(self):
        # type: () -> unicode
        """Get the C++ type name for a field."""
        pass

    @abstractmethod
    def get_storage_type(self):
        # type: () -> unicode
        """Get the C++ type name for the storage of class member for a field."""
        pass

    @abstractmethod
    def get_getter_setter_type(self):
        # type: () -> unicode
        """Get the C++ type name for the getter/setter parameter for a field."""
        pass

    @abstractmethod
    def return_by_reference(self):
        # type: () -> bool
        """Return True if the type should be returned by reference."""
        pass

    @abstractmethod
    def disable_xvalue(self):
        # type: () -> bool
        """Return True if the type should have the xvalue getter disabled."""
        pass

    @abstractmethod
    def is_view_type(self):
        # type: () -> bool
        """Return True if the C++ is returned as a view type from an IDL class."""
        pass
    
    @abstractmethod
    def get_getter_body(self, member_name):
        # type: (unicode) -> unicode
        """Get the body of the getter."""
        pass

    @abstractmethod
    def get_setter_body(self, member_name):
        # type: (unicode) -> unicode
        """Get the body of the setter."""
        pass

    @abstractmethod
    def get_transform_to_getter_type(self, expression):
        # type: (unicode) -> Optional[unicode]
        """Get the expression to transform the input expression into the getter type."""
        pass

    @abstractmethod
    def get_transform_to_storage_type(self, expression):
        # type: (unicode) -> Optional[unicode]
        """Get the expression to transform the input expression into the setter type."""
        pass

class _CppTypeBasic(CppTypeBase):
    """Base type for C++ Type information."""

    def __init__(self, field):
        # type: (ast.Field) -> None
        """Construct a CppTypeBasic."""
        self._field = field

    def get_type_name(self):
        # type: () -> unicode
        """Get the C++ type name for a field."""
        if self._field.struct_type:
            cpp_type = common.title_case(self._field.struct_type)
        else:
            cpp_type = self._field.cpp_type

        return cpp_type

    def get_storage_type(self):
        # type: () -> unicode
        """Get the C++ type name for the storage of class member for a field."""
        return self.get_type_name()

    def get_getter_setter_type(self):
        # type: () -> unicode
        """Get the C++ type name for the getter/setter parameter for a field."""
        return self.get_type_name()

    def return_by_reference(self):
        # type: () -> bool
        """Return True if the type should be returned by reference."""
        return not _is_primitive_type(self.get_type_name())

    def disable_xvalue(self):
        # type: () -> bool
        """Return True if the type should have the xvalue getter disabled."""
        return False

    def is_view_type(self):
        # type: () -> bool
        """Return True if the C++ is returned as a view type from an IDL class."""
        return False
    
    def get_getter_body(self, member_name):
        # type: (unicode) -> unicode
        """Get the body of the getter."""
        return common.template_args('return $member_name;', member_name=member_name)

    def get_setter_body(self, member_name):
        # type: (unicode) -> unicode
        """Get the body of the setter."""
        return common.template_args('${member_name} = std::move(value);', member_name=member_name)

    def get_transform_to_getter_type(self, expression):
        # type: (unicode) -> Optional[unicode]
        """Get the expression to transform the input expression into the getter type."""
        return None

    def get_transform_to_storage_type(self, expresion):
        # type: (unicode) -> Optional[unicode]
        """Get the expression to transform the input expression into the setter type."""
        return None

class _CppTypeView(CppTypeBase):
    """Base type for C++ View Types information."""

    def __init__(self, field, storage_type, view_type):
        # type: (ast.Field, unicode, unicode) -> None
        self._storage_type = storage_type
        self._view_type = view_type
        super(_CppTypeView, self).__init__(field)

    def get_type_name(self):
        # type: () -> unicode
        return self._storage_type

    def get_storage_type(self):
        # type: () -> unicode
        return self._storage_type

    def get_getter_setter_type(self):
        # type: () -> unicode
        return self._view_type

    def return_by_reference(self):
        # type: () -> bool
        return False

    def disable_xvalue(self):
        # type: () -> bool
        return True

    def is_view_type(self):
        # type: () -> bool
        return True
    
    def get_getter_body(self, member_name):
        # type: (unicode) -> unicode
        return common.template_args('return $member_name;', member_name=member_name)

    def get_setter_body(self, member_name):
        # type: (unicode) -> unicode
        return common.template_args('$member_name = ${value};', member_name=member_name, value = self.get_transform_to_storage_type("value"))

    def get_transform_to_getter_type(self, expression):
        # type: (unicode) -> Optional[unicode]
        return None

    def get_transform_to_storage_type(self, expression):
        # type: (unicode) -> Optional[unicode]
        return common.template_args(
            '$expression.toString()', 
            expression = expression,
            )

class _CppTypeDelegating(CppTypeBase):
    """Delegating to contained type."""

    def __init__(self, base, field):
        # type: (CppTypeBase, ast.Field) -> None
        self._base = base
        super(_CppTypeDelegating, self).__init__(field)

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
    
    def get_getter_body(self, member_name):
        # type: (unicode) -> unicode
        return self._base.get_getter_body(member_name)

    def get_setter_body(self, member_name):
        # type: (unicode) -> unicode
        return self._base.get_setter_body(member_name)

    def get_transform_to_getter_type(self, expression):
        # type: (unicode) -> Optional[unicode]
        return self._base.get_transform_to_getter_type(expression)

    def get_transform_to_storage_type(self, expression):
        # type: (unicode) -> Optional[unicode]
        return self._base.get_transform_to_storage_type(expression)


class _CppTypeArray(_CppTypeDelegating):
    """C++ Array type for wrapping a base C++ Type information."""

    def __init__(self, base, field):
        # type: (CppTypeBase, ast.Field) -> None
        super(_CppTypeArray, self).__init__(base, field)

    def get_storage_type(self):
        # type: () -> unicode
        return _qualify_array_type(self._base.get_storage_type())

    def get_getter_setter_type(self):
        # type: () -> unicode
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
        convert = self.get_transform_to_getter_type(member_name)
        if convert:
            return common.template_args(
                'return ${convert};', convert=convert)
        else:
            return self._base.get_getter_body(member_name)

    def get_setter_body(self, member_name):
        # type: (unicode) -> unicode
        convert = self.get_transform_to_storage_type("value")
        if convert:
            return common.template_args(
                '${member_name} = ${convert};', member_name=member_name, convert=convert)
        else:
            return self._base.get_getter_body("value")

    def get_transform_to_getter_type(self, expression):
        # type: (unicode) -> Optional[unicode]
        if( self._base.get_storage_type() != self._base.get_getter_setter_type()):
            return common.template_args(
                'transformVector<${storage_type}, ${param_type}>($expression)', 
                storage_type = self._base.get_storage_type(),
                param_type = self._base.get_getter_setter_type(),
                expression = expression,
                )
        else:
            return None


    def get_transform_to_storage_type(self, expression):
        # type: (unicode) -> Optional[unicode]
        if( self._base.get_storage_type() != self._base.get_getter_setter_type()):
            return common.template_args(
                'transformVector<${param_type}, ${storage_type}>($expression)', 
                storage_type = self._base.get_storage_type(),
                param_type = self._base.get_getter_setter_type(),
                expression = expression,
                )
        else:
            return None


class _CppTypeOptional(_CppTypeDelegating):
    """Base type for Optional C++ Type information which wraps C++ types."""

    def __init__(self, base, field):
        # type: (CppTypeBase, ast.Field) -> None
        super(_CppTypeOptional, self).__init__(base, field)

    def get_storage_type(self):
        # type: () -> unicode
        return _qualify_optional_type(self._base.get_storage_type())

    def get_getter_setter_type(self):
        # type: () -> unicode
        return _qualify_optional_type(self._base.get_getter_setter_type())

    def disable_xvalue(self):
        # type: () -> bool
        return True

    def return_by_reference(self):
        # type: () -> bool
        return False

    def get_getter_body(self, member_name):
        # type: (unicode) -> unicode
        base_expression = common.template_args("${member_name}.get()", member_name=member_name)

        convert = self._base.get_transform_to_getter_type(base_expression)
        if convert:
            # We need to convert between two different types of optional<T> and yet provide
            # the ability for the user to specific an uninitialized optional. This occurs
            # for vector<mongo::StringData> and vector<std::string> paired together.
            return common.template_args(
                textwrap.dedent("""\
                if (${member_name}.is_initialized()) {
                    return ${convert};
                } else {
                    return boost::none;
                }
                """), member_name=member_name, convert = convert)
        else:
            return common.template_args('return ${param_type}{${member_name}};', 
                param_type = self.get_getter_setter_type(),
                member_name=member_name)

    def get_setter_body(self, member_name):
        # type: (unicode) -> unicode
        convert = self._base.get_transform_to_storage_type("value.get()")
        if convert:
            return common.template_args(
                                                textwrap.dedent("""\
                            if (value.is_initialized()) {
                                ${member_name} = ${convert};
                            } else {
                                ${member_name} = boost::none;
                            }
                            """),  member_name=member_name, convert = convert)
        else:
            return self._base.get_setter_body(member_name)

def get_cpp_type(field):
    # type: (ast.Field) -> CppTypeBase
    """Get the C++ Type information for the given field."""

    cpp_type_info = None # type: Any

    if field.cpp_type == 'std::string':
        cpp_type_info = _CppTypeView(field, 'std::string', 'StringData')
    #elif field.cpp_type == 'std::vector<std::uint8_t>':
    #    cpp_type_info = CppTypeView(field, 'std::vector<std::uint8_t>', 'ConstDataRange')
    else:
        cpp_type_info = _CppTypeBasic(field)

    if field.array:
        cpp_type_info = _CppTypeArray(cpp_type_info, field)

    if field.optional:
        cpp_type_info = _CppTypeOptional(cpp_type_info, field)

    return cpp_type_info
