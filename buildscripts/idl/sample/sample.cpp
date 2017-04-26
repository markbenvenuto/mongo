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
    std::set<StringData> usedFields;

    for (const auto& element : bsonObject) {
        const auto fieldName = element.fieldNameStringData();

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
                object._nsfield = NamespaceString(element.valueStringData());
            }
        } else if (fieldName == "optionalField") {
            if (ctxt.checkAndAssertType(element, String)) {
                object._optionalField = element.str();
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
            object._vectorField = std::move(values);
        } else if (fieldName == "binDataField") {
            if (ctxt.checkAndAssertBinDataType(element, BinDataGeneral)) {
                object._binDataField = element._binDataVector();
            }
        } else if (fieldName == "uuidField") {
            if (ctxt.checkAndAssertBinDataType(element, newUUID)) {
                object._uuidField = element.uuid();
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
    if (usedFields.find("vectorField") == usedFields.end()) {
        ctxt.throwMissingField("vectorField");
    }
    if (usedFields.find("binDataField") == usedFields.end()) {
        ctxt.throwMissingField("binDataField");
    }
    if (usedFields.find("uuidField") == usedFields.end()) {
        ctxt.throwMissingField("uuidField");
    }

    return object;
}

void Default_values::serialize(BSONObjBuilder* builder) const {
    builder->append("stringfield", _stringfield);

    builder->append("intfield", _intfield);

    builder->append("numericfield", _numericfield);

    { builder->append("nsfield", _nsfield.toString()); }

    if (_optionalField) {
        builder->append("optionalField", _optionalField.get());
    }

    { builder->append("vectorField", _vectorField); }

    {
        ConstDataRange tempCDR = makeCDR(_binDataField);
        builder->append("binDataField",
                        BSONBinData(tempCDR.data(), tempCDR.length(), BinDataGeneral));
    }

    {
        ConstDataRange tempCDR = makeCDR(_uuidField);
        builder->append("uuidField", BSONBinData(tempCDR.data(), tempCDR.length(), newUUID));
    }
}

}  // namespace mongo
