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

#include "mongo/client/sasl_iam_client_protocol.h"
#include "mongo/client/sasl_iam_protocol_common.h"
#include "mongo/client/sasl_iam_server_protocol.h"

#include "mongo/db/auth/sasl_options.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/base64.h"
#include "mongo/util/kms_message_support.h"

namespace mongo {
namespace {

static const AWSCredentials defaultCredentials("FAKEFAKEFAKEFAKEFAKE",
                                               "FAKEFAKEFAKEFAKEFAKEfakefakefakefakefake");

// Positive: Test a simple succesful conversation
TEST(SaslIamProtocol, Basic_Success) {
    // Boot KMS message init so that the Windows Crypto is setup
    kms_message_init();
    saslGlobalParams.awsSTSHost = "dummy";

    std::vector<char> clientNonce;
    auto clientFirst = SaslIAMClientProtocol::generateClientFirst(&clientNonce);
    std::vector<char> serverNonce;

    auto serverFirst = SaslIAMServerProtocol::generateServerFirst(clientFirst, &serverNonce);

    auto clientSecond =
        SaslIAMClientProtocol::generateClientSecond(serverFirst, clientNonce, defaultCredentials);

    auto httpTuple = SaslIAMServerProtocol::parseClientSecond(clientSecond, serverNonce);
}

// Positive: Test the ARN is extracted correctly from XML
TEST(SaslIamProtocol, Xml_Good) {
    auto str1 = R"(<GetCallerIdentityResponse xmlns="https://sts.amazonaws.com/doc/2011-06-15/">
   <GetCallerIdentityResult>
     <Arn>arn:aws:iam::NUMBER:user/USER_NAME</Arn>
     <UserId>HEX STRING</UserId>
     <Account>NUMBER</Account>
   </GetCallerIdentityResult>
   <ResponseMetadata>
     <RequestId>GUID</RequestId>
   </ResponseMetadata>
 </GetCallerIdentityResponse>)";

    ASSERT_EQUALS("arn:aws:iam::NUMBER:user/USER_NAME", SaslIAMServerProtocolUtil::getUserId(str1));
}

// Negative: Fail properly on incorrect xml
TEST(SaslIamProtocol, Xml_Bad) {
    auto str1 = R"(Foo)";

    ASSERT_THROWS(SaslIAMServerProtocolUtil::getUserId(str1), std::exception);
}


// Negative: Fail properly on xml missing the information
TEST(SaslIamProtocol, Xml_Bad_Partial) {
    auto str1 = R"(<GetCallerIdentityResponse xmlns="https://sts.amazonaws.com/doc/2011-06-15/">
   <GetCallerIdentityResult>
     <UserId>HEX STRING</UserId>
     <Account>NUMBER</Account>
   </GetCallerIdentityResult>
   <ResponseMetadata>
     <RequestId>GUID</RequestId>
   </ResponseMetadata>
 </GetCallerIdentityResponse>)";

    ASSERT_THROWS_CODE(SaslIAMServerProtocolUtil::getUserId(str1), AssertionException, 51741);
}


// Negative: Server rejects when the ClientFirst message nonce is the wrong length
TEST(SaslIamProtocol, ClientFirst_ShortNonce) {
    IamClientFirst clientFirst;

    clientFirst.setNonce(std::vector<char>{0x1, 0x2});
    clientFirst.setGs2_cb_flag(static_cast<int>('n'));

    std::vector<char> serverNonce;

    ASSERT_THROWS_CODE(
        SaslIAMServerProtocol::generateServerFirst(convertToByteString(clientFirst), &serverNonce),
        AssertionException,
        51273);
}

// Negative: Server rejects when the ClientFirst has the wrong channel prefix flag
TEST(SaslIamProtocol, ClientFirst_ChannelPrefix) {
    IamClientFirst clientFirst;

    clientFirst.setNonce(std::vector<char>(32, 0));
    clientFirst.setGs2_cb_flag(static_cast<int>('p'));

    std::vector<char> serverNonce;

    ASSERT_THROWS_CODE(
        SaslIAMServerProtocol::generateServerFirst(convertToByteString(clientFirst), &serverNonce),
        AssertionException,
        51274);
}

