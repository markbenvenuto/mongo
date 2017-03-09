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
    IDLParserErrorContext(StringData fieldName) : _currentField(fieldName), _predecessor(nullptr) {}
    IDLParserErrorContext(StringData fieldName, IDLParserErrorContext* predecessor)
        : _currentField(fieldName), _predecessor(predecessor) {}
    void throwNotEmptyObject();
    
    void assertType(const BSONElement& element, BSONType type); 
    void assertBinDataType(const BSONElement& element, BinDataType type);

    bool checkAndAssertType(const BSONElement& element, BSONType type);

    //template< std::size_t N>
    //void assertTypes(const BSONElement& element, std::array<BSONType, N> types) 
    bool checkAndAssertTypes(const BSONElement& element, std::vector<BSONType> types);

    void throwDuplicateField(const  BSONElement& element);
    void throwMissingField(StringData fieldName);

    void throwUnknownField(StringData fieldName);
    void throwMissingRequiredField(StringData str);
    //NamespaceString parseCommandNamespace(BSONElement&, StringData str);

private:
    std::string getElementPath(const  BSONElement& element);
    std::string getElementPath(StringData fieldName);

private:
    StringData _currentField;
    IDLParserErrorContext* _predecessor;
};

}  //namespace mongo
