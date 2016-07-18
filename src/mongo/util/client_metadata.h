/**
 * Copyright (C) 2016 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#pragma once

#include <string>

#include "mongo/base/status.h"
#include "mongo/base/status_with.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"

namespace mongo {

class Client;
class ClientBasic;
class OperationContext;

/**
 * ClientMetadata is responsible for parsing the client metadata document that is received in
 * isMaster from clients.
 *
 * Example document:
 * {
 *    "isMaster" : 1,
 *    "meta" : {
 *        "application" : {              // Optional
 *            "name" : "string"          // Optional with caveats
 *        },
 *        "driver" : {                   // Required
 *            "name" : "string",         // Required
 *            "version" : "string"       // Required
 *        },
 *        "os" : {                       // Optional, Informational Only
 *            "type" : "string",         // Optional, Informational Only
 *            "name" : "string",         // Optional, Informational Only
 *            "architecture" : "string", // Optional, Informational Only
 *            "version" : "string"       // Optional, Informational Only
 *        }
 *    }
 * }
 *
 * For this classes' purposes, the client metadata document is the sub-document in "meta". It is
 * allowed to contain additional fields that are simply ignored not in the example above. The "os"
 * document is optional and for informational purposes only. The content is logged to disk but
 * otherwise ignored.
 * See Driver Specification: Client Metadata Capture for more information.
 *
 * This class is also used by client libraries to create a valid client metadata document.
 */
class ClientMetadata {
public:
    /**
     * Parse and validate a client metadata document contained in an isMaster request.
     *
     * Empty or non-existent sub-documents are permitted. Non-empty documents are required to have
     * the fields driver.name, and driver.version which must be strings.
     *
     * Returns true if it found a document, false if no document was found.
     */
    StatusWith<bool> parseIsMasterReply(const BSONObj& doc);

    /**
     * Create a new client metadata document with os information from the ProcessInfo class.
     */
    static void serialize(StringData driverName, StringData driverVersion, BSONObjBuilder& builder);

    /**
     * Create a new client metadata document with os information from the ProcessInfo class.
     *
     * driverName - name of the driver, must not be empty
     * driverVersion - a string for the driver version, must not be empty
     *
     * Notes: appName must be <= 128 bytes otherwise an error is returned. It may be empty in which
     * case it is omitted from the output document.
     */
    static Status serialize(StringData driverName,
                            StringData driverVersion,
                            StringData appName,
                            BSONObjBuilder& builder);

    /**
     * Get the Application Name for the client metadata document.
     *
     * Used to log Application Name in slow operation reports, and into system.profile.
     * Return: May be empty.
     */
    StringData getApplicationName() const;

    /**
     * Get the BSON Document of the client metadata document. In the example above in the class
     * comment, this is the document in the "meta" field.
     *
     * Return: May be empty.
     */
    const BSONObj& getDocument() const;

    /**
    * Log the client metadata to disk if it has been set.
    */
    void logClientMetadata(Client* client) const;

    /**
     * Get a ClientMetadata object that is attached via decoration to a ClientBasic object.
     */
    static ClientMetadata* get(ClientBasic& client);
    static ClientMetadata* get(ClientBasic* client);

    static ClientMetadata* get(OperationContext* client);
    
    static StringData fieldName() {
        return "$client";
    }

    static Status readFromMetadata(OperationContext* txn, BSONElement& elem);
    static void writeToMetadata(OperationContext* txn, BSONObjBuilder* builder);

    // TODO REMOVE
    bool seen() const {
        return _sawIsMaster;
    }

public:
    /**
     * Create a new client metadata document.
     *
     * Exposed for Unit Test purposes
     */
    static void serialize(StringData driverName,
                          StringData driverVersion,
                          StringData osType,
                          StringData osName,
                          StringData osArchitecture,
                          StringData osVersion,
                          BSONObjBuilder& builder);

    /**
     * Create a new client metadata document.
     *
     * driverName - name of the driver
     * driverVersion - a string for the driver version
     * osType - name of host operating system of client, i.e. uname -s
     * osName - name of operating system distro, i.e. "Ubuntu..." or "Microsoft Windows 8"
     * osArchitecture - architecture of host operating system, i.e. uname -p
     * osVersion - operating system version, i.e. uname -v
     *
     * Notes: appName must be <= 128 bytes otherwise an error is returned. It may be empty in which
     * case it is omitted from the output document. All other fields must not be empty.
     *
     * Exposed for Unit Test purposes
     */
    static Status serialize(StringData driverName,
                            StringData driverVersion,
                            StringData osType,
                            StringData osName,
                            StringData osArchitecture,
                            StringData osVersion,
                            StringData appName,
                            BSONObjBuilder& builder);

private:
    Status parseClientMetadataDocument(const BSONObj& doc);
    static Status validateDriverDocument(const BSONObj& doc);
    Status parseApplicationDocument(const BSONObj& doc);

private:
    // Parsed Client Metadata document
    // May be empty
    // Owned
    BSONObj _document;

    // Application Name extracted from the client metadata document.
    // May be empty
    StringData _appName;

    // Flag to indicate whether we saw isMaster at least once.
    bool _sawIsMaster{false};
};

}  // namespace mongo
