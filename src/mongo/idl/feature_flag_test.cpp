/**
 *    Copyright (C) 2020-present MongoDB, Inc.
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

#include "mongo/idl/feature_flag_test_gen.h"
#include "mongo/unittest/unittest.h"

namespace mongo {

namespace {

ServerParameter* getServerParameter(const std::string& name) {
    const auto& spMap = ServerParameterSet::getGlobal()->getMap();
    const auto& spIt = spMap.find(name);
    ASSERT(spIt != spMap.end());

    auto* sp = spIt->second;
    ASSERT(sp);
    return sp;
}

TEST(IDLFeatureFlag, Basic) {
    // true is set by "default" attribute in the IDL file.
    ASSERT(feature_flags::gFeatureFlagToaster.isEnabledAndIgnoreFCV() == true);

    auto* featureFlagToaster = getServerParameter("featureFlagToaster");
    ASSERT_OK(featureFlagToaster->setFromString("false"));
    ASSERT(feature_flags::gFeatureFlagToaster.isEnabledAndIgnoreFCV() == false);
    ASSERT_NOT_OK(featureFlagToaster->setFromString("alpha"));
}

}  // namespace
}  // namespace mongo
