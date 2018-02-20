/**
 *    Copyright (C) 2017 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include "mongo/util/net/ssl_manager.h"

#include "mongo/config.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/log.h"


namespace mongo {

#if 0
namespace {
TEST(SSLManager, matchHostname) {
#ifdef MONGO_CONFIG_SSL
    enum Expected : bool { match = true, mismatch = false };
    const struct {
        Expected expected;
        std::string hostname;
        std::string certName;
    } tests[] = {
        // clang-format off
        // Matches?  |    Hostname and possibly FQDN   |  Certificate name
        {match, "foo.bar.bas", "*.bar.bas."},
        {mismatch, "foo.subdomain.bar.bas", "*.bar.bas."},
        {match, "foo.bar.bas.", "*.bar.bas."},
        {mismatch, "foo.subdomain.bar.bas.", "*.bar.bas."},

        {match, "foo.bar.bas", "*.bar.bas"},
        {mismatch, "foo.subdomain.bar.bas", "*.bar.bas"},
        {match, "foo.bar.bas.", "*.bar.bas"},
        {mismatch, "foo.subdomain.bar.bas.", "*.bar.bas"},

        {mismatch, "foo.evil.bas", "*.bar.bas."},
        {mismatch, "foo.subdomain.evil.bas", "*.bar.bas."},
        {mismatch, "foo.evil.bas.", "*.bar.bas."},
        {mismatch, "foo.subdomain.evil.bas.", "*.bar.bas."},

        {mismatch, "foo.evil.bas", "*.bar.bas"},
        {mismatch, "foo.subdomain.evil.bas", "*.bar.bas"},
        {mismatch, "foo.evil.bas.", "*.bar.bas"},
        {mismatch, "foo.subdomain.evil.bas.", "*.bar.bas"},
        // clang-format on
    };
    bool failure = false;
    for (const auto& test : tests) {
        if (bool(test.expected) != hostNameMatchForX509Certificates(test.hostname, test.certName)) {
            failure = true;
            LOG(1) << "Failure for Hostname: " << test.hostname
                << " Certificate: " << test.certName;
        } else {
            LOG(1) << "Passed for Hostname: " << test.hostname << " Certificate: " << test.certName;
        }
    }
    ASSERT_FALSE(failure);
#endif
}

}
#endif

#ifdef MONGO_CONFIG_SSL
#ifdef _WIN32

StatusWith<stdx::unordered_set<RoleName>> parsePeerRoles(ConstDataRange cdrExtension);

TEST(SSLManager, MongoDBRolesParser) {
    /*
    openssl asn1parse -genconf mongodbroles.cnf -out foo.der

    -------- mongodbroles.cnf --------
    asn1 = SET:MongoDBAuthorizationGrant

    [MongoDBAuthorizationGrant]
    grant1 = SEQUENCE:MongoDBRole

    [MongoDBRole]
    role  = UTF8:role_name
    database = UTF8:Third field
    */
    unsigned char foo_der[] = {
        0x31, 0x1a, 0x30, 0x18, 0x0c, 0x09, 0x72, 0x6f, 0x6c, 0x65, 0x5f, 0x6e,
        0x61, 0x6d, 0x65, 0x0c, 0x0b, 0x54, 0x68, 0x69, 0x72, 0x64, 0x20, 0x66,
        0x69, 0x65, 0x6c, 0x64
    };
    unsigned int foo_der_len = 28;

    auto swPeer = parsePeerRoles(ConstDataRange(reinterpret_cast<char*>(foo_der), foo_der_len));
    ASSERT_OK(swPeer.getStatus());
}
#endif
#endif

//  // namespace
}  // namespace mongo
