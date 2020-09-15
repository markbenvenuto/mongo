/*    Copyright 2013 10gen Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include "jemalloc/jemalloc.h"

#include "mongo/base/init.h"
#include "mongo/db/commands/server_status.h"
#include "mongo/util/log.h"
#include "mongo/util/net/listen.h"

namespace mongo {

namespace {

class JEMallocServerStatusSection : public ServerStatusSection {
public:
    JEMallocServerStatusSection() : ServerStatusSection("jemalloc") {}
    virtual bool includeByDefault() const {
        return true;
    }

    virtual BSONObj generateSection(OperationContext* txn, const BSONElement& configElement) const {
        long long verbosity = 1;
        if (configElement) {
            // Relies on the fact that safeNumberLong turns non-numbers into 0.
            long long configValue = configElement.safeNumberLong();
            if (configValue) {
                verbosity = configValue;
            }
        }

        BSONObjBuilder builder;

        // For a list of properties see the jemalloc man page
        {
            BSONObjBuilder sub(builder.subobjStart("generic"));
            appendNumericPropertyIfAvailable(
                sub, "current_allocated_bytes", "stats.allocated");
	    /* TODO: I think this is right? */
            appendNumericPropertyIfAvailable(sub, "heap_size", "stats.active");
        }
        {
            BSONObjBuilder sub(builder.subobjStart("jemalloc"));

            appendNumericPropertyIfAvailable(
                sub, "allocated_chunk_highwater", "stats.chunks.high");
            appendNumericPropertyIfAvailable(
                sub, "allocated_huge", "stats.huge.allocated");
            appendNumericPropertyIfAvailable(
                sub, "allocated_huge_highwater", "stats.huge.nmalloc");

	    /* TODO: Should switch this to malloc_stats_print
            char buffer[4096];
            MallocExtension::instance()->GetStats(buffer, sizeof buffer);
            builder.append("formattedString", buffer);
	    */
        }

        return builder.obj();
    }

private:
    static void appendNumericPropertyIfAvailable(BSONObjBuilder& builder,
                                                 StringData bsonName,
                                                 const char* property) {
        size_t sz, value;
	if (mallctl(property, &value, &sz, NULL, 0) == 0)
            builder.appendNumber(bsonName, value);
    }

} jemallocServerStatusSection;
}
}
