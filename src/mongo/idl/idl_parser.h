#pragma once

#include <string>

#include "mongo/db/namespace_string.h"
#include "mongo/stdx/memory.h"
#include "mongo/base/status_with.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/util/net/hostandport.h"

namespace mongo {

class IDLParserErrorContext {
public:
    IDLParserErrorContext push_back(StringData fieldName);
    void assertNotEmptyObject(StringData fieldName);
    void assertType(const BSONElement&, BSONType, StringData fieldName);
    void assertIsNumber(const BSONElement&, StringData str);
    void throwUnknownField(const BSONElement&, StringData str);
    void throwMissingRequiredField(StringData str);
    //NamespaceString parseCommandNamespace(BSONElement&, StringData str);
};

}  //namespace mongo
