#include "mongo/db/namespace_string.h"
#include "mongo/stdx/memory.h"
#include "mongo/base/status_with.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobjbuilder.h"
#include <string>

namespace mongo {
class WriteConcernWriteField {
public:
    static StatusWith<WriteConcernWriteField> deserializeWField(BSONElement& elem);
    int i;
};

class WriteConcern {
public:
    static StatusWith<std::unique_ptr<WriteConcern>> parse(const BSONObj& obj);
    Status serialize();

    const WriteConcernWriteField& getW() const { return _w; }
    void setW(WriteConcernWriteField value) { _w = std::move(value); }

    bool getJ() const { return _j;  }
    void setJ(bool value) { _j = value;  }

    // Getters/Setters ...
private:
    WriteConcernWriteField _w;
    bool _j;
    int32_t _wtimeout;
};

class BikeShedCmd {
public:
    static StatusWith<std::unique_ptr<BikeShedCmd>> parse(const BSONObj& obj);
    Status serialize();

    StringData getColor() const { return _color;  }
    void setColor(StringData value) { _color = value.toString(); }
    // Getters/Setters ...
private:
    std::string _color;
    NamespaceString _ns;
    std::unique_ptr<WriteConcern> _writeConcen;
};

} // namespace mongo

namespace mongo {

StatusWith<std::unique_ptr<WriteConcern>> WriteConcern::parse(const BSONObj& obj) {
    if (obj.isEmpty()) {
        return Status(ErrorCodes::FailedToParse, "WriteConcern object cannot be empty");
    }

    std::unique_ptr<WriteConcern> object;
    for (auto element : obj) {
        const auto fieldName = element.fieldNameStringData();
        if (fieldName == "j") {
            if (element.type() != Bool) {
                return Status(ErrorCodes::FailedToParse, "j must be a boolean value");
            }
            object->_j = element.trueValue();
        } else if (fieldName == "w") {
            auto swParseField = WriteConcernWriteField::deserializeWField(element);
            if (!swParseField.isOK()) {
                return swParseField.getStatus();
            }
            object->_w = std::move(swParseField.getValue());
        } else if (fieldName == "wTimeout") {
            if (element.isNumber()) {
                return Status(ErrorCodes::FailedToParse, "wTimeout must be a number field");
            }
            object->_wtimeout = element.numberInt();
        } else if (fieldName == "wOptime") {
            // Ignore.
        } else {
            return Status(ErrorCodes::FailedToParse,
                str::stream() << "Unrecognized WriteConcern field: " << fieldName);
        }
    }

    return std::move(object);
}

StatusWith<std::unique_ptr<BikeShedCmd>> BikeShedCmd::parse(const BSONObj& obj) {
    if (obj.isEmpty()) {
        return Status(ErrorCodes::FailedToParse, "BikeShedCmd object cannot be empty");
    }

    std::unique_ptr<WriteConcern> object;
    for (auto element : obj) {
        const auto fieldName = element.fieldNameStringData();
        if (fieldName == "color") {
            if (element.type() != String) {
                return Status(ErrorCodes::FailedToParse, "color must be a string value");
            }
            object->_color = element.stringValue();
        } else if (fieldName == "ns") {
            if (element.type() != String) {
                return Status(ErrorCodes::FailedToParse, "ns must be a string value");
            }
            object->_ns = NamespaceString(element.stringValue());
        } else if (fieldName == "writeConcern") {
            auto swParseField = WriteConcern::parse(element);
            if (!swParseField.isOK()) {
                return swParseField.getStatus();
            }
            object->_writeConcen = std::move(swParseField.getValue());
        } else {
            return Status(ErrorCodes::FailedToParse,
                str::stream() << "Unrecognized BikeShedCmd field: " << fieldName);
        }
    }

    return std::move(object);
}

} // namespace mongo

