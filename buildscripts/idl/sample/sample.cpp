/**
 * WARNING: This is a generated file. Do not modify.
 *
 * Source: .\idlc.py --header .\sample\sample.h -o .\sample\sample.cpp .\sample\sample.idl
 */

#include "D:\mongo\buildscripts\idl\sample\sample.h"

#include "mongo/bson/bsonobjbuilder.h"

namespace mongo {

Default_values Default_values::parse(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {
    Default_values object;
    std::set<StringData> usedFields;

    for (const auto&& element : bsonObject) {
        const auto& fieldName = element.fieldNameStringData();

        auto push_result = usedFields.insert(fieldName);
        if (push_result.second == false) {
            ctxt.throwDuplicateField(element);
        }

        if (fieldName == "stringfield") {
            if (ctxt.checkAndAssertType(element, String)) {
                object._stringfield = element.str();
            }
        } else if (fieldName == "intfield") {
            if (ctxt.checkAndAssertType(element, NumberInt)) {
                object._intfield = element._numberInt();
            }
        } else if (fieldName == "numericfield") {
            if (ctxt.checkAndAssertTypes(element,
                                         {NumberLong, NumberInt, NumberDecimal, NumberDouble})) {
                object._numericfield = element.numberInt();
            }
        } else if (fieldName == "nsfield") {
            if (ctxt.checkAndAssertType(element, String)) {
                auto tempValue = element.valueStringData();
                object._nsfield = NamespaceString(tempValue);
            }
        } else if (fieldName == "optionalField") {
            if (ctxt.checkAndAssertType(element, String)) {
                object._optionalField = element.str();
            }
        } else {
            ctxt.throwUnknownField(fieldName);
        }
    }

    if (usedFields.find("stringfield") == usedFields.end()) {
        object._stringfield = "a default";
    }
    if (usedFields.find("intfield") == usedFields.end()) {
        object._intfield = 42;
    }
    if (usedFields.find("numericfield") == usedFields.end()) {
        ctxt.throwMissingField("numericfield");
    }
    if (usedFields.find("nsfield") == usedFields.end()) {
        ctxt.throwMissingField("nsfield");
    }

    return object;
}

void Default_values::serialize(BSONObjBuilder* builder) const {
    builder->append("stringfield", _stringfield);

    builder->append("intfield", _intfield);

    builder->append("numericfield", _numericfield);

    auto tempValue = _nsfield.toString();
    builder->append("nsfield", tempValue);

    if (_optionalField) {
        builder->append("optionalField", _optionalField.get());
    }
}

}  // namespace mongo
