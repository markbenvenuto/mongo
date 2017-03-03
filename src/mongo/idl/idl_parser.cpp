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
    std::string path = getElementPath();
    uasserted(65001, str::stream() << "Object '" << getElementPath() << "' is not allowed to be empty");
}

std::string IDLParserErrorContext::getElementPath() {
    if (_predecessor == nullptr) {
        return _currentField.toString();
    } else {
        std::stack<StringData> pieces;
        pieces.push(_currentField);
        
        IDLParserErrorContext* head = _predecessor;
        while (head) {
            pieces.push(head->_currentField);
            head = head->_predecessor;
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

}  //namespace mongo