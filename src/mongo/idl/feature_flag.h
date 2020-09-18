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

#pragma once

#include <string>
#include <vector>

#include "mongo/db/server_options.h"
#include "mongo/idl/server_parameter.h"
#include "mongo/idl/server_parameter_with_storage.h"

namespace mongo {

/**
 * FeatureFlag contains information about whether a feature flag is enabled and what version it was
 * finished.
 * TODO
 *
 * It is only set at startup.
 */
class FeatureFlag {
    friend class FeatureFlagServerParameter;

public:
    FeatureFlag(bool enabled, StringData version)
        : _enabled(enabled), _version(version.toString()) {}

    /**
     * Returns true if the flag is set to true and enabled for this FCV version.
     *
     * TODO - what if there is no version assigned to this flag?
     */
    bool isEnabled(const ServerGlobalParams::FeatureCompatibility& fcv) const;

    /**
     * Returns true if this flag is enabled regardless of the current FCV version.
     *
     * isEnabled() is prefered over this function since it will prevent upgrade/downgrade issues.
     */
    bool isEnabledAndIgnoreFCV() const;

    /**
     * Return the version associated with this feature flag.
     *
     * Throws if the feature is not enabled.
     */
    StringData getVersion() const;

private:
    void set(bool enabled);

private:
    bool _enabled;
    std::string _version;
};

/**
 * Specialization of ServerParameter for FeatureFlags used by IDL generator.
 */
class FeatureFlagServerParameter : public ServerParameter {
public:
    FeatureFlagServerParameter(StringData name, FeatureFlag& storage)
        : ServerParameter(ServerParameterSet::getGlobal(), name, true, false), _storage(storage) {}

    /**
     * Encode the setting into BSON object.
     *
     * Typically invoked by {getParameter:...} to produce a dictionary
     * of SCP settings.
     */
    void append(OperationContext* opCtx, BSONObjBuilder& b, const std::string& name) final;

    /**
     * Update the underlying value using a BSONElement
     *
     * Allows setting non-basic values (e.g. vector<string>)
     * via the {setParameter: ...} call.
     */
    Status set(const BSONElement& newValueElement) final;

    /**
     * Update the underlying value from a string.
     *
     * Typically invoked from commandline --setParameter usage.
     */
    Status setFromString(const std::string& str) final;

private:
    FeatureFlag& _storage;
};

}  // namespace mongo