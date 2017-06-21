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

namespace mongo {

constexpr StringData One_string::kValueFieldName;


One_string One_string::parse(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {
    One_string object;
    object.parseProtected(ctxt, bsonObject);
    return object;
}
void One_string::parseProtected(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {
    std::bitset<1> usedFields;
    const size_t kValueBit = 0;

    for (const auto& element : bsonObject) {
        const auto fieldName = element.fieldNameStringData();


        if (fieldName == kValueFieldName) {
            if (usedFields[kValueBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kValueBit);

            if (ctxt.checkAndAssertType(element, String)) {
                _value = element.str();
            }
        } else {
            ctxt.throwUnknownField(fieldName);
        }
    }


    if (!usedFields.all()) {
        if (!usedFields[kValueBit]) {
            ctxt.throwMissingField(kValueFieldName);
        }
    }
}


void One_string::serialize(BSONObjBuilder* builder) const {
    builder->append(kValueFieldName, _value);
}


BSONObj One_string::toBSON() const {
    BSONObjBuilder builder;
    serialize(&builder);
    return builder.obj();
}

constexpr StringData Default_values::kBinDataFieldFieldName;
constexpr StringData Default_values::kDbNameFieldName;
constexpr StringData Default_values::kIntfieldFieldName;
constexpr StringData Default_values::kNsfieldFieldName;
constexpr StringData Default_values::kNumericfieldFieldName;
constexpr StringData Default_values::kObjectsFieldName;
constexpr StringData Default_values::kOptionalFieldFieldName;
constexpr StringData Default_values::kStringfieldFieldName;
constexpr StringData Default_values::kStructsFieldName;
constexpr StringData Default_values::kUuidFieldFieldName;
constexpr StringData Default_values::kVectorFieldFieldName;
constexpr StringData Default_values::kCommandName;

const std::vector<StringData> Default_values::_knownFields{
    Default_values::kBinDataFieldFieldName,
    Default_values::kDbNameFieldName,
    Default_values::kIntfieldFieldName,
    Default_values::kNsfieldFieldName,
    Default_values::kNumericfieldFieldName,
    Default_values::kObjectsFieldName,
    Default_values::kOptionalFieldFieldName,
    Default_values::kStringfieldFieldName,
    Default_values::kStructsFieldName,
    Default_values::kUuidFieldFieldName,
    Default_values::kVectorFieldFieldName,
    Default_values::kCommandName,
};

Default_values::Default_values(const NamespaceString& nss)
    : _nss(nss), _dbName(nss.db().toString()) {
    // Used for initialization only
}

Default_values Default_values::parse(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {
    NamespaceString localNs;
    Default_values object(localNs);
    object.parseProtected(ctxt, bsonObject);
    return object;
}
void Default_values::parseProtected(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject) {
    std::bitset<11> usedFields;
    const size_t kStringfieldBit = 0;
    const size_t kIntfieldBit = 1;
    const size_t kNumericfieldBit = 2;
    const size_t kNsfieldBit = 3;
    const size_t kOptionalFieldBit = 4;
    const size_t kVectorFieldBit = 5;
    const size_t kBinDataFieldBit = 6;
    const size_t kUuidFieldBit = 7;
    const size_t kStructsBit = 8;
    const size_t kObjectsBit = 9;
    const size_t kDbNameBit = 10;
    BSONElement commandElement;
    bool firstFieldFound = false;

    for (const auto& element : bsonObject) {
        const auto fieldName = element.fieldNameStringData();

        if (firstFieldFound == false) {
            commandElement = element;
            firstFieldFound = true;
            continue;
        }

        if (fieldName == kStringfieldFieldName) {
            if (usedFields[kStringfieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kStringfieldBit);

            if (ctxt.checkAndAssertType(element, String)) {
                _stringfield = element.str();
            }
        } else if (fieldName == kIntfieldFieldName) {
            if (usedFields[kIntfieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kIntfieldBit);

            if (ctxt.checkAndAssertType(element, NumberInt)) {
                _intfield = element._numberInt();
            }
        } else if (fieldName == kNumericfieldFieldName) {
            if (usedFields[kNumericfieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kNumericfieldBit);

            if (ctxt.checkAndAssertTypes(element,
                                         {NumberLong, NumberInt, NumberDecimal, NumberDouble})) {
                _numericfield = element.numberInt();
            }
        } else if (fieldName == kNsfieldFieldName) {
            if (usedFields[kNsfieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kNsfieldBit);

            if (ctxt.checkAndAssertType(element, String)) {
                _nsfield = NamespaceString(element.valueStringData());
            }
        } else if (fieldName == kOptionalFieldFieldName) {
            if (usedFields[kOptionalFieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kOptionalFieldBit);

            if (ctxt.checkAndAssertType(element, String)) {
                _optionalField = element.str();
            }
        } else if (fieldName == kVectorFieldFieldName) {
            if (usedFields[kVectorFieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kVectorFieldBit);

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
            if (usedFields[kBinDataFieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kBinDataFieldBit);

            if (ctxt.checkAndAssertBinDataType(element, BinDataGeneral)) {
                _binDataField = element._binDataVector();
            }
        } else if (fieldName == kUuidFieldFieldName) {
            if (usedFields[kUuidFieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kUuidFieldBit);

            if (ctxt.checkAndAssertBinDataType(element, newUUID)) {
                _uuidField = element.uuid();
            }
        } else if (fieldName == kStructsFieldName) {
            if (usedFields[kStructsBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kStructsBit);

            std::uint32_t expectedFieldNumber{0};
            const IDLParserErrorContext arrayCtxt(kStructsFieldName, &ctxt);
            std::vector<One_string> values;

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

                    if (arrayCtxt.checkAndAssertType(arrayElement, Object)) {
                        IDLParserErrorContext tempContext(kStructsFieldName, &ctxt);
                        const auto localObject = arrayElement.Obj();
                        values.emplace_back(One_string::parse(tempContext, localObject));
                    }
                } else {
                    arrayCtxt.throwBadArrayFieldNumberValue(arrayFieldName);
                }
                ++expectedFieldNumber;
            }
            _structs = std::move(values);
        } else if (fieldName == kObjectsFieldName) {
            if (usedFields[kObjectsBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kObjectsBit);

            std::uint32_t expectedFieldNumber{0};
            const IDLParserErrorContext arrayCtxt(kObjectsFieldName, &ctxt);
            std::vector<mongo::BSONObj> values;

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

                    if (arrayCtxt.checkAndAssertType(arrayElement, Object)) {
                        values.emplace_back(arrayElement.Obj());
                    }
                } else {
                    arrayCtxt.throwBadArrayFieldNumberValue(arrayFieldName);
                }
                ++expectedFieldNumber;
            }
            _objects = std::move(values);
        } else if (fieldName == kDbNameFieldName) {
            if (usedFields[kDbNameBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kDbNameBit);

            if (ctxt.checkAndAssertType(element, String)) {
                _dbName = element.str();
            }
        } else {
            if (!Command::isGenericArgument(fieldName)) {
                ctxt.throwUnknownField(fieldName);
            }
        }
    }


    if (!usedFields.all()) {
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
        if (!usedFields[kStructsBit]) {
            ctxt.throwMissingField(kStructsFieldName);
        }
        if (!usedFields[kObjectsBit]) {
            ctxt.throwMissingField(kObjectsFieldName);
        }
        if (!usedFields[kDbNameBit]) {
            _dbName = "admin";
        }
    }

    _nss = ctxt.parseNSCollectionRequired(_dbName, commandElement);
}

Default_values Default_values::parse(const IDLParserErrorContext& ctxt,
                                     const OpMsgRequest& request) {
    NamespaceString localNs;
    Default_values object(localNs);
    object.parseProtected(ctxt, request);
    return object;
}
void Default_values::parseProtected(const IDLParserErrorContext& ctxt,
                                    const OpMsgRequest& request) {
    std::bitset<11> usedFields;
    const size_t kStringfieldBit = 0;
    const size_t kIntfieldBit = 1;
    const size_t kNumericfieldBit = 2;
    const size_t kNsfieldBit = 3;
    const size_t kOptionalFieldBit = 4;
    const size_t kVectorFieldBit = 5;
    const size_t kBinDataFieldBit = 6;
    const size_t kUuidFieldBit = 7;
    const size_t kStructsBit = 8;
    const size_t kObjectsBit = 9;
    const size_t kDbNameBit = 10;
    BSONElement commandElement;
    bool firstFieldFound = false;

    for (const auto& element : request.body) {
        const auto fieldName = element.fieldNameStringData();

        if (firstFieldFound == false) {
            commandElement = element;
            firstFieldFound = true;
            continue;
        }

        if (fieldName == kStringfieldFieldName) {
            if (usedFields[kStringfieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kStringfieldBit);

            if (ctxt.checkAndAssertType(element, String)) {
                _stringfield = element.str();
            }
        } else if (fieldName == kIntfieldFieldName) {
            if (usedFields[kIntfieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kIntfieldBit);

            if (ctxt.checkAndAssertType(element, NumberInt)) {
                _intfield = element._numberInt();
            }
        } else if (fieldName == kNumericfieldFieldName) {
            if (usedFields[kNumericfieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kNumericfieldBit);

            if (ctxt.checkAndAssertTypes(element,
                                         {NumberLong, NumberInt, NumberDecimal, NumberDouble})) {
                _numericfield = element.numberInt();
            }
        } else if (fieldName == kNsfieldFieldName) {
            if (usedFields[kNsfieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kNsfieldBit);

            if (ctxt.checkAndAssertType(element, String)) {
                _nsfield = NamespaceString(element.valueStringData());
            }
        } else if (fieldName == kOptionalFieldFieldName) {
            if (usedFields[kOptionalFieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kOptionalFieldBit);

            if (ctxt.checkAndAssertType(element, String)) {
                _optionalField = element.str();
            }
        } else if (fieldName == kVectorFieldFieldName) {
            if (usedFields[kVectorFieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kVectorFieldBit);

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
            if (usedFields[kBinDataFieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kBinDataFieldBit);

            if (ctxt.checkAndAssertBinDataType(element, BinDataGeneral)) {
                _binDataField = element._binDataVector();
            }
        } else if (fieldName == kUuidFieldFieldName) {
            if (usedFields[kUuidFieldBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kUuidFieldBit);

            if (ctxt.checkAndAssertBinDataType(element, newUUID)) {
                _uuidField = element.uuid();
            }
        } else if (fieldName == kStructsFieldName) {
            if (usedFields[kStructsBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kStructsBit);

            std::uint32_t expectedFieldNumber{0};
            const IDLParserErrorContext arrayCtxt(kStructsFieldName, &ctxt);
            std::vector<One_string> values;

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

                    if (arrayCtxt.checkAndAssertType(arrayElement, Object)) {
                        IDLParserErrorContext tempContext(kStructsFieldName, &ctxt);
                        const auto localObject = arrayElement.Obj();
                        values.emplace_back(One_string::parse(tempContext, localObject));
                    }
                } else {
                    arrayCtxt.throwBadArrayFieldNumberValue(arrayFieldName);
                }
                ++expectedFieldNumber;
            }
            _structs = std::move(values);
        } else if (fieldName == kObjectsFieldName) {
            if (usedFields[kObjectsBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kObjectsBit);

            std::uint32_t expectedFieldNumber{0};
            const IDLParserErrorContext arrayCtxt(kObjectsFieldName, &ctxt);
            std::vector<mongo::BSONObj> values;

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

                    if (arrayCtxt.checkAndAssertType(arrayElement, Object)) {
                        values.emplace_back(arrayElement.Obj());
                    }
                } else {
                    arrayCtxt.throwBadArrayFieldNumberValue(arrayFieldName);
                }
                ++expectedFieldNumber;
            }
            _objects = std::move(values);
        } else if (fieldName == kDbNameFieldName) {
            if (usedFields[kDbNameBit]) {
                ctxt.throwDuplicateField(element);
            }

            usedFields.set(kDbNameBit);

            if (ctxt.checkAndAssertType(element, String)) {
                _dbName = element.str();
            }
        } else {
            if (!Command::isGenericArgument(fieldName)) {
                ctxt.throwUnknownField(fieldName);
            }
        }
    }


    for (auto&& sequence : request.sequences) {
        if (sequence.name == kStructsFieldName) {
            if (usedFields[kStructsBit]) {
                ctxt.throwDuplicateField(sequence.name);
            }

            usedFields.set(kStructsBit);

            std::vector<One_string> values;

            for (const BSONObj& sequenceObject : sequence.objs) {
                IDLParserErrorContext tempContext(kStructsFieldName, &ctxt);
                values.emplace_back(One_string::parse(tempContext, sequenceObject));
            }
            _structs = std::move(values);
        } else if (sequence.name == kObjectsFieldName) {
            if (usedFields[kObjectsBit]) {
                ctxt.throwDuplicateField(sequence.name);
            }

            usedFields.set(kObjectsBit);

            std::vector<mongo::BSONObj> values;

            for (const BSONObj& sequenceObject : sequence.objs) {
                values.emplace_back(sequenceObject);
            }
            _objects = std::move(values);
        } else {
            ctxt.throwUnknownField(sequence.name);
        }
    }

    if (!usedFields.all()) {
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
        if (!usedFields[kStructsBit]) {
            ctxt.throwMissingField(kStructsFieldName);
        }
        if (!usedFields[kObjectsBit]) {
            ctxt.throwMissingField(kObjectsFieldName);
        }
        if (!usedFields[kDbNameBit]) {
            _dbName = "admin";
        }
    }

    _nss = ctxt.parseNSCollectionRequired(_dbName, commandElement);
}

void Default_values::serialize(const BSONObj& commandPassthroughFields,
                               BSONObjBuilder* builder) const {
    builder->append("default_values", _nss.coll());

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

    {
        BSONArrayBuilder arrayBuilder(builder->subarrayStart(kStructsFieldName));
        for (const auto& item : _structs) {
            BSONObjBuilder subObjBuilder(arrayBuilder.subobjStart());
            item.serialize(&subObjBuilder);
        }
    }

    { builder->append(kObjectsFieldName, _objects); }

    builder->append(kDbNameFieldName, _dbName);

    IDLParserErrorContext::appendGenericCommandArguments(
        commandPassthroughFields, _knownFields, builder);
}

OpMsgRequest Default_values::serialize(const BSONObj& commandPassthroughFields) const {
    BSONObjBuilder localBuilder;
    serialize(commandPassthroughFields, &localBuilder);
    OpMsgRequest request;
    request.body = localBuilder.obj();
    return request;
}

BSONObj Default_values::toBSON(const BSONObj& commandPassthroughFields) const {
    BSONObjBuilder builder;
    serialize(commandPassthroughFields, &builder);
    return builder.obj();
}

}  // namespace mongo
