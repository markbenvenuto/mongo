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

#pragma once

#include <boost/optional.hpp>
#include <string>
#include <vector>

#include "mongo/base/status_with.h"
#include "mongo/base/string_data.h"
#include "mongo/client/sasl_iam_protocol_common.h"
#include "mongo/platform/random.h"


namespace mongo {

/**
 * Class that handles the individual messages of IAM Auth conversation
 */
class SaslIAMServerProtocol {
public:
    static std::string generateServerFirst(StringData clientFirst, std::vector<char>* serverNonce);

    static std::tuple<std::vector<std::string>, std::string> parseClientSecond(
        StringData clientSecond, const std::vector<char>& serverNonce);

    static std::array<char, SaslIAMProtocol::kServerFirstNoncePieceLength> generateServerNonce();
};

class SaslIAMServerProtocolUtil {
public:
    /**
     * Example of a typical response
     * <GetCallerIdentityResponse xmlns="https://sts.amazonaws.com/doc/2011-06-15/">
     *   <GetCallerIdentityResult>
     *     <Arn>arn:aws:iam::NUMBER:user/USER_NAME</Arn>
     *     <UserId>HEX STRING</UserId>
     *     <Account>NUMBER</Account>
     *   </GetCallerIdentityResult>
     *   <ResponseMetadata>
     *     <RequestId>GUID</RequestId>
     *   </ResponseMetadata>
     * </GetCallerIdentityResponse>
     */
    static std::string getUserId(StringData request);

    /**
     * ARNS for IAM resources come in the following forms:
     *
     * User:
     *   arn:aws:iam::123456789:user/a.user.name
     *
     * EC2 Role:
     *   arn:aws:sts::123456789:assumed-role/<A_ROLE_NAME>/<i-ec2_instance>
     *
     * Assumed Role:
     *   arn:aws:sts::123456789:assumed-role/<A_ROLE_NAME>/<SESSION_NAME>
     *
     * Return
     * - Users - same as input
     * - Assume Rolee, EC2 Role - last component is changed to *
     *   - arn:aws:sts::123456789:assumed-role/<A_ROLE_NAME>/*
     *
     */
    static std::string getSimplifiedARN(StringData arn);
};

}  // namespace mongo