// Negative: Client rejects when the ServerFirst has a short server nonce
TEST(SaslIamProtocol, ServerFirst_ShortNonce) {
    std::vector<char> clientNonce;
    auto clientFirst = SaslIAMClientProtocol::generateClientFirst(&clientNonce);

    IamServerFirst serverFirst;
    serverFirst.setServerNonce(std::vector<char>{0x1, 0x2});
    serverFirst.setStsHost("dummy");

    ASSERT_THROWS_CODE(SaslIAMClientProtocol::generateClientSecond(
                           convertToByteString(serverFirst), clientNonce, defaultCredentials),
                       AssertionException,
                       51270);
}


// Negative: Client rejects when the ServerFirst has the wrong client nonce
TEST(SaslIamProtocol, ServerFirst_WrongNonce) {
    std::vector<char> clientNonce;
    auto clientFirst = SaslIAMClientProtocol::generateClientFirst(&clientNonce);

    IamServerFirst serverFirst;

    auto serverNoncePiece = SaslIAMServerProtocol::generateServerNonce();

    std::vector<char> serverNonce;
    std::copy(serverNoncePiece.begin(), serverNoncePiece.end(), std::back_inserter(serverNonce));
    std::copy(serverNoncePiece.begin(), serverNoncePiece.end(), std::back_inserter(serverNonce));

    serverFirst.setServerNonce(serverNonce);
    serverFirst.setStsHost("dummy");

    ASSERT_THROWS_CODE(SaslIAMClientProtocol::generateClientSecond(
                           convertToByteString(serverFirst), clientNonce, defaultCredentials),
                       AssertionException,
                       51271);
}

void parseServerFirstWithHost(StringData host) {
    std::vector<char> clientNonce;
    auto clientFirst = SaslIAMClientProtocol::generateClientFirst(&clientNonce);

    IamServerFirst serverFirst;

    auto serverNoncePiece = SaslIAMServerProtocol::generateServerNonce();

    std::vector<char> serverNonce;
    std::copy(clientNonce.begin(), clientNonce.end(), std::back_inserter(serverNonce));
    std::copy(serverNoncePiece.begin(), serverNoncePiece.end(), std::back_inserter(serverNonce));

    serverFirst.setServerNonce(serverNonce);
    serverFirst.setStsHost(host);

    SaslIAMClientProtocol::generateClientSecond(
        convertToByteString(serverFirst), clientNonce, defaultCredentials);
}


// Negative: Client rejects when the ServerFirst has empty host name
TEST(SaslIamProtocol, ServerFirst_BadHost_Empty) {
    ASSERT_THROWS_CODE(parseServerFirstWithHost(""), AssertionException, 51276);
}


// Negative: Client rejects when the ServerFirst has long host name
TEST(SaslIamProtocol, ServerFirst_BadHost_LongName) {
    ASSERT_THROWS_CODE(parseServerFirstWithHost(std::string(256, 'a')), AssertionException, 51276);
}


// Negative: Client rejects when the ServerFirst has empty dns part
TEST(SaslIamProtocol, ServerFirst_BadHost_EmptyDnsComponent) {
    ASSERT_THROWS_CODE(parseServerFirstWithHost("empty..dns.component"), AssertionException, 51277);
}

void parseWithCustomAuthHeader(StringData authHeader) {
    std::vector<char> clientNonce;
    auto clientFirst = SaslIAMClientProtocol::generateClientFirst(&clientNonce);
    std::vector<char> serverNonce;

    auto serverFirst = SaslIAMServerProtocol::generateServerFirst(clientFirst, &serverNonce);

    IamClientSecond second;

    second.setAuthHeader(authHeader);

    second.setXAmzDate("FAKE");

    auto httpTuple =
        SaslIAMServerProtocol::parseClientSecond(convertToByteString(second), serverNonce);
}

// Negative: Missing basic, required HTTP headers
TEST(SaslIamProtocol, ClientSecond_BadAuth_MissingSignedHeaders) {
    ASSERT_THROWS_CODE(
        parseWithCustomAuthHeader(
            "FAKEFAKEFAKE/20191107/us-east-1/sts/aws4_request, "
            "Signature=ab62ce1c75f19c4c8b918b2ed63b46512765ed9b8bb5d79b374ae83eeac11f55"),
        AssertionException,
        51728);
}

// Negative: Missing a trailing comma after SignedHeaders
TEST(SaslIamProtocol, ClientSecond_BadAuth_MissingTrailingComma) {
    ASSERT_THROWS_CODE(
        parseWithCustomAuthHeader(
            "FAKEFAKEFAKE/20191107/us-east-1/sts/aws4_request, "
            "SignedHeaders=content-length;content-type;host;x-amz-date;x-mongodb-gs2-cb-flag "
            "Signature=ab62ce1c75f19c4c8b918b2ed63b46512765ed9b8bb5d79b374ae83eeac11f55"),
        AssertionException,
        51729);
}

