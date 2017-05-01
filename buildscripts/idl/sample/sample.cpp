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

/**
 * An example int enum
 */
IntEnum IntEnum_parse(const IDLParserErrorContext& ctxt, std::int32_t value) {

    if (!(value >= static_cast<std::underlying_type<IntEnum>::type>(IntEnum::a0) &&
          value <= static_cast<std::underlying_type<IntEnum>::type>(IntEnum::c2))) {
        ctxt.throwBadEnumValue(value);
        return IntEnum::a0;
    } else {
        return static_cast<IntEnum>(value);
    }
}

std::int32_t IntEnum_serializer(IntEnum value) {
    return static_cast<std::int32_t>(value);
}

/**
 * An example string enum
 */
StringEnumEnum StringEnum_parse(const IDLParserErrorContext& ctxt, StringData value) {
    if (value == "zero") {
        return StringEnumEnum::s0;
    }
    if (value == "one") {
        return StringEnumEnum::s1;
    }
    if (value == "two") {
        return StringEnumEnum::s2;
    }
    ctxt.throwBadEnumValue(value);
    return StringEnumEnum::s0;
}

StringData StringEnum_serializer(StringEnumEnum value) {
    if (value == StringEnumEnum::s0) {
        return "zero";
    }
    if (value == StringEnumEnum::s1) {
        return "one";
    }
    if (value == StringEnumEnum::s2) {
        return "two";
    }
    MONGO_UNREACHABLE;
    return StringData();
}

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
        } else if (fieldName == "intEnumField") {
            if (ctxt.checkAndAssertType(element, NumberInt)) {
                IDLParserErrorContext tempContext("intEnumField", &ctxt);
                _intEnumField = IntEnum_parse(tempContext, element._numberInt());
            }
        } else if (fieldName == "stringEnumField") {
            if (ctxt.checkAndAssertType(element, String)) {
                IDLParserErrorContext tempContext("stringEnumField", &ctxt);
                _stringEnumField = StringEnum_parse(tempContext, element.valueStringData());
            }
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
    if (usedFields.find("intEnumField") == usedFields.end()) {
        ctxt.throwMissingField("intEnumField");
    }
    if (usedFields.find("stringEnumField") == usedFields.end()) {
        ctxt.throwMissingField("stringEnumField");
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

    { builder->append("intEnumField", IntEnum_serializer(_intEnumField)); }

    { builder->append("stringEnumField", StringEnum_serializer(_stringEnumField)); }
}

}  // namespace mongo
