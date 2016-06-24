/**
 * Copyright (C) 2016 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include "mongo/util/client_metadata.h"

#include <boost/filesystem.hpp>
#include <map>

#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/log.h"

namespace mongo {
const StringData kMetadataDoc = "client";
const StringData kApplication = "application";
const StringData kDriver = "driver";
const StringData kName = "name";
const StringData kType = "type";
const StringData kVersion = "version";
const StringData kOperatingSystem = "os";
const StringData kArchitecture = "architecture";

TEST(ClientMetadatTest, TestLoopbackTest) {
    // Serialize without application name
    {
        BSONObjBuilder builder;
        ASSERT_OK(ClientMetadata::serialize("a", "b", "c", "d", "e", "f", "g", builder));

        auto obj = builder.obj();
        auto md = ClientMetadata();
        auto swParse = md.parseIsMasterReply(obj);
        ASSERT_OK(swParse);
        ASSERT_EQUALS("g", md.getApplicationName());

        BSONObj outDoc =
            BSON(kMetadataDoc << BSON(
                     kApplication << BSON(kName << "g") << kDriver
                                  << BSON(kName << "a" << kVersion << "b")
                                  << kOperatingSystem
                                  << BSON(kType << "c" << kName << "d" << kArchitecture << "e"
                                                << kVersion
                                                << "f")));
        ASSERT_EQUALS(obj, outDoc);
    }

    // Serialize without application name
    {
        BSONObjBuilder builder;
        ClientMetadata::serialize("a", "b", "c", "d", "e", "f", builder);

        auto obj = builder.obj();
        auto md = ClientMetadata();
        auto swParse = md.parseIsMasterReply(obj);
        ASSERT_OK(swParse);

        BSONObj outDoc = BSON(
            kMetadataDoc << BSON(
                kDriver << BSON(kName << "a" << kVersion << "b") << kOperatingSystem
                        << BSON(kType << "c" << kName << "d" << kArchitecture << "e" << kVersion
                                      << "f")));
        ASSERT_EQUALS(obj, outDoc);
    }

    // Serialize with the os information automatically computed
    {
        BSONObjBuilder builder;
        ASSERT_OK(ClientMetadata::serialize("a", "b", "f", builder));

        auto obj = builder.obj();

        log() << "DOC: " << obj;

        auto md = ClientMetadata();
        auto swParse = md.parseIsMasterReply(obj);
        ASSERT_OK(swParse);
        ASSERT_EQUALS("f", md.getApplicationName());
    }
}

#define ASSERT_DOC_OK(x)                                                                    \
    {                                                                                       \
        auto _swParse = ClientMetadata().parseIsMasterReply(BSON(kMetadataDoc << BSON(x))); \
        ASSERT_OK(_swParse.getStatus());                                                    \
    }
#define ASSERT_DOC_NOT_OK(x)                                                                \
    {                                                                                       \
        auto _swParse = ClientMetadata().parseIsMasterReply(BSON(kMetadataDoc << BSON(x))); \
        ASSERT_NOT_OK(_swParse.getStatus());                                                \
    }

// Mixed: no client metadata document
TEST(ClientMetadatTest, TestEmptyDoc) {
    {
        auto swParse = ClientMetadata().parseIsMasterReply(BSONObj());

        ASSERT_OK(swParse.getStatus());
    }

    {
        auto obj = BSON("client" << BSONObj());
        auto swParse = ClientMetadata().parseIsMasterReply(obj);

        ASSERT_NOT_OK(swParse.getStatus());
    }
}

// Positive: test with only required fields
TEST(ClientMetadatTest, TestRequiredOnlyFields) {
    // Without app name
    ASSERT_DOC_OK(kDriver << BSON(kName << "n1" << kVersion << "v1") << kOperatingSystem
                          << BSON(kType << "unknown"));

    // With AppName
    ASSERT_DOC_OK(kApplication << BSON(kName << "1") << kDriver
                               << BSON(kName << "n1" << kVersion << "v1")
                               << kOperatingSystem
                               << BSON(kType << "unknown"));
}


// Positive: test with app_name spelled wrong fields
TEST(ClientMetadatTest, TestWithAppNameSpelledWrong) {
    ASSERT_DOC_OK(kApplication << BSON("extra"
                                       << "1")
                               << kDriver
                               << BSON(kName << "n1" << kVersion << "v1")
                               << kOperatingSystem
                               << BSON(kType << "unknown"));
}

// Positive: test with empty application document
TEST(ClientMetadatTest, TestWithEmptyApplication) {
    ASSERT_DOC_OK(kApplication << BSONObj() << kDriver << BSON(kName << "n1" << kVersion << "v1")
                               << kOperatingSystem
                               << BSON(kType << "unknown"));
}

// Negative: test with appplication wrong type
TEST(ClientMetadatTest, TestNegativeWithAppNameWrongType) {
    ASSERT_DOC_NOT_OK(kApplication << "1" << kDriver << BSON(kName << "n1" << kVersion << "v1")
                                   << kOperatingSystem
                                   << BSON(kType << "unknown"));
}

// Negative: second call with client metadata document fails
TEST(ClientMetadatTest, TestNegativeDuplicateIsMaster) {
    auto doc = BSON(kMetadataDoc << BSON(kApplication << BSON(kName << "1") << kDriver
                                                      << BSON(kName << "n1" << kVersion << "v1")
                                                      << kOperatingSystem
                                                      << BSON(kType << "unknown")));

    ClientMetadata md;
    ASSERT_OK(md.parseIsMasterReply(doc).getStatus());
    ASSERT_NOT_OK(md.parseIsMasterReply(doc).getStatus());
}

// Positive: test with extra fields
TEST(ClientMetadatTest, TestExtraFields) {
    ASSERT_DOC_OK(kApplication << BSON(kName << "1"
                                             << "extra"
                                             << "v1")
                               << kDriver
                               << BSON(kName << "n1" << kVersion << "v1")
                               << kOperatingSystem
                               << BSON(kType << "unknown"));
    ASSERT_DOC_OK(kApplication << BSON(kName << "1"
                                             << "extra"
                                             << "v1")
                               << kDriver
                               << BSON(kName << "n1" << kVersion << "v1"
                                             << "extra"
                                             << "v1")
                               << kOperatingSystem
                               << BSON(kType << "unknown"));
    ASSERT_DOC_OK(kApplication << BSON(kName << "1"
                                             << "extra"
                                             << "v1")
                               << kDriver
                               << BSON(kName << "n1" << kVersion << "v1")
                               << kOperatingSystem
                               << BSON(kType << "unknown"
                                             << "extra"
                                             << "v1"));
    ASSERT_DOC_OK(kApplication << BSON(kName << "1"
                                             << "extra"
                                             << "v1")
                               << kDriver
                               << BSON(kName << "n1" << kVersion << "v1")
                               << kOperatingSystem
                               << BSON(kType << "unknown")
                               << "extra"
                               << "v1");
}

// Negative: only application specified
TEST(ClientMetadatTest, TestNegativeOnlyApplication) {
    ASSERT_DOC_NOT_OK(kApplication << BSON(kName << "1"
                                                 << "extra"
                                                 << "v1"));
}

// Negative: all combinations of only missing 1 required field
TEST(ClientMetadatTest, TestNegativeMissingRequiredOneField) {
    ASSERT_DOC_NOT_OK(kDriver << BSON(kVersion << "v1") << kOperatingSystem
                              << BSON(kType << "unknown"));
    ASSERT_DOC_NOT_OK(kDriver << BSON(kName << "n1") << kOperatingSystem
                              << BSON(kType << "unknown"));
    ASSERT_DOC_NOT_OK(kDriver << BSON(kName << "n1" << kVersion << "v1"));
}

// Negative: document with wrong types for required fields
TEST(ClientMetadatTest, TestNegativeWrongTypes) {
    ASSERT_DOC_NOT_OK(kApplication << BSON(kName << 1) << kDriver
                                   << BSON(kName << "n1" << kVersion << "v1")
                                   << kOperatingSystem
                                   << BSON(kType << "unknown"));
    ASSERT_DOC_NOT_OK(kApplication << BSON(kName << "1") << kDriver
                                   << BSON(kName << 1 << kVersion << "v1")
                                   << kOperatingSystem
                                   << BSON(kType << "unknown"));
    ASSERT_DOC_NOT_OK(kApplication << BSON(kName << "1") << kDriver
                                   << BSON(kName << "n1" << kVersion << 1)
                                   << kOperatingSystem
                                   << BSON(kType << "unknown"));
    ASSERT_DOC_NOT_OK(kApplication << BSON(kName << "1") << kDriver
                                   << BSON(kName << "n1" << kVersion << "v1")
                                   << kOperatingSystem
                                   << BSON(kType << 1));
}

// Negative: document larger than 512 bytes
TEST(ClientMetadatTest, TestNegativeLargeDocument) {
    {
        std::string str(350, 'x');
        ASSERT_DOC_OK(kApplication << BSON(kName << "1") << kDriver
                                   << BSON(kName << "n1" << kVersion << "1")
                                   << kOperatingSystem
                                   << BSON(kType << "unknown")
                                   << "extra"
                                   << str);
    }
    {
        std::string str(512, 'x');
        ASSERT_DOC_NOT_OK(kApplication << BSON(kName << "1") << kDriver
                                       << BSON(kName << "n1" << kVersion << "1")
                                       << kOperatingSystem
                                       << BSON(kType << "unknown")
                                       << "extra"
                                       << str);
    }
}

// Negative: document with app_name larger than 128 bytes
TEST(ClientMetadatTest, TestNegativeLargeAppName) {
    {
        std::string str(128, 'x');
        ASSERT_DOC_OK(kApplication << BSON(kName << str) << kDriver
                                   << BSON(kName << "n1" << kVersion << "1")
                                   << kOperatingSystem
                                   << BSON(kType << "unknown"));

        BSONObjBuilder builder;
        ASSERT_OK(ClientMetadata::serialize("n1", "1", str, builder));
    }
    {
        std::string str(129, 'x');
        ASSERT_DOC_NOT_OK(kApplication << BSON(kName << str) << kDriver
                                       << BSON(kName << "n1" << kVersion << "1")
                                       << kOperatingSystem
                                       << BSON(kType << "unknown"));

        BSONObjBuilder builder;
        ASSERT_NOT_OK(ClientMetadata::serialize("n1", "1", str, builder));
    }
}

}  // namespace mongo