// Negative: Missing either the x-mongodb-gs2-cb-flag or x-mongodb-server-nonce flags
TEST(SaslIamProtocol, ClientSecond_BadAuth_MissingRequiredField) {
    ASSERT_THROWS_CODE(
        parseWithCustomAuthHeader(
            "FAKEFAKEFAKE/20191107/us-east-1/sts/aws4_request, "
            "SignedHeaders=content-length;content-type;host;x-amz-date;x-mongodb-server-nonce, "
            "Signature=ab62ce1c75f19c4c8b918b2ed63b46512765ed9b8bb5d79b374ae83eeac11f55"),
        AssertionException,
        51733);

    ASSERT_THROWS_CODE(
        parseWithCustomAuthHeader(
            "FAKEFAKEFAKE/20191107/us-east-1/sts/aws4_request, "
            "SignedHeaders=content-length;content-type;host;x-amz-date;x-mongodb-gs2-cb-flag, "
            "Signature=ab62ce1c75f19c4c8b918b2ed63b46512765ed9b8bb5d79b374ae83eeac11f55"),
        AssertionException,
        51734);
}

// Negative: SignedHeaders has an extra header
TEST(SaslIamProtocol, ClientSecond_BadAuth_ExtraHeader) {
    ASSERT_THROWS_CODE(
        parseWithCustomAuthHeader(
            "FAKEFAKEFAKE/20191107/us-east-1/sts/aws4_request, "
            "SignedHeaders=content-length;content-type;host;x-amz-date;x-fake-field;x-mongodb-gs2-"
            "cb-flag;x-mongodb-server-nonce, "
            "Signature=ab62ce1c75f19c4c8b918b2ed63b46512765ed9b8bb5d79b374ae83eeac11f55"),
        AssertionException,
        51732);
}

// Negative: SignedHeaders has client binding headers which are wrong since we do not support
// channel bindings
TEST(SaslIamProtocol, ClientSecond_BadAuth_WrongBindings) {
    ASSERT_THROWS_CODE(
        parseWithCustomAuthHeader(
            "FAKEFAKEFAKE/20191107/us-east-1/sts/aws4_request, "
            "SignedHeaders=content-length;content-type;host;x-amz-date;x-mongodb-channel-type-"
            "prefix;x-mongodb-gs2-cb-flag;x-mongodb-server-nonce, "
            "Signature=ab62ce1c75f19c4c8b918b2ed63b46512765ed9b8bb5d79b374ae83eeac11f55"),
        AssertionException,
        51732);

    ASSERT_THROWS_CODE(
        parseWithCustomAuthHeader(
            "FAKEFAKEFAKE/20191107/us-east-1/sts/aws4_request, "
            "SignedHeaders=content-length;content-type;host;x-amz-date;x-mongodb-channel-binding-"
            "data;x-mongodb-gs2-cb-flag;x-mongodb-server-nonce, "
            "Signature=ab62ce1c75f19c4c8b918b2ed63b46512765ed9b8bb5d79b374ae83eeac11f55"),
        AssertionException,
        51732);
}

// Positive: EC2 instance metadata returns a valid string
TEST(SaslIAMClientProtocolUtil, ParseRole_Basic) {
    ASSERT_EQUALS("foo",
                  SaslIAMClientProtocolUtil::parseRoleFromEC2IamSecurityCredentials("foo\n"));
}

// Negative: EC2 instance metadata does not return a valid string
TEST(SaslIAMClientProtocolUtil, ParseRole_Bad) {
    ASSERT_THROWS_CODE(SaslIAMClientProtocolUtil::parseRoleFromEC2IamSecurityCredentials("foo"),
                       AssertionException,
                       51272);
}

// Positive: EC2 instance role metadata returns a valid json document
TEST(SaslIAMClientProtocolUtil, EC2ParseTemporaryCreds_Basic) {
    auto credsJson = R"({
    "Code" : "Success",
    "LastUpdated" : "DATE",
    "Type" : "AWS-HMAC",
    "AccessKeyId" : "ACCESS_KEY_ID",
    "SecretAccessKey" : "SECRET_ACCESS_KEY",
    "Token" : "SECURITY_TOKEN_STRING",
    "Expiration" : "EXPIRATION_DATE"
})";

    auto creds =
        SaslIAMClientProtocolUtil::parseCredentialsFromEC2IamSecurityCredentials(credsJson);
    ASSERT_EQUALS(creds.accessKeyId, "ACCESS_KEY_ID");
    ASSERT_EQUALS(creds.secretAccessKey, "SECRET_ACCESS_KEY");
    ASSERT_EQUALS(creds.sessionToken.get(), "SECURITY_TOKEN_STRING");
}

