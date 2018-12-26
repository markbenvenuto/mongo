
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kBridge

#include "mongo/platform/basic.h"

#include "mongo/fle/mongofle_commands.h"

#include "mongo/base/init.h"
#include "mongo/base/status.h"
#include "mongo/base/status_with.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/db/wire_version.h"
#include "mongo/rpc/message.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/net/hostandport.h"
#include "mongo/util/string_map.h"

#include "mongo/db/matcher/extensions_callback_noop.h"
#include "mongo/db/query/canonical_query.h"
#include "mongo/db/query/query_request.h"
#include "mongo/util/log.h"

#include "mongo/db/matcher/expression_serialization_context.h"
#include "mongo/db/matcher/schema/json_schema_parser.h"

namespace mongo {

namespace {


class FLEExpressionSerializationContext : public ExpressionSerializationContext {
public:
    virtual boost::optional<std::vector<char>> encrypt(ElementPath path,
                                                       BSONElement element) final {
        const FieldRef& fieldRef = path.fieldRef();
        if (fieldRef.numParts() > 0) {
            if (fieldRef.getPart(0) == "_id") {
                return std::vector<char>{0x1, 0x2, 0x3, 0x4};
            }
        }

        return boost::none;
    }
};


class FLECmdFind final : public FLECommand {
public:
    StatusWith<BSONObj> extractJSONSchema(BSONObj obj, BSONObjBuilder* stripped){
        BSONObj ret;
        for(auto& e : obj ) {
            if( e.fieldNameStringData() == "$jsonSchema") {

                // TODO: add error checking
                ret = e.Obj();
            } else {
                stripped->append(e);
            }
        }

        if( ret.isEmpty() ) { 
            return Status(ErrorCodes::BadValue, "JSON SCHEMA IS MISSING!!!!");
        }

        stripped->doneFast();

        return ret;
    }

    Status run(const OpMsgRequest& request, BSONObjBuilder* builder) final {
        builder->append("query", request.body);


        BSONObjBuilder stripped;
        auto schema = uassertStatusOK(extractJSONSchema(request.body, &stripped));

        // Valdidate JSON Schema and get crypto crap
        JSONSchemaContext paths;
        auto swMatch = uassertStatusOK(JSONSchemaParser::parse(schema, false, &paths));

                BSONObjBuilder scratch2;

                swMatch->serialize(&scratch2, nullptr);
                log() << "SCHEMA: " << scratch2.obj();

        // Parse the command BSON to a QueryRequest.
        const bool isExplain = false;


        // Pass parseNs to makeFromFindCommand in case _request.body does not have a UUID.
        auto qr = uassertStatusOK(QueryRequest::makeFromFindCommand(
            NamespaceString("TODO.TODO"), stripped.obj(), isExplain));

        // CommandHelpers::parseNsOrUUID(_dbName,  _request.body)

        // Finish the parsing step by using the QueryRequest to create a CanonicalQuery.
        const boost::intrusive_ptr<ExpressionContext> expCtx;
        OperationContext* opCtx = nullptr;
        auto cq = uassertStatusOK(
            CanonicalQuery::canonicalize(opCtx,
                                         std::move(qr),
                                         expCtx,
                                         ExtensionsCallbackNoop(),
                                         MatchExpressionParser::kAllowAllSpecialFeatures));


        invariant(cq.get());

        log() << "Running query:\n" << redact(cq->toString());
        log() << "Running query: " << redact(cq->toStringShort());

        log() << "Foo" << cq->getQueryObj();
        {
            // TODO: use CursorResponse
            BSONObjBuilder cursor = builder->subobjStart("cursor");


            FLEExpressionSerializationContext fleContext;
            cursor.append("id", static_cast<long long>(0));
            cursor.append("ns", "ginore.me");

            {
                BSONArrayBuilder arrayBuilder(cursor.subarrayStart("firstBatch"));

                BSONObjBuilder scratch;

                cq->root()->serialize(&scratch, &fleContext);
                arrayBuilder.append(scratch.obj());
                // arrayBuilder.append(cq->getQueryObj());
            }
            cursor.done();
        }
        return Status::OK();
    }
};


class FLECmdIsMaster final : public FLECommand {
public:
    Status run(const OpMsgRequest& request, BSONObjBuilder* builder) final {

        builder->appendBool("ismaster", true);
        // builder->append("msg", "isdbgrid");
        builder->appendNumber("maxBsonObjectSize", BSONObjMaxUserSize);
        builder->appendNumber("maxMessageSizeBytes", MaxMessageSizeBytes);
        builder->appendDate("localTime", jsTime());

        // Mongos tries to keep exactly the same version range of the server for which
        // it is compiled.
        builder->append("maxWireVersion",
                        WireSpec::instance().incomingExternalClient.maxWireVersion);
        builder->append("minWireVersion",
                        WireSpec::instance().incomingExternalClient.minWireVersion);
        return Status::OK();
    }
};

class FLECmdBuildInfo final : public FLECommand {
public:
    Status run(const OpMsgRequest& request, BSONObjBuilder* builder) final {
        builder->append("version", "0.0.0");

        return Status::OK();
    }
};

class FLECmdGetLog final : public FLECommand {
public:
    Status run(const OpMsgRequest& request, BSONObjBuilder* builder) final {
        builder->append("totalLinesWritten", 0);
        BSONArrayBuilder arr(builder->subarrayStart("log"));
        arr.done();
        return Status::OK();
    }
};

StringMap<FLECommand*> fleCommandMap;

MONGO_INITIALIZER(RegisterFLECommands)(InitializerContext* context) {
    fleCommandMap["find"] = new FLECmdFind();
    fleCommandMap["isMaster"] = new FLECmdIsMaster();
    fleCommandMap["buildinfo"] = new FLECmdBuildInfo();
    fleCommandMap["buildInfo"] = new FLECmdBuildInfo();
    fleCommandMap["getLog"] = new FLECmdGetLog();
    return Status::OK();
}

}  // namespace

StatusWith<FLECommand*> FLECommand::findCommand(StringData cmdName) {
    auto it = fleCommandMap.find(cmdName);
    if (it != fleCommandMap.end()) {
        invariant(it->second);
        return it->second;
    }
    return {ErrorCodes::CommandNotFound, str::stream() << "Unknown command: " << cmdName};
}

FLECommand::~FLECommand() = default;

}  // namespace mongo
