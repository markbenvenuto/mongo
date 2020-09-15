/**
 *    Copyright (C) 2014 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include <algorithm>
#include "jemalloc/jemalloc.h"

#include "mongo/base/disallow_copying.h"
#include "mongo/base/init.h"
#include "mongo/base/parse_number.h"
#include "mongo/base/status.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/server_parameters.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/processinfo.h"

namespace mongo {
namespace {

class JemallocNumericPropertyServerParameter : public ServerParameter {
    MONGO_DISALLOW_COPYING(JemallocNumericPropertyServerParameter);

public:
    explicit JemallocNumericPropertyServerParameter(const std::string& serverParameterName,
                                                    const std::string& jemallocPropertyName);

    virtual void append(OperationContext* txn, BSONObjBuilder& b, const std::string& name);
    virtual Status set(const BSONElement& newValueElement);
    virtual Status setFromString(const std::string& str);

private:
    const std::string _jemallocPropertyName;
};

JemallocNumericPropertyServerParameter::JemallocNumericPropertyServerParameter(
    const std::string& serverParameterName, const std::string& jemallocPropertyName)
    : ServerParameter(ServerParameterSet::getGlobal(),
                      serverParameterName,
                      true /* change at startup */,
                      true /* change at runtime */),
      _jemallocPropertyName(jemallocPropertyName) {}

void JemallocNumericPropertyServerParameter::append(OperationContext* txn,
                                                    BSONObjBuilder& b,
                                                    const std::string& name) {
    size_t sz, value;
    if (mallctl(_jemallocPropertyName.c_str(), &value, &sz, NULL, 0)) {
        b.appendNumber(name, value);
    }
}

Status JemallocNumericPropertyServerParameter::set(const BSONElement& newValueElement) {
    if (!newValueElement.isNumber()) {
        return Status(ErrorCodes::TypeMismatch,
                      str::stream() << "Expected server parameter " << newValueElement.fieldName()
                                    << " to have numeric type, but found "
                                    << newValueElement.toString(false)
                                    << " of type "
                                    << typeName(newValueElement.type()));
    }
    long long valueAsLongLong = newValueElement.safeNumberLong();
    if (valueAsLongLong < 0 ||
        static_cast<unsigned long long>(valueAsLongLong) > std::numeric_limits<size_t>::max()) {
        return Status(
            ErrorCodes::BadValue,
            str::stream() << "Value " << newValueElement.toString(false) << " is out of range for "
                          << newValueElement.fieldName()
                          << "; expected a value between 0 and "
                          << std::min<unsigned long long>(std::numeric_limits<size_t>::max(),
                                                          std::numeric_limits<long long>::max()));
    }
    size_t valueAsSizet = static_cast<size_t>(valueAsLongLong);
    if (!mallctl(_jemallocPropertyName.c_str(),
            NULL, 0, &valueAsSizet, sizeof(size_t))) {
        return Status(ErrorCodes::InternalError,
                      str::stream() << "Failed to set internal jemalloc property "
                                    << _jemallocPropertyName);
    }
    return Status::OK();
}

Status JemallocNumericPropertyServerParameter::setFromString(const std::string& str) {
    long long valueAsLongLong;
    Status status = parseNumberFromString(str, &valueAsLongLong);
    if (!status.isOK()) {
        return status;
    }
    BSONObjBuilder builder;
    builder.append(name(), valueAsLongLong);
    return set(builder.done().firstElement());
}

// JEMalloc uses a certain number of arenas, shared amongst all threads. The default value is
// eight times the number of CPUs, which can contribute to fragmentation.
JemallocNumericPropertyServerParameter jemallocMaxArenas(
    "jemallocMaxArenas", "opt.narenas");

JemallocNumericPropertyServerParameter jemallocRedzone(
    "jemallocRedzone", "opt.redzone");

MONGO_INITIALIZER_GENERAL(JemallocConfigurationDefaults,
                          MONGO_NO_PREREQUISITES,
                          ("BeginStartupOptionHandling"))
(InitializerContext*) {
    // Don't set our custom configuration options if the user has configured their own
    if (getenv("MALLOC_CONF")) {
        return Status::OK();
    }

    (void)jemallocRedzone.setFromString("0");
    return jemallocMaxArenas.setFromString("8");
}

}  // namespace
}  // namespace mongo
