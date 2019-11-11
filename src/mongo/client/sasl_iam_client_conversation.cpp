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

#include "mongo/client/sasl_iam_client_conversation.h"

#include <boost/algorithm/string/replace.hpp>
#include <string>
#include <time.h>

#include "mongo/base/string_data.h"
#include "mongo/bson/json.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/client/sasl_iam_client_options.h"
#include "mongo/client/sasl_iam_client_protocol.h"
#include "mongo/client/sasl_iam_client_protocol_gen.h"
#include "mongo/crypto/sha256_block.h"
#include "mongo/db/query/datetime/date_time_support.h"
#include "mongo/platform/random.h"
#include "mongo/util/base64.h"
#include "mongo/util/net/http_client.h"
#include "mongo/util/str.h"
#include "mongo/util/text.h"

namespace mongo {

SASLIamClientGlobalParams saslIamClientGlobalParams;

std::string getDefaultHost() {
    return saslIamClientGlobalParams.awsEC2Url;
}

SaslIAMClientConversation::SaslIAMClientConversation(SaslClientSession* saslClientSession)
    : SaslClientConversation(saslClientSession) {}

AWSCredentials SaslIAMClientConversation::_getCredentials() const {

    if (_saslClientSession->hasParameter(SaslClientSession::parameterUser) &&
        _saslClientSession->hasParameter(SaslClientSession::parameterPassword)) {
        return _getUserCredentials();
    } else {
        return _getLocalAWSCredentials();
    }
}

AWSCredentials SaslIAMClientConversation::_getUserCredentials() const {
    // TODO - support temp creds
    return AWSCredentials(
        _saslClientSession->getParameter(SaslClientSession::parameterUser).toString(),
        _saslClientSession->getParameter(SaslClientSession::parameterPassword).toString());
}

AWSCredentials SaslIAMClientConversation::_getLocalAWSCredentials() const {
    // TODO - branch on the environment variable that ECS tasks set
    return _getEc2Credentials();
}

AWSCredentials SaslIAMClientConversation::_getEc2Credentials() const {
    std::unique_ptr<HttpClient> httpClient = HttpClient::create();
    httpClient->allowInsecureHTTP(true);

    // Retrieve the role attached to the EC2 instance
    DataBuilder getRoleResult =
        httpClient->get(getDefaultHost() + "/latest/meta-data/iam/security-credentials/");

    ConstDataRange cdrRole = getRoleResult.getCursor();
    StringData getRoleOutput;
    cdrRole.readInto<StringData>(&getRoleOutput);

    std::string role =
        SaslIAMClientProtocolUtil::parseRoleFromEC2IamSecurityCredentials(getRoleOutput);

    // Retrieve the temporary credentials of the EC2 instance
    DataBuilder getRoleCredentialsResult = httpClient->get(
        str::stream() << getDefaultHost() + "/latest/meta-data/iam/security-credentials/" << role);

    ConstDataRange cdrCredentials = getRoleCredentialsResult.getCursor();
    StringData getRoleCredentialsOutput;
    cdrCredentials.readInto<StringData>(&getRoleCredentialsOutput);

    return SaslIAMClientProtocolUtil::parseCredentialsFromEC2IamSecurityCredentials(
        getRoleCredentialsOutput);
}

StatusWith<bool> SaslIAMClientConversation::step(StringData inputData, std::string* outputData) {
    _step++;

    switch (_step) {
        case 1:
            return _firstStep(outputData);
        case 2:
            return _secondStep(inputData, outputData);
        default:
            return StatusWith<bool>(ErrorCodes::AuthenticationFailed,
                                    str::stream() << "Invalid IAM authentication step: " << _step);
    }
}

StatusWith<bool> SaslIAMClientConversation::_firstStep(std::string* outputData) {

    *outputData = SaslIAMClientProtocol::generateClientFirst(&_clientNonce);

    return false;
}

StatusWith<bool> SaslIAMClientConversation::_secondStep(StringData inputData,
                                                        std::string* outputData) {

    try {
        auto credentials = _getCredentials();

        *outputData =
            SaslIAMClientProtocol::generateClientSecond(inputData, _clientNonce, credentials);
    } catch (DBException&) {
        return exceptionToStatus();
    }

    return true;
}

}  // namespace mongo
