/**
 * WARNING: This is a generated file. Do not modify.
 *
 * Source: .\idlc.py --include d:\mongo\src --base_dir d:\mongo\buildscripts --header
 * .\sample\sample.h -o .\sample\sample.cpp .\sample\sample.idl
 */

#include "idl/sample/sample.h"

#include <bitset>
#include <set>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/commands.h"

namespace mongo {

constexpr StringData Default_values::kBinDataFieldFieldName;
constexpr StringData Default_values::kIntfieldFieldName;
constexpr StringData Default_values::kNsfieldFieldName;
constexpr StringData Default_values::kNumericfieldFieldName;
constexpr StringData Default_values::kOptionalFieldFieldName;
constexpr StringData Default_values::kStringfieldFieldName;
constexpr StringData Default_values::kUuidFieldFieldName;
constexpr StringData Default_values::kVectorFieldFieldName;


Default_values::Default_values()
    : _numericfield(-1),
      _hasNumericfield(false),
      _hasNsfield(false),
      _hasVectorField(false),
      _hasBinDataField(false),
      _hasUuidField(false) {
    // Used for initialization only
}

Default_values Default_values::parse(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {
    Default_values object;
    object.parseProtected(ctxt, bsonObject);
    return object;
}
void Default_values::parseProtected(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {
    std::bitset<8> usedFields;
    const size_t kStringfieldBit = 0;
    const size_t kIntfieldBit = 1;
    const size_t kNumericfieldBit = 2;
    const size_t kNsfieldBit = 3;
    const size_t kOptionalFieldBit = 4;
    const size_t kVectorFieldBit = 5;
    const size_t kBinDataFieldBit = 6;
    const size_t kUuidFieldBit = 7;

    for (const auto& element : bsonObject) {
        const auto fieldName = element.fieldNameStringData();


        if (fieldName == kStringfieldFieldName) {
            if (MONGO_unlikely(usedFields[kStringfieldBit])) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kStringfieldBit);

            if (MONGO_likely(ctxt.checkAndAssertType(element, String))) {
                _stringfield = element.str();
            }
        } else if (fieldName == kIntfieldFieldName) {
            if (MONGO_unlikely(usedFields[kIntfieldBit])) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kIntfieldBit);

            if (MONGO_likely(ctxt.checkAndAssertType(element, NumberInt))) {
                _intfield = element._numberInt();
            }
        } else if (fieldName == kNumericfieldFieldName) {
            if (MONGO_unlikely(usedFields[kNumericfieldBit])) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kNumericfieldBit);

            _hasNumericfield = true;
            if (MONGO_likely(ctxt.checkAndAssertTypes(
                    element, {NumberLong, NumberInt, NumberDecimal, NumberDouble}))) {
                _numericfield = element.numberInt();
            }
        } else if (fieldName == kNsfieldFieldName) {
            if (MONGO_unlikely(usedFields[kNsfieldBit])) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kNsfieldBit);

            _hasNsfield = true;
            if (MONGO_likely(ctxt.checkAndAssertType(element, String))) {
                _nsfield = NamespaceString(element.valueStringData());
            }
        } else if (fieldName == kOptionalFieldFieldName) {
            if (MONGO_unlikely(usedFields[kOptionalFieldBit])) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kOptionalFieldBit);

            if (MONGO_likely(ctxt.checkAndAssertType(element, String))) {
                _optionalField = element.str();
            }
        } else if (fieldName == kVectorFieldFieldName) {
            if (MONGO_unlikely(usedFields[kVectorFieldBit])) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kVectorFieldBit);

            _hasVectorField = true;
            std::uint32_t expectedFieldNumber{0};
            const IDLParserErrorContext arrayCtxt(kVectorFieldFieldName, &ctxt);
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
        } else if (fieldName == kBinDataFieldFieldName) {
            if (MONGO_unlikely(usedFields[kBinDataFieldBit])) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kBinDataFieldBit);

            _hasBinDataField = true;
            if (MONGO_likely(ctxt.checkAndAssertBinDataType(element, BinDataGeneral))) {
                _binDataField = element._binDataVector();
            }
        } else if (fieldName == kUuidFieldFieldName) {
            if (MONGO_unlikely(usedFields[kUuidFieldBit])) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kUuidFieldBit);

            _hasUuidField = true;
            if (MONGO_likely(ctxt.checkAndAssertBinDataType(element, newUUID))) {
                _uuidField = element.uuid();
            }
        } else {
            ctxt.throwUnknownField(fieldName);
        }
    }


    if (MONGO_unlikely(!usedFields.all())) {
        if (!usedFields[kStringfieldBit]) {
            _stringfield = "a default";
        }
        if (!usedFields[kIntfieldBit]) {
            _intfield = 42;
        }
        if (!usedFields[kNumericfieldBit]) {
            ctxt.throwMissingField(kNumericfieldFieldName);
        }
        if (!usedFields[kNsfieldBit]) {
            ctxt.throwMissingField(kNsfieldFieldName);
        }
        if (!usedFields[kVectorFieldBit]) {
            ctxt.throwMissingField(kVectorFieldFieldName);
        }
        if (!usedFields[kBinDataFieldBit]) {
            ctxt.throwMissingField(kBinDataFieldFieldName);
        }
        if (!usedFields[kUuidFieldBit]) {
            ctxt.throwMissingField(kUuidFieldFieldName);
        }
    }
}


void Default_values::serialize(BSONObjBuilder* builder) const {
    invariant(_hasNumericfield && _hasNsfield && _hasVectorField && _hasBinDataField &&
              _hasUuidField);

    builder->append(kStringfieldFieldName, _stringfield);

    builder->append(kIntfieldFieldName, _intfield);

    builder->append(kNumericfieldFieldName, _numericfield);

    { builder->append(kNsfieldFieldName, _nsfield.toString()); }

    if (_optionalField.is_initialized()) {
        builder->append(kOptionalFieldFieldName, _optionalField.get());
    }

    { builder->append(kVectorFieldFieldName, _vectorField); }

    {
        ConstDataRange tempCDR = makeCDR(_binDataField);
        builder->append(kBinDataFieldFieldName,
                        BSONBinData(tempCDR.data(), tempCDR.length(), BinDataGeneral));
    }

    {
        ConstDataRange tempCDR = makeCDR(_uuidField);
        builder->append(kUuidFieldFieldName,
                        BSONBinData(tempCDR.data(), tempCDR.length(), newUUID));
    }
}


BSONObj Default_values::toBSON() const {
    BSONObjBuilder builder;
    serialize(&builder);
    return builder.obj();
}

}  // namespace mongo
