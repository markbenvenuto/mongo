/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kTest

#include <fstream>

#include "mongo/platform/basic.h"

#include "mongo/executor/network_interface_integration_fixture.h"
#include "mongo/logv2/log.h"
#include "mongo/unittest/integration_test.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/net/ssl_options.h"
#include "mongo/client/authenticate.h"

#include "mongo/base/init.h"
#include "mongo/base/shim.h"
#include "mongo/base/status.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/config.h"
#include "mongo/crypto/mechanism_scram.h"
#include "mongo/db/auth/address_restriction.h"
#include "mongo/db/auth/authorization_manager_global_parameters_gen.h"
#include "mongo/db/auth/authorization_manager_impl_parameters_gen.h"
#include "mongo/db/auth/authorization_session_impl.h"
#include "mongo/db/auth/authz_manager_external_state.h"
#include "mongo/db/auth/sasl_options.h"
#include "mongo/db/auth/user_document_parser.h"
#include "mongo/db/auth/user_management_commands_parser.h"
#include "mongo/db/global_settings.h"
#include "mongo/db/mongod_options.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/fail_point.h"
#include "mongo/util/net/ssl_peer_info.h"
#include "mongo/util/net/ssl_types.h"
#include "mongo/util/str.h"


namespace mongo {
namespace executor {
namespace {

std::string loadFile(const std::string& name) {
    std::ifstream input(name);
    std::string str((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return str;
}

class NetworkInterfaceSSLFixture : public NetworkInterfaceIntegrationFixture {
public:
    void setUp() final {


        UserHandle user(User(UserName("__system", "local")));

        ActionSet allActions;
        allActions.addAllActions();
        PrivilegeVector privileges;
        auth::generateUniversalPrivileges(&privileges);
        user->addPrivileges(privileges);

        internalSecurity.user = user;


            // Force all connections to use SSL for outgoing
        sslGlobalParams.sslMode.store(static_cast<int>(SSLParams::SSLModes::SSLMode_requireSSL));
        sslGlobalParams.sslCAFile = "jstests/libs/ca.pem";
              auth::setInternalUserAuthParams(auth::createInternalX509AuthDocument(boost::optional<StringData>("FAKE")));

        ConnectionPool::Options options;
        options.transientSSLParams.emplace([] {
            TransientSSLParams params;
            params.sslClusterPEMPayload = loadFile("jstests/libs/client.pem");
            params.targetedClusterConnectionString = ConnectionString::forLocal();
            return params;
        }());
        LOGV2(5181101, "Initializing the test connection with transient SSL params");
        createNet(nullptr, std::move(options));
        net().startup();
    }
};

TEST_F(NetworkInterfaceSSLFixture, Ping) {
    assertCommandOK("admin", BSON("ping" << 1));
}


}  // namespace
}  // namespace executor
}  // namespace mongo
