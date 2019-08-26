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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kAccessControl

#include "mongo/platform/basic.h"

#include "mongo/db/auth/sasl_iam_server_conversation.h"

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "mongo/base/init.h"
#include "mongo/base/status.h"
#include "mongo/base/string_data.h"
#include "mongo/db/auth/sasl_mechanism_policies.h"
#include "mongo/db/auth/sasl_mechanism_registry.h"
#include "mongo/db/auth/sasl_options.h"
#include "mongo/db/auth/user.h"
#include "mongo/util/base64.h"
#include "mongo/util/log.h"
#include "mongo/util/net/http_client.h"
#include "mongo/util/text.h"
#include "mongo/client/sasl_iam_protocol.h"
#include "mongo/client/sasl_iam_gen.h"

namespace mongo {

StatusWith<std::tuple<bool, std::string>> SaslIAMServerMechanism::_getUserId(StringData request) {
    constexpr auto startTagArn = "<Arn>"_sd;
    constexpr auto closingTagArn = "</Arn>"_sd;
    constexpr auto startTagAccount = "<Account>"_sd;
    constexpr auto closingTagAccount = "</Account>"_sd;
    constexpr auto arnService = "arn:aws:"_sd;
    constexpr auto role = ":assumed-role/"_sd;
    constexpr auto user = ":user/"_sd;

    size_t arnStartIndex;
    size_t arnLength;
    size_t accountStartIndex;
    size_t accountLength;

    if (request.find(startTagArn) != std::string::npos &&
        request.find(closingTagArn) != std::string::npos &&
        request.find(startTagAccount) != std::string::npos &&
        request.find(closingTagAccount) != std::string::npos) {
        arnStartIndex = request.find(startTagArn) + startTagArn.size();
        arnLength = request.find(closingTagArn) - arnStartIndex;
        accountStartIndex = request.find(startTagAccount) + startTagAccount.size();
        accountLength = request.find(closingTagAccount) - accountStartIndex;
    } else {
        return Status(ErrorCodes::BadValue, str::stream() << "Could not retrieve user id");
    }

    StringData arn = request.substr(arnStartIndex, arnLength);
    StringData account = request.substr(accountStartIndex, accountLength);

    std::string getAssumedRole = str::stream() << arnService << "sts::" << account << role;
    std::string getUser = str::stream() << arnService << "iam::" << account << user;

    std::string userId;
    if (arn.find(getAssumedRole) == 0) {
        size_t endIdx = arn.find("/", getAssumedRole.size()) + 1;
        userId = str::stream() << arn.substr(0, endIdx) << "*";
    } else if (arn.find(getUser) == 0) {
        userId = arn.substr(0).toString();
    } else {
        return Status(ErrorCodes::BadValue, str::stream() << "Could not retrieve user id");
    }

    return std::make_tuple(true, userId);
}

StatusWith<std::tuple<bool, std::string>> SaslIAMServerMechanism::stepImpl(OperationContext* opCtx,
                                                                           StringData inputData) {
    _step++;

    if (_step > 2 || _step <= 0) {
        return Status(ErrorCodes::AuthenticationFailed,
                      str::stream() << "Invalid IAM authentication step: " << _step);
    }

    if (_step == 1) {
        return _firstStep(opCtx, inputData);
    }

    return _secondStep(opCtx, inputData);
}

/*
 * Parse client-first-message of the form:
 * n,a=authzid,r=client-nonce
 *
 * Generate server-first-message on the form:
 * r=client-nonce+server-nonce,s=server-salt
 */
StatusWith<std::tuple<bool, std::string>> SaslIAMServerMechanism::_firstStep(
    OperationContext* opCtx, StringData inputData) {
    warning() << "SASL INPUT: " << inputData;

    std::string outputData = SaslIAMProtocol::generateServerFirst(inputData);

    return std::make_tuple(false, std::move(outputData));
}
/*
 * Parse client-second-message of the form:
 * c=channel-binding(base64) | h=request-headers | a=request-auth-header |
 * r=client-nonce [ | extensions]
 */
StatusWith<std::tuple<bool, std::string>> SaslIAMServerMechanism::_secondStep(
    OperationContext* opCtx, StringData inputData) {

    warning() << "SASL INPUT: " << inputData;

    /* Retrieve arguments */
    constexpr auto requestUrl = "https://sts.amazonaws.com/"_sd;
    constexpr auto requestBody = "Action=GetCallerIdentity&Version=2011-06-15"_sd;

    // const auto nonce = StringData(input_args[3]).substr(2);
    // if (nonce != _nonce) {
    //     return Status(ErrorCodes::BadValue,
    //                   str::stream()
    //                       << "Unmatched IAM nonce received from client in second step, expected "
    //                       << _nonce << " but received " << nonce);
    // }

    /* Send request */
    auto clientSecond = SaslIAMProtocol::parseClientSecond(inputData);

    DataBuilder result;
    std::unique_ptr<HttpClient> request = HttpClient::create();

    std::vector<std::string> header;
    header.push_back("Content-Length:" + clientSecond.getHeaders().getContentLength());
    header.push_back("Content-Type:" + clientSecond.getHeaders().getContentType());
    header.push_back("Host:" + clientSecond.getHeaders().getHost());
    header.push_back("X-Amz-Date:" + clientSecond.getHeaders().getXAmzDate());
    if  (clientSecond.getHeaders().getXAmzSecurityToken()) {
        header.push_back("X-Amz-Security-Token:" + clientSecond.getHeaders().getXAmzSecurityToken().get());
    }
    header.push_back("X-Mongodb-Server-Salt:" + clientSecond.getHeaders().getXMongodbServerSalt());

    header.push_back("Authorization:" + clientSecond.getRequestAuthHeader());

    for(const auto& head:header ) {
        warning() << "HEADER1:" << head;
    }

    ConstDataRange body(requestBody.rawData(), requestBody.size());
    request->setHeaders(header);

    log() << "Attempting to send POST request" << std::endl;

    result = request->post(requestUrl, body);

    /* Parse reply */

    // Confirm authorization
    ConstDataRange cdr = result.getCursor();
    StringData output;
    auto status = cdr.readIntoNoThrow<StringData>(&output);

    if (!status.isOK()) {
        return Status(ErrorCodes::BadValue, str::stream() << "Authentication failed");
    }

    log() << "Response from AWS STS: " << output;

    // Need to confirm identity
    // TODO - constexpr auto securityTokenArg = "X-Amz-Security-Token:"_sd;
    // constexpr auto saltArg = "X-Mongodb-Server-Salt:"_sd;

    // TOdOD _ vaildate salt is same
    // const size_t startIndexSalt = requestHeader.find(saltArg);

    // if (startIndexSalt == std::string::npos ||
    //     requestHeader.substr(startIndexSalt + saltArg.size()) != _salt) {
    //     return Status(ErrorCodes::BadValue, str::stream() << "Invalid salt");
    // }

    // TODO - validate "X-Mongodb-Server-Salt:" is in SignedHeaders

    auto ret = _getUserId(output);

    if (!ret.isOK()) {
        return ret.getStatus();
    }
    ServerMechanismBase::_principalName = std::get<1>(ret.getValue());

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
}  // namespace
}  // namespace mongo
