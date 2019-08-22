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

namespace mongo {

SaslIAMClientConversation::SaslIAMClientConversation(SaslClientSession* saslClientSession)
    : SaslClientConversation(saslClientSession) {}

SHA256Block SaslIAMClientConversation::_getSignatureKey(std::string datestamp,
                                                        StringData region,
                                                        StringData service) {
    std::string key = "AWS4" + _secretKey;
    constexpr auto request = "aws4_request"_sd;
    SHA256Block kDateBlock =
        SHA256Block::computeHmac(reinterpret_cast<const uint8_t*>(key.c_str()),
                                 key.length(),
                                 reinterpret_cast<const uint8_t*>(datestamp.c_str()),
                                 datestamp.length());
    SHA256Block kRegionBlock =
        SHA256Block::computeHmac(kDateBlock.data(),
                                 kDateBlock.size(),
                                 reinterpret_cast<const uint8_t*>(region.rawData()),
                                 region.size());
    SHA256Block kServiceBlock =
        SHA256Block::computeHmac(kRegionBlock.data(),
                                 kRegionBlock.size(),
                                 reinterpret_cast<const uint8_t*>(service.rawData()),
                                 service.size());
    SHA256Block kSigningBlock =
        SHA256Block::computeHmac(kServiceBlock.data(),
                                 kServiceBlock.size(),
                                 reinterpret_cast<const uint8_t*>(request.rawData()),
                                 request.size());
    return kSigningBlock;
}

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
        getRoleRequest->get("http://169.254.169.254/latest/meta-data/iam/security-credentials");

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
        str::stream() << "http://169.254.169.254/latest/meta-data/iam/security-credentials/"
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

    if (!bsonExtractStringField(obj, "AccessKeyId", &_accessKey).isOK() ||
        !bsonExtractStringField(obj, "SecretAccessKey", &_secretKey).isOK() ||
        bsonExtractStringField(obj, "Token", &_securityToken).isOK()) {
        return false;
    }
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
    // Create text-based nonce as base64 encoding of a binary blob of length multiple of 3
    const int nonceLenQWords = 3;
    uint64_t binaryNonce[nonceLenQWords];

    std::unique_ptr<SecureRandom> sr(SecureRandom::create());

    binaryNonce[0] = sr->nextInt64();
    binaryNonce[1] = sr->nextInt64();
    binaryNonce[2] = sr->nextInt64();

    _clientNonce = base64::encode(reinterpret_cast<char*>(binaryNonce), sizeof(binaryNonce));

    StringBuilder sb;
    sb << "n,,r=" << _clientNonce;
    *outputData = sb.str();

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
    if (inputData.startsWith("m=")) {
        return Status(ErrorCodes::BadValue, "IAM required extensions not supported");
    }

    /* Retrieve arguments */
    const auto input_args = StringSplitter::split(inputData.toString(), ",");

    if (input_args.size() < 2) {
        return Status(ErrorCodes::BadValue,
                      str::stream()
                          << "Incorrect number of arguments for first IAM server message, got "
                          << input_args.size() << " expected at least 2");
    }

