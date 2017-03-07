#include <string>
#include <queue>

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
    std::string path = getElementPath();
    uasserted(65001, str::stream() << "Object '" << getElementPath() << "' is not allowed to be empty");
}

void IDLParserErrorContext::assertType(const BSONElement& element, BSONType type) {
    if (element.type() != type) {
        std::string path = getElementPath();
        uasserted(65002, str::stream() << "BSON field '" << getElementPath() << "' is the wrong type '" 
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
            
        std::string path = getElementPath();
        uasserted(65003, str::stream() << "BSON field '" << getElementPath() << "' is the wrong type '"
            << typeName(element.type()) << "', expected type '" << type << "'");
    }

    return true;
}

void IDLParserErrorContext::assertBinDataType(const BSONElement& element, BinDataType type) {
    assertType(element, BinData);

    if (element.binDataType() != type) {
        std::string path = getElementPath();
        uasserted(65004, str::stream() << "BSON field '" << getElementPath() << "' is the wrong bindData type '"
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

        std::string path = getElementPath();
        std::string type_str = "";
        uasserted(65005, str::stream() << "BSON field '" << getElementPath() << "' is the wrong type '"
            << typeName(element.type()) << "', expected types '" << type_str << "'");
    }

    return true;
}

std::string IDLParserErrorContext::getElementPath() {
    if (_predecessor == nullptr) {
        return _currentField.toString();
    } else {
        std::queue<StringData> pieces;
        pieces.push(_currentField);
        
        IDLParserErrorContext* head = _predecessor;
        while (head) {
            pieces.push(head->_currentField);
            head = head->_predecessor;
        }

        str::stream builder;
        while (!pieces.empty()) {
            builder << pieces.front();
            pieces.pop();

            if (!pieces.empty()) {
                builder << ".";
            }
        }

        return builder;
    }
}

}  //namespace mongo