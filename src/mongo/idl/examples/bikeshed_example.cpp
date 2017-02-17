#include "mongo/db/namespace_string.h"
#include "mongo/stdx/memory.h"
#include "mongo/base/status_with.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/util/net/hostandport.h"
#include <string>

namespace mongo {
NamespaceString parseAndValidateCommandNamespace(StringData, BSONElement&) {
    return NamespaceString();
}
class IDLParserErrorContext {
public:
    IDLParserErrorContext push_back(StringData str);
    void assertNotEmptyObject(StringData str);
    void assertType(BSONElement&, BSONType, StringData str );
    void assertIsNumber(BSONElement&, StringData str);
    void throwUnknownField(BSONElement&, StringData str);
    NamespaceString parseCommandNamespace(BSONElement&, StringData str);
};

class WriteConcernWriteField {
public:
    static WriteConcernWriteField deserializeWField(BSONElement& elem);
    int i;
};

class WriteConcern {
public:
    static WriteConcern parse(IDLParserErrorContext& ctxt, const BSONObj& obj);
    Status serialize();

    const WriteConcernWriteField& getW() const { return _w; }
    void setW(WriteConcernWriteField value) { _w = std::move(value); }

    boost::optional<bool> getJ() const { return _j;  }
    void setJ(boost::optional<bool> value) { _j = value;  }

    int32_t getWTimeout() const { return _wtimeout; }
    void setWTimeout(int32_t value) { _wtimeout = value; }
private:
    WriteConcernWriteField _w; // required so optional is not used
    boost::optional<bool> _j;
    int32_t _wtimeout; // required so optional is not used
};
class BikeShedCmd {
public:
    static BikeShedCmd parse(IDLParserErrorContext& ctxt, const BSONObj& obj);
    Status serialize(NamespaceString ns);

    const NamespaceString& getNS() const { return _ns; }

    boost::optional<StringData> getColor() const { return boost::optional<StringData>(_color);  }
    void setColor(StringData value) { _color = value.toString(); }
    
    const boost::optional<HostAndPort> getHost() const { return _host; }
    void setHost(boost::optional<HostAndPort> value) { _host = value; }

    const WriteConcern& getWriteConcern() const { return _writeConcern; }
    void setWriteConcern(WriteConcern value) { _writeConcern = std::move(value); }
private:
    NamespaceString _ns;
    boost::optional<std::string> _color;
    boost::optional<HostAndPort> _host;
    WriteConcern _writeConcern; // required so optional is not used
};
WriteConcern WriteConcern::parse(IDLParserErrorContext& ctxt, const BSONObj& obj) {
    ctxt.assertNotEmptyObject("writeConcern");

    WriteConcern object;
    for (auto element : obj) {
        const auto fieldName = element.fieldNameStringData();
        if (fieldName == "j") {
            ctxt.assertType(element, Bool, "j");
            object._j = element.trueValue();
        } else if (fieldName == "w") {
            object._w = std::move(WriteConcernWriteField::deserializeWField(element));
        } else if (fieldName == "wTimeout") {
            ctxt.assertIsNumber(element, "wTimeout");
            object._wtimeout = element.numberInt();
        } else if (fieldName == "wOptime") {
            // Ignore.
        } else {
            ctxt.throwUnknownField(element, "writeConcern");
        }
    }
    return std::move(object);
}

BikeShedCmd BikeShedCmd::parse(IDLParserErrorContext& ctxt, const BSONObj& obj) {
    ctxt.assertNotEmptyObject("writeConcern");

    BikeShedCmd object;
    bool firstFieldFound = false;
    for (auto element : obj) {
        const auto fieldName = element.fieldNameStringData();
        if (firstFieldFound == false) {
            object._ns = ctxt.parseCommandNamespace(element, "bikeShedCmd");
            firstFieldFound = true;
            continue;
        }
        if (fieldName == "color") {
            ctxt.assertType(element, String, "color");
            object._color = element.toString();
        } else if (fieldName == "host") {
            ctxt.assertType(element, String, "host");
            object._host = HostAndPort::parseIDL(element.toString());
        } else if (fieldName == "writeConcern") {
            object._writeConcern = std::move(WriteConcern::parse(ctxt.push_back("writeConcern"), element.Obj()));
        } else {
            ctxt.throwUnknownField(element, "writeConcern");
        }
    }
    return std::move(object);
}
} // namespace mongo

