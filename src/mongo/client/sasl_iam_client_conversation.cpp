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
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <string>
#include <time.h>

#include "mongo/base/string_data.h"
#include "mongo/bson/json.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/crypto/sha256_block.h"
#include "mongo/db/query/datetime/date_time_support.h"
#include "mongo/platform/random.h"
#include "mongo/util/base64.h"
#include "mongo/util/net/http_client.h"
#include "mongo/util/str.h"
#include "mongo/util/text.h"
#include "mongo/client/sasl_iam_protocol.h"
#include "mongo/client/sasl_iam_gen.h"

namespace mongo {

std::string getDefaultHost() {
    return "http://localhost:8000";
    //return "http://169.254.169.254";
}

SaslIAMClientConversation::SaslIAMClientConversation(SaslClientSession* saslClientSession)
    : SaslClientConversation(saslClientSession) {}

void SaslIAMClientConversation::_getUserCredentials() {
    _accessKey =
        _saslClientSession->getParameter(SaslClientSession::parameterIamAccessKey).toString();
    _secretKey =
        _saslClientSession->getParameter(SaslClientSession::parameterIamSecretKey).toString();
    _securityToken = "";
}

StatusWith<bool> SaslIAMClientConversation::_getEc2Credentials() {
    // We need to allow insecure http request as we need to retrieve credentials from
    // http://169.254.169.254/ which does not support https. This host is provided
    // by AWS.

    // Retrieve the role attached to the EC2 instance
    std::unique_ptr<HttpClient> getRoleRequest = HttpClient::create();
    getRoleRequest->allowInsecureHTTP(true);

    DataBuilder getRoleResult =
        getRoleRequest->get(getDefaultHost() + "/latest/meta-data/iam/security-credentials/");

    ConstDataRange cdrRole = getRoleResult.getCursor();
    StringData getRoleOutput;
    auto statusGetRole = cdrRole.readIntoNoThrow<StringData>(&getRoleOutput);

    if (!statusGetRole.isOK()) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << "Failed to retrieve EC2 instance role name");
    }

    std::string role = getRoleOutput.substr(0, getRoleOutput.find("\n")).toString();

    // Retrieve the temporary credentials of the EC2 instance
    std::unique_ptr<HttpClient> getRoleCredentialsRequest = HttpClient::create();

    getRoleCredentialsRequest->allowInsecureHTTP(true);

    DataBuilder getRoleCredentialsResult = getRoleCredentialsRequest->get(
        str::stream() << getDefaultHost() + "/latest/meta-data/iam/security-credentials/"
                      << role);

    ConstDataRange cdrCredentials = getRoleCredentialsResult.getCursor();
    StringData getRoleCredentialsOutput;
    auto statusGetCredentials =
        cdrCredentials.readIntoNoThrow<StringData>(&getRoleCredentialsOutput);

    if (!statusGetCredentials.isOK()) {
        return Status(ErrorCodes::BadValue,
                      str::stream()
                          << "Failed to retrieve EC2 instance role temprorary credentials");
    }

    BSONObj obj = fromjson(getRoleCredentialsOutput.toString());

    auto creds = Ec2SecurityCredentials::parse(IDLParserErrorContext("security-credentials"), obj);

    _accessKey = creds.getAccessKeyId().toString();
    _secretKey = creds.getSecretAccessKey().toString();
    _securityToken = creds.getToken().has_value() ? boost::optional<std::string>(creds.getToken()->toString()) : boost::none;

    return true;
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

/*
 * Generate client-first-message of the form:
 * n,a=authzid,r=client-nonce
 */
StatusWith<bool> SaslIAMClientConversation::_firstStep(std::string* outputData) {

    *outputData = SaslIAMProtocol::generateClientFirst();

    return false;
}

/*
 * Parse server-first-message of the form:
 * [reserved-mext ',']r=client-nonce+server-nonce,s=server-salt[,extensions]
 *
 * Generate client-second-message of the form:
 * c=channel-binding(base64) | h=request-headers | a=request-auth-header |
 * r=client-nonce+server-nonce
 *
 * The request-headers contains the following information:
 * Content-Length:<content-length>,Content-Type:<content-type>,X-Amz-Date:<timestamp>
 * X-Amz-Security-Token:<token>,X-Mongodb-Server-Salt:<salt>
 *
 * The request-auth-header contains the following information:
 * Authorization:<algorithm> Credential=<access-key>/<datestamp>/<region>/<service>/aws4_request,
 * SignedHeaders=content-type;host;x-amz-date;x-amz-security-token;x-mongodb-server-salt,
 * Signature=<signature>
 */
StatusWith<bool> SaslIAMClientConversation::_secondStep(StringData inputData,
                                                        std::string* outputData) {

    /* Generate client-second-message */

    // using namespace fmt::literals;
    // constexpr auto method = "POST"_sd;

    // constexpr auto service = "sts"_sd;
    // constexpr auto region = "us-east-1"_sd;  // Need to locate region
    //auto host = "{}.amazonaws.com"_format(service);

    // constexpr auto contentType = "application/x-www-form-urlencoded; charset=utf-8"_sd;
    // constexpr auto body = "Action=GetCallerIdentity&Version=2011-06-15"_sd;

    if (_saslClientSession->hasParameter(SaslClientSession::parameterIamAccessKey) &&
        _saslClientSession->hasParameter(SaslClientSession::parameterIamSecretKey)) {
        _getUserCredentials();
    } else {
        auto ret = _getEc2Credentials();
        if (!ret.isOK()) {
            return Status(ErrorCodes::BadValue, str::stream() << "Failed to acquire credentials");
        }
    }

try {

    *outputData = SaslIAMProtocol::generateClientSecond(inputData, _accessKey, _secretKey, _securityToken);
} catch (DBException& dbe) {
    return exceptionToStatus();
}

    return true;
}

}  // namespace mongo
