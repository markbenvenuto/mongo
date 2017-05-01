/**
 * WARNING: This is a generated file. Do not modify.
 *
 * Source: .\idlc.py --base_dir d:\mongo\buildscripts --header .\sample\sample.h -o
 * .\sample\sample.cpp .\sample\sample.idl
 */

#include "idl/sample/sample.h"

#include <set>

#include "mongo/bson/bsonobjbuilder.h"

namespace mongo {

Default_values Default_values::parse(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {
    Default_values object;
    object.parseProtected(ctxt, bsonObject);
    return object;
}
void Default_values::parseProtected(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {
    std::set<StringData> usedFields;

    for (const auto& element : bsonObject) {
        const auto fieldName = element.fieldNameStringData();

        auto push_result = usedFields.insert(fieldName);
        if (push_result.second == false) {
            ctxt.throwDuplicateField(element);
        }

        if (fieldName == "stringfield") {
            if (ctxt.checkAndAssertType(element, String)) {
                _stringfield = element.str();
            }
        } else if (fieldName == "intfield") {
            if (ctxt.checkAndAssertType(element, NumberInt)) {
                _intfield = element._numberInt();
            }
        } else if (fieldName == "numericfield") {
            if (ctxt.checkAndAssertTypes(element,
                                         {NumberLong, NumberInt, NumberDecimal, NumberDouble})) {
                _numericfield = element.numberInt();
            }
        } else if (fieldName == "nsfield") {
            if (ctxt.checkAndAssertType(element, String)) {
                _nsfield = NamespaceString(element.valueStringData());
            }
        } else if (fieldName == "optionalField") {
            if (ctxt.checkAndAssertType(element, String)) {
                _optionalField = element.str();
            }
        } else if (fieldName == "vectorField") {
            std::uint32_t expectedFieldNumber{0};
            const IDLParserErrorContext arrayCtxt("vectorField", &ctxt);
            std::vector<std::int32_t> values;

            const BSONObj arrayObject = element.Obj();
            for (const auto& arrayElement : arrayObject) {
                const auto arrayFieldName = arrayElement.fieldNameStringData();
                std::uint32_t fieldNumber;

                Status status = parseNumberFromString(arrayFieldName, &fieldNumber);
                if (status.isOK()) {
                    if (fieldNumber != expectedFieldNumber) {
                        arrayCtxt.throwBadArrayFieldNumberSequence(fieldNumber,
                                                                   expectedFieldNumber);
                    }

                    if (arrayCtxt.checkAndAssertType(arrayElement, NumberInt)) {
                        values.emplace_back(arrayElement._numberInt());
                    }
                } else {
                    arrayCtxt.throwBadArrayFieldNumberValue(arrayFieldName);
                }
                ++expectedFieldNumber;
            }
            _vectorField = std::move(values);
        } else {
            ctxt.throwUnknownField(fieldName);
        }
    }

    if (usedFields.find("stringfield") == usedFields.end()) {
        _stringfield = "a default";
    }
    if (usedFields.find("intfield") == usedFields.end()) {
        _intfield = 42;
    }
    if (usedFields.find("numericfield") == usedFields.end()) {
        ctxt.throwMissingField("numericfield");
    }
    if (usedFields.find("nsfield") == usedFields.end()) {
        ctxt.throwMissingField("nsfield");
    }
    if (usedFields.find("vectorField") == usedFields.end()) {
        ctxt.throwMissingField("vectorField");
    }
}

void Default_values::serialize(BSONObjBuilder* builder) const {
    builder->append("stringfield", _stringfield);

    builder->append("intfield", _intfield);

    builder->append("numericfield", _numericfield);

    { builder->append("nsfield", _nsfield.toString()); }

    if (_optionalField.is_initialized()) {
        builder->append("optionalField", _optionalField.get());
    }

    { builder->append("vectorField", _vectorField); }
}

BasicIgnoredCommand BasicIgnoredCommand::parse(const IDLParserErrorContext& ctxt,
                                               const BSONObj& bsonObject) {
    BasicIgnoredCommand object;
    object.parseProtected(ctxt, bsonObject);
    return object;
}
void BasicIgnoredCommand::parseProtected(const IDLParserErrorContext& ctxt,
                                         const BSONObj& bsonObject) {
    std::set<StringData> usedFields;
    bool firstFieldFound = false;

    for (const auto& element : bsonObject) {
        const auto fieldName = element.fieldNameStringData();

        if (firstFieldFound == false) {
            firstFieldFound = true;
            continue;
        }
        auto push_result = usedFields.insert(fieldName);
        if (push_result.second == false) {
            ctxt.throwDuplicateField(element);
        }

        if (fieldName == "field1") {
            if (ctxt.checkAndAssertType(element, NumberInt)) {
                _field1 = element._numberInt();
            }
        } else if (fieldName == "field2") {
            if (ctxt.checkAndAssertType(element, String)) {
                _field2 = element.str();
            }
        } else {
            ctxt.throwUnknownField(fieldName);
        }
    }

    if (usedFields.find("field1") == usedFields.end()) {
        ctxt.throwMissingField("field1");
    }
    if (usedFields.find("field2") == usedFields.end()) {
        ctxt.throwMissingField("field2");
    }
}

void BasicIgnoredCommand::serialize(BSONObjBuilder* builder) const {
    builder->append("BasicIgnoredCommand", 1);
    builder->append("field1", _field1);

    builder->append("field2", _field2);
}

BasicConcatenateWithDbCommand BasicConcatenateWithDbCommand::parse(
    const IDLParserErrorContext& ctxt, StringData dbName, const BSONObj& bsonObject) {
    BasicConcatenateWithDbCommand object;
    object.parseProtected(ctxt, dbName, bsonObject);
    return object;
}
void BasicConcatenateWithDbCommand::parseProtected(const IDLParserErrorContext& ctxt,
                                                   StringData dbName,
                                                   const BSONObj& bsonObject) {
    std::set<StringData> usedFields;
    bool firstFieldFound = false;

    for (const auto& element : bsonObject) {
        const auto fieldName = element.fieldNameStringData();

        if (firstFieldFound == false) {
            _ns = ctxt.parseNSCollectionRequired(dbName, element);
            firstFieldFound = true;
            continue;
        }
        auto push_result = usedFields.insert(fieldName);
        if (push_result.second == false) {
            ctxt.throwDuplicateField(element);
        }

        if (fieldName == "field1") {
            if (ctxt.checkAndAssertType(element, NumberInt)) {
                _field1 = element._numberInt();
            }
        } else if (fieldName == "field2") {
            if (ctxt.checkAndAssertType(element, String)) {
                _field2 = element.str();
            }
        } else {
            ctxt.throwUnknownField(fieldName);
        }
    }

    if (usedFields.find("field1") == usedFields.end()) {
        ctxt.throwMissingField("field1");
    }
    if (usedFields.find("field2") == usedFields.end()) {
        ctxt.throwMissingField("field2");
    }
}

void BasicConcatenateWithDbCommand::serialize(const NamespaceString ns,
                                              BSONObjBuilder* builder) const {
    builder->append("BasicConcatenateWithDbCommand", ns.toString());
    builder->append("field1", _field1);

    builder->append("field2", _field2);
}

}  // namespace mongo
