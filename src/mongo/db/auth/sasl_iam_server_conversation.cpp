/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include "mongo/platform/basic.h"

#include "mongo/db/auth/sasl_iam_server_conversation.h"

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include "mongo/base/init.h"
#include "mongo/base/status.h"
#include "mongo/base/string_data.h"
#include "mongo/client/sasl_iam_server_protocol.h"
#include "mongo/client/sasl_iam_server_protocol_gen.h"
#include "mongo/db/auth/sasl_mechanism_policies.h"
#include "mongo/db/auth/sasl_mechanism_registry.h"
#include "mongo/db/auth/sasl_options.h"
#include "mongo/db/auth/user.h"
#include "mongo/util/net/http_client.h"
#include "mongo/util/text.h"

namespace pt = boost::property_tree;

namespace mongo {

StatusWith<std::tuple<bool, std::string>> SaslIAMServerMechanism::stepImpl(OperationContext* opCtx,
                                                                           StringData inputData) {
    if (_step >= 2 || _step < 0) {
        return Status(ErrorCodes::AuthenticationFailed,
                      str::stream() << "Invalid IAM authentication step: " << _step);
    }

    _step++;

    try {
        if (_step == 1) {
            return _firstStep(opCtx, inputData);
        }

        return _secondStep(opCtx, inputData);
    } catch (DBException&) {
        return exceptionToStatus();
    }
}

StatusWith<std::tuple<bool, std::string>> SaslIAMServerMechanism::_firstStep(
    OperationContext* opCtx, StringData inputData) {

    std::string outputData = SaslIAMServerProtocol::generateServerFirst(inputData, &_serverNonce);

    return std::make_tuple(false, std::move(outputData));
}

StatusWith<std::tuple<bool, std::string>> SaslIAMServerMechanism::_secondStep(
    OperationContext* opCtx, StringData inputData) {

    auto [headers, requestBody] = SaslIAMServerProtocol::parseClientSecond(inputData, _serverNonce);

    std::unique_ptr<HttpClient> request = HttpClient::create();

    ConstDataRange body(requestBody.c_str(), requestBody.size());
    request->setHeaders(headers);

    DataBuilder result = request->post(saslGlobalParams.awsSTSUrl, body);

    // Confirm authorization
    ConstDataRange cdr = result.getCursor();
    StringData output;
    cdr.readInto<StringData>(&output);

    ServerMechanismBase::_principalName = SaslIAMServerProtocolUtil::getUserId(output);

    /* Check user exists */
    UserName user(ServerMechanismBase::ServerMechanismBase::_principalName,
                  ServerMechanismBase::getAuthenticationDatabase());

    auto authManager = AuthorizationManager::get(opCtx->getServiceContext());

    auto swUser = authManager->acquireUser(opCtx, user);
    if (!swUser.isOK()) {
        return swUser.getStatus();
    }

    return std::make_tuple(true, std::string());
}

namespace {

GlobalSASLMechanismRegisterer<IAMServerFactory> iamRegisterer;

// TODO - make the registration conditional

// ServiceContext::ConstructorActionRegisterer cyrusSaslServerMechanismRegisterMechanisms{
//     "CyrusSaslRegisterMechanisms,",
//     {"CyrusSaslServerCore", "CyrusSaslAllPluginsRegistered",
//     "CreateSASLServerMechanismRegistry"},
//     [](ServiceContext* service) {
//         auto& registry = SASLServerMechanismRegistry::get(service);
//         if (registry.registerFactory<CyrusGSSAPIServerFactory>()) {
//         }

//         // The PLAIN variant of Cyrus is not registered direcly.
//         // Rather, it is dispatched to by PLAINServerFactoryProxy in
//         // ldap_sasl_authentication_session.cpp
//         cyrusPlainServerFactory.create("test");
//     }};


}  // namespace
}  // namespace mongo
