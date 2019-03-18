/**
 * WARNING: This is a generated file. Do not modify.
 *
 * Source: idl/idlc.py idl/sample/sample.idl
 */

#include "mongo/platform/basic.h"

#include "/home/mark/mongo/buildscripts/idl/sample/sample_gen.h"

#include <bitset>
#include <set>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/command_generic_argument.h"
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


Default_values::Default_values() : _numericfield(-1), _hasNumericfield(false), _hasNsfield(false), _hasVectorField(false), _hasBinDataField(false), _hasUuidField(false) {
    // Used for initialization only
}
Default_values::Default_values(std::int64_t numericfield, mongo::NamespaceString nsfield, std::vector<std::int32_t> vectorField, std::vector<std::uint8_t> binDataField, std::array<std::uint8_t, 16> uuidField) : _numericfield(std::move(numericfield)), _nsfield(std::move(nsfield)), _vectorField(std::move(vectorField)), _binDataField(std::move(binDataField)), _uuidField(std::move(uuidField)), _hasNumericfield(true), _hasNsfield(true), _hasVectorField(true), _hasBinDataField(true), _hasUuidField(true) {
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

    for (const auto& element :bsonObject) {
        const auto fieldName = element.fieldNameStringData();


        if (fieldName == kStringfieldFieldName) {
            if (MONGO_likely(ctxt.checkAndAssertType(element, String))) {
                if (MONGO_unlikely(usedFields[kStringfieldBit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kStringfieldBit);

                _stringfield = element.str();
            }
        }
        else if (fieldName == kIntfieldFieldName) {
            if (MONGO_likely(ctxt.checkAndAssertType(element, NumberInt))) {
                if (MONGO_unlikely(usedFields[kIntfieldBit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kIntfieldBit);

                _intfield = element._numberInt();
            }
        }
        else if (fieldName == kNumericfieldFieldName) {
            if (MONGO_likely(ctxt.checkAndAssertTypes(element, {NumberLong, NumberInt, NumberDecimal, NumberDouble}))) {
                if (MONGO_unlikely(usedFields[kNumericfieldBit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kNumericfieldBit);

                _hasNumericfield = true;
                _numericfield = element.numberInt();
            }
        }
        else if (fieldName == kNsfieldFieldName) {
            if (MONGO_likely(ctxt.checkAndAssertType(element, String))) {
                if (MONGO_unlikely(usedFields[kNsfieldBit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kNsfieldBit);

                _hasNsfield = true;
                _nsfield = NamespaceString(element.valueStringData());
            }
        }
        else if (fieldName == kOptionalFieldFieldName) {
            if (MONGO_likely(ctxt.checkAndAssertType(element, String))) {
                if (MONGO_unlikely(usedFields[kOptionalFieldBit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kOptionalFieldBit);

                _optionalField = element.str();
            }
        }
        else if (fieldName == kVectorFieldFieldName) {
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
                        arrayCtxt.throwBadArrayFieldNumberSequence(fieldNumber, expectedFieldNumber);
                    }

                    if (arrayCtxt.checkAndAssertType(arrayElement, NumberInt)) {
                        values.emplace_back(arrayElement._numberInt());
                    }
                }
                else {
                    arrayCtxt.throwBadArrayFieldNumberValue(arrayFieldName);
                }
                ++expectedFieldNumber;
            }
            _vectorField = std::move(values);
        }
        else if (fieldName == kBinDataFieldFieldName) {
            if (MONGO_likely(ctxt.checkAndAssertBinDataType(element, BinDataGeneral))) {
                if (MONGO_unlikely(usedFields[kBinDataFieldBit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kBinDataFieldBit);

                _hasBinDataField = true;
                _binDataField = element._binDataVector();
            }
        }
        else if (fieldName == kUuidFieldFieldName) {
            if (MONGO_likely(ctxt.checkAndAssertBinDataType(element, newUUID))) {
                if (MONGO_unlikely(usedFields[kUuidFieldBit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kUuidFieldBit);

                _hasUuidField = true;
                _uuidField = element.uuid();
            }
        }
        else {
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
    invariant(_hasNumericfield && _hasNsfield && _hasVectorField && _hasBinDataField && _hasUuidField);

    builder->append(kStringfieldFieldName, _stringfield);

    builder->append(kIntfieldFieldName, _intfield);

    builder->append(kNumericfieldFieldName, _numericfield);

    {
        builder->append(kNsfieldFieldName, _nsfield.toString());
    }

    if (_optionalField.is_initialized()) {
        builder->append(kOptionalFieldFieldName, _optionalField.get());
    }

    {
        builder->append(kVectorFieldFieldName, _vectorField);
    }

    {
        ConstDataRange tempCDR = makeCDR(_binDataField);
        builder->append(kBinDataFieldFieldName, BSONBinData(tempCDR.data(), tempCDR.length(), BinDataGeneral));
    }

    {
        ConstDataRange tempCDR = makeCDR(_uuidField);
        builder->append(kUuidFieldFieldName, BSONBinData(tempCDR.data(), tempCDR.length(), newUUID));
    }

}


BSONObj Default_values::toBSON() const {
    BSONObjBuilder builder;
    serialize(&builder);
    return builder.obj();
}

constexpr StringData BasicConcatenateWithDbOrUUIDCommand::kDbNameFieldName;
constexpr StringData BasicConcatenateWithDbOrUUIDCommand::kField1FieldName;
constexpr StringData BasicConcatenateWithDbOrUUIDCommand::kField2FieldName;
constexpr StringData BasicConcatenateWithDbOrUUIDCommand::kCommandName;

const std::vector<StringData> BasicConcatenateWithDbOrUUIDCommand::_knownBSONFields {
    BasicConcatenateWithDbOrUUIDCommand::kField1FieldName,
    BasicConcatenateWithDbOrUUIDCommand::kField2FieldName,
    BasicConcatenateWithDbOrUUIDCommand::kCommandName,
};
const std::vector<StringData> BasicConcatenateWithDbOrUUIDCommand::_knownOP_MSGFields {
    BasicConcatenateWithDbOrUUIDCommand::kDbNameFieldName,
    BasicConcatenateWithDbOrUUIDCommand::kField1FieldName,
    BasicConcatenateWithDbOrUUIDCommand::kField2FieldName,
    BasicConcatenateWithDbOrUUIDCommand::kCommandName,
};

BasicConcatenateWithDbOrUUIDCommand::BasicConcatenateWithDbOrUUIDCommand(const NamespaceString nss) : _nss(std::move(nss)), _field1(-1), _dbName(nss.db().toString()), _hasField1(false), _hasField2(false), _hasDbName(true) {
    // Used for initialization only
}
BasicConcatenateWithDbOrUUIDCommand::BasicConcatenateWithDbOrUUIDCommand(const NamespaceString nss, std::int32_t field1, std::string field2) : _nss(std::move(nss)), _field1(std::move(field1)), _field2(std::move(field2)), _dbName(nss.db().toString()), _hasField1(true), _hasField2(true), _hasDbName(true) {
    // Used for initialization only
}


BasicConcatenateWithDbOrUUIDCommand BasicConcatenateWithDbOrUUIDCommand::parse(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {
    NamespaceString localNS;
    BasicConcatenateWithDbOrUUIDCommand object(localNS);
    object.parseProtected(ctxt, bsonObject);
    return object;
}
void BasicConcatenateWithDbOrUUIDCommand::parseProtected(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {
    std::bitset<3> usedFields;
    const size_t kField1Bit = 0;
    const size_t kField2Bit = 1;
    const size_t kDbNameBit = 2;
    BSONElement commandElement;
    bool firstFieldFound = false;

    for (const auto& element :bsonObject) {
        const auto fieldName = element.fieldNameStringData();

        if (firstFieldFound == false) {
            commandElement = element;
            firstFieldFound = true;
            continue;
        }

        if (fieldName == kField1FieldName) {
            if (MONGO_likely(ctxt.checkAndAssertType(element, NumberInt))) {
                if (MONGO_unlikely(usedFields[kField1Bit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kField1Bit);

                _hasField1 = true;
                _field1 = element._numberInt();
            }
        }
        else if (fieldName == kField2FieldName) {
            if (MONGO_likely(ctxt.checkAndAssertType(element, String))) {
                if (MONGO_unlikely(usedFields[kField2Bit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kField2Bit);

                _hasField2 = true;
                _field2 = element.str();
            }
        }
        else if (fieldName == kDbNameFieldName) {
            if (MONGO_likely(ctxt.checkAndAssertType(element, String))) {
                if (MONGO_unlikely(usedFields[kDbNameBit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kDbNameBit);

                _hasDbName = true;
                _dbName = element.str();
            }
        }
        else {
            if (!mongo::isGenericArgument(fieldName)) {
                ctxt.throwUnknownField(fieldName);
            }
        }
    }

    if (MONGO_unlikely(!usedFields.all())) {
        if (!usedFields[kField1Bit]) {
            ctxt.throwMissingField(kField1FieldName);
        }
        if (!usedFields[kField2Bit]) {
            ctxt.throwMissingField(kField2FieldName);
        }
        if (!usedFields[kDbNameBit]) {
            ctxt.throwMissingField(kDbNameFieldName);
        }
    }

    invariant(_nss.isEmpty());
    _nss = ctxt.parseNSCollectionRequired(_dbName, commandElement);
}

BasicConcatenateWithDbOrUUIDCommand BasicConcatenateWithDbOrUUIDCommand::parse(const IDLParserErrorContext& ctxt, const OpMsgRequest& request) {
    NamespaceString localNS;
    BasicConcatenateWithDbOrUUIDCommand object(localNS);
    object.parseProtected(ctxt, request);
    return object;
}
void BasicConcatenateWithDbOrUUIDCommand::parseProtected(const IDLParserErrorContext& ctxt, const OpMsgRequest& request) {
    std::bitset<3> usedFields;
    const size_t kField1Bit = 0;
    const size_t kField2Bit = 1;
    const size_t kDbNameBit = 2;
    BSONElement commandElement;
    bool firstFieldFound = false;

    for (const auto& element :request.body) {
        const auto fieldName = element.fieldNameStringData();

        if (firstFieldFound == false) {
            commandElement = element;
            firstFieldFound = true;
            continue;
        }

        if (fieldName == kField1FieldName) {
            if (MONGO_likely(ctxt.checkAndAssertType(element, NumberInt))) {
                if (MONGO_unlikely(usedFields[kField1Bit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kField1Bit);

                _hasField1 = true;
                _field1 = element._numberInt();
            }
        }
        else if (fieldName == kField2FieldName) {
            if (MONGO_likely(ctxt.checkAndAssertType(element, String))) {
                if (MONGO_unlikely(usedFields[kField2Bit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kField2Bit);

                _hasField2 = true;
                _field2 = element.str();
            }
        }
        else if (fieldName == kDbNameFieldName) {
            if (MONGO_likely(ctxt.checkAndAssertType(element, String))) {
                if (MONGO_unlikely(usedFields[kDbNameBit])) {
                    ctxt.throwDuplicateField(element);
                }

                usedFields.set(kDbNameBit);

                _hasDbName = true;
                _dbName = element.str();
            }
        }
        else {
            if (!mongo::isGenericArgument(fieldName)) {
                ctxt.throwUnknownField(fieldName);
            }
        }
    }

    if (MONGO_unlikely(!usedFields.all())) {
        if (!usedFields[kField1Bit]) {
            ctxt.throwMissingField(kField1FieldName);
        }
        if (!usedFields[kField2Bit]) {
            ctxt.throwMissingField(kField2FieldName);
        }
        if (!usedFields[kDbNameBit]) {
            ctxt.throwMissingField(kDbNameFieldName);
        }
    }

    invariant(_nss.isEmpty());
    _nss = ctxt.parseNSCollectionRequired(_dbName, commandElement);
}

void BasicConcatenateWithDbOrUUIDCommand::serialize(const BSONObj& commandPassthroughFields, BSONObjBuilder* builder) const {
    invariant(_hasField1 && _hasField2 && _hasDbName);

    invariant(!_nss.isEmpty());
    builder->append("BasicConcatenateWithDbOrUUIDCommand"_sd, _nss.coll());

    builder->append(kField1FieldName, _field1);

    builder->append(kField2FieldName, _field2);

    IDLParserErrorContext::appendGenericCommandArguments(commandPassthroughFields, _knownBSONFields, builder);

}

OpMsgRequest BasicConcatenateWithDbOrUUIDCommand::serialize(const BSONObj& commandPassthroughFields) const {
    BSONObjBuilder localBuilder;
    {
        BSONObjBuilder* builder = &localBuilder;
        invariant(_hasField1 && _hasField2 && _hasDbName);

        invariant(!_nss.isEmpty());
        builder->append("BasicConcatenateWithDbOrUUIDCommand"_sd, _nss.coll());

        builder->append(kField1FieldName, _field1);

        builder->append(kField2FieldName, _field2);

        builder->append(kDbNameFieldName, _dbName);

        IDLParserErrorContext::appendGenericCommandArguments(commandPassthroughFields, _knownOP_MSGFields, builder);

    }
    OpMsgRequest request;
    request.body = localBuilder.obj();
    return request;
}

BSONObj BasicConcatenateWithDbOrUUIDCommand::toBSON(const BSONObj& commandPassthroughFields) const {
    BSONObjBuilder builder;
    serialize(commandPassthroughFields, &builder);
    return builder.obj();
}

}  // namespace mongo
