#include <string>
#include <stack>

#include "mongo/idl/idl_parser.h"

#include "mongo/db/namespace_string.h"
#include "mongo/stdx/memory.h"
#include "mongo/base/status_with.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/util/net/hostandport.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

void IDLParserErrorContext::throwNotEmptyObject() {
    std::string path = getElementPath(StringData());
    uasserted(65001, str::stream() << "Object '" << path << "' is not allowed to be empty");
}

void IDLParserErrorContext::assertType(const BSONElement& element, BSONType type) {
    if (element.type() != type) {
        std::string path = getElementPath(element);
        uasserted(65002, str::stream() << "BSON field '" <<path << "' is the wrong type '" 
            << typeName(element.type()) << "', expected type '"<< type <<  "'");
    }
}

bool IDLParserErrorContext::checkAndAssertType(const BSONElement& element, BSONType type) {
    auto elementType = element.type();

    if (elementType != type) {
        // If the type is wrong, ignore Null and Undefined values
        if (elementType == jstNULL || elementType == Undefined) {
            return false;
        }
            
        std::string path = getElementPath(element);
        uasserted(65003, str::stream() << "BSON field '" << path << "' is the wrong type '"
            << typeName(element.type()) << "', expected type '" << type << "'");
    }

    return true;
}

void IDLParserErrorContext::assertBinDataType(const BSONElement& element, BinDataType type) {
    assertType(element, BinData);

    if (element.binDataType() != type) {
        std::string path = getElementPath(element);
        uasserted(65004, str::stream() << "BSON field '" << path << "' is the wrong bindData type '"
            << typeName(element.type()) << "', expected type '" << type << "'");
    }
}

bool IDLParserErrorContext::checkAndAssertTypes(const BSONElement& element, std::vector<BSONType> types) {
    auto elementType = element.type();

    auto pos = std::find(types.begin(), types.end(), elementType);
    if (pos == types.end()) {
        // If the type is wrong, ignore Null and Undefined values
        if (elementType == jstNULL || elementType == Undefined) {
            return false;
        }

        std::string path = getElementPath(element);
        std::string type_str = "";
        uasserted(65005, str::stream() << "BSON field '" << path << "' is the wrong type '"
            << typeName(element.type()) << "', expected types '" << type_str << "'");
    }

    return true;
}


std::string IDLParserErrorContext::getElementPath(const  BSONElement& element) {
    return getElementPath(element.fieldNameStringData());
}

std::string IDLParserErrorContext::getElementPath(StringData fieldName) {
    if (_predecessor == nullptr) {
        str::stream builder;
        builder << _currentField;

        if (!fieldName.empty()) {
            builder << "." << fieldName;
        }

        return builder;
    } else {
        std::stack<StringData> pieces;
        pieces.push(_currentField);
        
        IDLParserErrorContext* head = _predecessor;
        while (head) {
            pieces.push(head->_currentField);
            head = head->_predecessor;
        }

        if (!fieldName.empty()) {
            pieces.push(fieldName);
        }

        str::stream builder;
        while (!pieces.empty()) {
            builder << pieces.top();
            pieces.pop();

            if (!pieces.empty()) {
                builder << ".";
            }
        }

        return builder;
    }
}

void IDLParserErrorContext::throwDuplicateField(const  BSONElement& element) {
    std::string path = getElementPath(element);
    uasserted(65013, str::stream() << "BSON field '" << path << "' is a duplicate field");

}
void IDLParserErrorContext::throwMissingField(StringData fieldName) {
    std::string path = getElementPath(fieldName);
    uasserted(65014, str::stream() << "BSON field '" << path << "' is missing but required");

}

}  //namespace mongo