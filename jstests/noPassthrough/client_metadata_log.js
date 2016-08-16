/**
 * Test that verifies client metadata is logged into log file on new connections.
 */
(function() {
    'use strict';

    // Test MongoD
    let TestMongoD = function(){
        let conn = MongoRunner.runMongod( 
            {useLogFiles: true}
        );
        assert.neq(null, conn, 'mongod was unable to start up');

        let testDb = conn.getDB("test");
        let coll = conn.getCollection("test.foo");
        coll.insert({_id : 1});

        print("Checking log file for message: " + conn.fullOptions.logFile);
        let log = cat(conn.fullOptions.logFile);
        assert(/received client metadata from .*: { application: { name: ".*" }, driver: { name: ".*", version: ".*" }, os: { type: ".*", name: ".*", architecture: ".*", version: ".*" } }/.test(log), "received client metadata log line missing in mongod log file!");

        MongoRunner.stopMongod(conn);
    };

    // Test MongoS
    let TestMongoS = function(){
        let options = {

            mongosOptions: {useLogFiles: true},
            configOptions: {},
            shardOptions: {},
            // TODO: SERVER-24163 remove after v3.4
            waitForCSRSSecondaries: false
        };

        let st = new ShardingTest({shards: 1, mongos: 1, other: options});

        let conn = st.s0;
        let testDb = conn.getDB("test");
        let coll = conn.getCollection("test.foo");
        coll.insert({_id : 1});

        print("Checking log file for message: " + conn.fullOptions.logFile);
        let log = cat(conn.fullOptions.logFile);
        assert(/received client metadata from .*: { application: { name: ".*" }, driver: { name: ".*", version: ".*" }, os: { type: ".*", name: ".*", architecture: ".*", version: ".*" } }/.test(log), "received client metadata log line missing in mongos log file!");

        st.stop();
    };

    TestMongoD();
    TestMongoS();
})();

