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

#include "mongo/client/sasl_iam_server_protocol.h"

#include <boost/algorithm/string/finder.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <iostream>

#include "mongo/base/data_range_cursor.h"
#include "mongo/base/data_type_validated.h"
#include "mongo/base/init.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/client/sasl_iam_server_protocol_gen.h"
#include "mongo/db/auth/sasl_options.h"
#include "mongo/platform/mutex.h"
#include "mongo/platform/random.h"
#include "mongo/rpc/object_check.h"

namespace mongo {

namespace {
// Secure Random for SASL IAM Nonce generation
Mutex saslIAMServerMutex;
SecureRandom saslIAMServerGen;

std::array<StringData, 10> allowedHeaders = {"content-length"_sd,
                                             "content-type"_sd,
                                             "host"_sd,
                                             "x-amz-date"_sd,
                                             "x-amz-security-token"_sd,
                                             SaslIAMProtocol::kMongoGS2CBHeader,
                                             "x-mongodb-optional-data"_sd,
                                             SaslIAMProtocol::kMongoServerNonceHeader};

constexpr auto kStsPrefix = "arn:aws:sts::"_sd;
constexpr auto kAssumedRole = "assumed-role/"_sd;
constexpr auto kUser = "user/"_sd;
constexpr auto signedHeadersStr = "SignedHeaders="_sd;

void validateSignedHeaders(StringData authHeader) {
    size_t pos = authHeader.find(signedHeadersStr);
    uassert(51728, "SignedHeaders missing from Authorization Header", pos != std::string::npos);

    size_t trailingComma = authHeader.find(',', pos);
    uassert(51729, "SignedHeaders missing trailing comma", trailingComma != std::string::npos);

    StringData signedHeaders = authHeader.substr(pos + signedHeadersStr.size(),
                                                 trailingComma - (pos + signedHeadersStr.size()));

    size_t headerIndex = 0;
    bool hasMongoDBGS2CbFlag = false;
    bool hasMonogDBServerNonce = false;

    for (auto partIt = boost::split_iterator<StringData::const_iterator>(
             signedHeaders.begin(), signedHeaders.end(), boost::token_finder([](char c) {
                 return c == ';';
             }));
         partIt != boost::split_iterator<StringData::const_iterator>();
         ++partIt) {
        StringData header((*partIt).begin(), (*partIt).end());
        uassert(51731, "Too many headers", headerIndex < allowedHeaders.size());

        if (header == SaslIAMProtocol::kMongoGS2CBHeader) {
            hasMongoDBGS2CbFlag = true;
        } else if (header == SaslIAMProtocol::kMongoServerNonceHeader) {
            hasMonogDBServerNonce = true;
        }

        if (header == allowedHeaders[headerIndex]) {
            // The header is expected, advance one and continue
            headerIndex++;
        } else {
            // The header is not expected, advance one and check again until we find a match
            // or we run out of allowed headers
            for (; headerIndex < (allowedHeaders.size()) && header != allowedHeaders[headerIndex];
                 headerIndex++) {
            }

            uassert(51732, "Did not find expected header", headerIndex < allowedHeaders.size());
        }
    }

    uassert(51733, "The x-mongodb-gs2-cb-flag header is missing", hasMongoDBGS2CbFlag);
    uassert(51734, "The x-mongodb-server-nonce header is missing", hasMonogDBServerNonce);
}

}  // namespace

std::array<char, 32> SaslIAMServerProtocol::generateServerNonce() {

    std::array<char, SaslIAMProtocol::kServerFirstNoncePieceLength> ret;

    {
        stdx::lock_guard<Latch> lk(saslIAMServerMutex);
        saslIAMServerGen.fill(&ret, ret.size());
    }

    return ret;
}

std::string SaslIAMServerProtocol::generateServerFirst(StringData clientFirstBase64,
                                                       std::vector<char>* serverNonce) {
    auto clientFirst = convertFromByteString<IamClientFirst>(clientFirstBase64);

    uassert(51273,
            "Nonce must be 32 bytes",
            clientFirst.getNonce().length() == SaslIAMProtocol::kClientFirstNonceLength);
    uassert(51274, "Channel Binding Prefix must be 'p'", clientFirst.getGs2_cb_flag() == 'n');

    auto serverNoncePiece = generateServerNonce();

    IamServerFirst first;

    serverNonce->reserve(SaslIAMProtocol::kServerFirstNonceLength);

    auto cdr = clientFirst.getNonce();
    std::copy(cdr.data(), cdr.data() + cdr.length(), std::back_inserter(*serverNonce));
    std::copy(serverNoncePiece.begin(), serverNoncePiece.end(), std::back_inserter(*serverNonce));

    first.setServerNonce(*serverNonce);
    first.setStsHost(saslGlobalParams.awsSTSHost);

    return convertToByteString(first);
}

std::tuple<std::vector<std::string>, std::string> SaslIAMServerProtocol::parseClientSecond(
    StringData clientSecondStr, const std::vector<char>& serverNonce) {
    auto clientSecond = convertFromByteString<IamClientSecond>(clientSecondStr);

    validateSignedHeaders(clientSecond.getAuthHeader());

    /* Retrieve arguments */
    constexpr auto requestBody = "Action=GetCallerIdentity&Version=2011-06-15"_sd;

    std::vector<std::string> headers;
    headers.push_back("Content-Length:43");
    headers.push_back("Content-Type:application/x-www-form-urlencoded");
    headers.push_back("Host:" + saslGlobalParams.awsSTSHost);
    headers.push_back("X-Amz-Date:" + clientSecond.getXAmzDate());

    if (clientSecond.getXAmzSecurityToken()) {
        headers.push_back("X-Amz-Security-Token:" + clientSecond.getXAmzSecurityToken().get());
    }

    headers.push_back(SaslIAMProtocol::kMongoServerNonceHeader + ":" +
                      base64::encode(serverNonce.data(), serverNonce.size()));
    headers.push_back(SaslIAMProtocol::kMongoGS2CBHeader + ":n");

    headers.push_back("Authorization:" + clientSecond.getAuthHeader());

    return {headers, requestBody.toString()};
}

std::string SaslIAMServerProtocolUtil::getUserId(StringData request) {
    std::stringstream istr(request.toString());

    boost::property_tree::ptree tree;

    boost::property_tree::read_xml(istr, tree);

    auto arnStr =
        tree.get_optional<std::string>("GetCallerIdentityResponse.GetCallerIdentityResult.Arn");

    uassert(51741, "Failed to parse GetCallerIdentityResponse", arnStr);

    return getSimplifiedARN(arnStr.get());
}

std::string SaslIAMServerProtocolUtil::getSimplifiedARN(StringData arn) {
    bool sts = arn.startsWith(kStsPrefix);
    bool iam = arn.startsWith("arn:aws:iam::");
    uassert(51735, "Incorrect ARN", sts || iam);

    // Skip past the account number
    size_t suffixPos = arn.find(':', kStsPrefix.size());
    uassert(51736, "Missing colon", suffixPos != std::string::npos);

    // Extract the suffix and verify it starts wtih assumed-role
    StringData suffix = arn.substr(suffixPos + 1);

    //
    if (iam) {
        uassert(51737, "Suffix", suffix.startsWith(kUser));
        return arn.toString();
    }

    uassert(51738, "Suffix", suffix.startsWith(kAssumedRole));

    // Find the second slash
    size_t starSuffixPos = suffix.find('/', kAssumedRole.size());
    uassert(51739, "Missing /", starSuffixPos != std::string::npos);

    // Check there are no other slashes
    size_t extraStarSuffixPos = suffix.find('/', starSuffixPos + 1);
    uassert(51740, "Extra /", extraStarSuffixPos == std::string::npos);

    // Build the final string to return
    size_t lastSlash = arn.rfind('/');
    auto ret = arn.substr(0, lastSlash + 1).toString();
    ret.push_back('*');

    return ret;
}

}  // namespace mongo