// Positive: ECS Task metadata returns a valid json document
TEST(SaslIAMClientProtocolUtil, ParseECSTemporaryCreds_Basic) {
    auto credsJson = R"({
    "AccessKeyId": "ACCESS_KEY_ID",
    "Expiration": "EXPIRATION_DATE",
    "RoleArn": "TASK_ROLE_ARN",
    "SecretAccessKey": "SECRET_ACCESS_KEY",
    "Token": "SECURITY_TOKEN_STRING"
})";

    auto creds = SaslIAMClientProtocolUtil::parseCredentialsFromECSTaskIamCredentials(credsJson);
    ASSERT_EQUALS(creds.accessKeyId, "ACCESS_KEY_ID");
    ASSERT_EQUALS(creds.secretAccessKey, "SECRET_ACCESS_KEY");
    ASSERT_EQUALS(creds.sessionToken.get(), "SECURITY_TOKEN_STRING");
}


// Positive: Test Region extraction
TEST(SaslIAMClientProtocolUtil, TestRegions) {
    ASSERT_EQUALS("us-east-1", SaslIAMClientProtocolUtil::getRegionFromHost("sts.amazonaws.com"));
    ASSERT_EQUALS("us-east-1", SaslIAMClientProtocolUtil::getRegionFromHost("first"));
    ASSERT_EQUALS("second", SaslIAMClientProtocolUtil::getRegionFromHost("first.second"));
    ASSERT_EQUALS("second", SaslIAMClientProtocolUtil::getRegionFromHost("first.second.third"));
    ASSERT_EQUALS("us-east-2",
                  SaslIAMClientProtocolUtil::getRegionFromHost("sts.us-east-2.amazonaws.com"));
}

// Positive: Test ARN is converted properly
TEST(SaslIAMServerProtocolUtil, ARN_Good) {
    ASSERT_EQUALS(
        "arn:aws:iam::123456789:user/a.user.name",
        SaslIAMServerProtocolUtil::getSimplifiedARN("arn:aws:iam::123456789:user/a.user.name"));
    ASSERT_EQUALS("arn:aws:sts::123456789:assumed-role/ROLE/*",
                  SaslIAMServerProtocolUtil::getSimplifiedARN(
                      "arn:aws:sts::123456789:assumed-role/ROLE/i-a0912374abc"));
    ASSERT_EQUALS("arn:aws:sts::123456789:assumed-role/ROLE/*",
                  SaslIAMServerProtocolUtil::getSimplifiedARN(
                      "arn:aws:sts::123456789:assumed-role/ROLE/a.session"));
}

// Negative: Bad ARN fail
TEST(SaslIAMServerProtocolUtil, ARN_Bad) {
    // Wrong service
    ASSERT_THROWS_CODE(
        SaslIAMServerProtocolUtil::getSimplifiedARN("arn:aws:fake::123456789:role/a.user.name"),
        AssertionException,
        51735);
    // Runt
    ASSERT_THROWS_CODE(SaslIAMServerProtocolUtil::getSimplifiedARN("arn:aws:iam::123456789"),
                       AssertionException,
                       51736);
    // Wrong suffix for IAM
    ASSERT_THROWS_CODE(
        SaslIAMServerProtocolUtil::getSimplifiedARN("arn:aws:iam::123456789:role/a.user.name"),
        AssertionException,
        51737);

    // Missing / in suffix
    ASSERT_THROWS_CODE(SaslIAMServerProtocolUtil::getSimplifiedARN("arn:aws:sts::123456789:role"),
                       AssertionException,
                       51738);

    // Missing two /
    ASSERT_THROWS_CODE(
        SaslIAMServerProtocolUtil::getSimplifiedARN("arn:aws:sts::123456789:assumed-role/foo"),
        AssertionException,
        51739);

    // Extra /
    ASSERT_THROWS_CODE(SaslIAMServerProtocolUtil::getSimplifiedARN(
                           "arn:aws:sts::123456789:assumed-role/foo/bar/stuff"),
                       AssertionException,
                       51740);
}

}  // namespace
}  // namespace mongo