    if (!str::startsWith(input_args[0], "r=") || input_args[0].size() < 3) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << "Incorrect IAM client|server nonce: " << input_args[0]);
    }

    const auto nonce = input_args[0].substr(2);
    if (nonce != _clientNonce) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << "Server IAM nonce does not match client nonce: " << nonce);
    }

    if (!str::startsWith(input_args[1], "s=") || input_args[1].size() < 3) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << "Incorrect IAM salt: " << input_args[1]);
    }

    const auto salt = input_args[1].substr(2);

    /* Generate client-second-message */

    using namespace fmt::literals;
    constexpr auto method = "POST"_sd;

    constexpr auto service = "sts"_sd;
    constexpr auto region = "us-east-1"_sd;  // Need to locate region
    auto host = "{}.amazonaws.com"_format(service);

    constexpr auto contentType = "application/x-www-form-urlencoded; charset=utf-8"_sd;
    constexpr auto body = "Action=GetCallerIdentity&Version=2011-06-15"_sd;

    if (_saslClientSession->hasParameter(SaslClientSession::parameterIamAccessKey) &&
        _saslClientSession->hasParameter(SaslClientSession::parameterIamSecretKey)) {
        _getUserCredentials();
    } else {
        auto ret = _getEc2Credentials();
        if (!ret.isOK()) {
            return Status(ErrorCodes::BadValue, str::stream() << "Failed to acquire credentials");
        }
    }

    auto now = Date_t::now();

    constexpr auto timestampFormat = "%Y%m%dT%H%M%SZ"_sd;
    constexpr auto dateFormat = "%Y%m%d"_sd;

    std::string timestamp =
        mongo::TimeZoneDatabase::utcZone().formatDate(timestampFormat, Date_t::now());
    std::string datestamp =
        mongo::TimeZoneDatabase::utcZone().formatDate(dateFormat, Date_t::now());

    /* -- Task 1: Create a canonical request -- */

    constexpr auto canonicalUri = "/"_sd;
    constexpr auto canonicalQuery = ""_sd;

    StringBuilder canonicalHeadersBuilder;
    StringBuilder signedHeadersBuilder;

    canonicalHeadersBuilder << "content-type:{}\nhost:{}\nx-amz-date:{}\n"_format(
        contentType, host, timestamp);
    signedHeadersBuilder << "content-type;host;x-amz-date";

    if (!_securityToken.empty()) {
        canonicalHeadersBuilder << "x-amz-security-token:{}\n"_format(_securityToken);
        signedHeadersBuilder << ";x-amz-security-token";
    }

    canonicalHeadersBuilder << "x-mongodb-server-salt:{}\n"_format(salt);
    signedHeadersBuilder << ";x-mongodb-server-salt";

    std::string canonicalHeaders = canonicalHeadersBuilder.str();
    std::string signedHeaders = signedHeadersBuilder.str();

    std::string payloadHash =
        SHA256Block::computeHash(reinterpret_cast<const uint8_t*>(body.rawData()), body.size())
            .toHexString();

    std::string canonicalRequest = "{}\n{}\n{}\n{}\n{}\n{}"_format(
        method, "/", "", canonicalHeaders, signedHeaders, payloadHash);

    /* -- Task 2: Create the string to sign -- */

    std::string algorithm = "AWS4-HMAC-SHA256";
    std::string credentialScope = "{}/{}/{}/aws4_request"_format(datestamp, region, service);

    std::string stringToSign = "{}\n{}\n{}\n{}"_format(
        algorithm,
        timestamp,
        credentialScope,
        SHA256Block::computeHash(reinterpret_cast<const uint8_t*>(canonicalRequest.c_str()),
                                 canonicalRequest.length())
            .toHexString());

    /* -- Task 3: Calculate the signature -- */

    SHA256Block signingKey = _getSignatureKey(datestamp, region, service);

    std::string signature =
        SHA256Block::computeHmac(signingKey.data(),
                                 signingKey.size(),
                                 reinterpret_cast<const uint8_t*>(stringToSign.c_str()),
                                 stringToSign.length())
            .toHexString();

    /* -- Task 4: Add signing information to the request -- */

    std::string authorizationHeader = "{} Credential={}/{}, SignedHeaders={}, Signature={}"_format(
        algorithm, _accessKey, credentialScope, signedHeaders, signature);

    std::string authorization = "Authorization:{}"_format(authorizationHeader);

    StringBuilder headerBuilder;
    headerBuilder << "Content-Length:{},Content-Type:{},X-Amz-Date:{}"_format(
        std::to_string(body.size()), contentType, timestamp);

    if (!_securityToken.empty()) {
        headerBuilder << ",X-Amz-Security-Token:{}"_format(_securityToken);
    }

    headerBuilder << ",X-Mongodb-Server-Salt:{}"_format(salt);

    std::string header = headerBuilder.str();

    StringBuilder sb;
    sb << "c=biws | h=" << header << " | a=" << authorization << " | r=" << nonce;
    *outputData = sb.str();

    return true;
}

}  // namespace mongo
