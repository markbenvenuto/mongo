/**
 * Test that verifies client metadata is logged as part of slow query loggging in MongoD
 */
(function() {
    'use strict';

    let conn = MongoRunner.runMongod({useLogFiles: true});
    assert.neq(null, conn, 'mongod was unable to start up');

    let testDb = conn.getDB("test");
    let coll = conn.getCollection("test.foo");
    coll.insert({_id: 1});

    // Do a really slow query beyond the 100ms threshold
    let c = coll.count({
        '$where': function() {
            sleep(1000);
            return true;
        }
    });
    assert.eq(c, 1, "expected 1 row");

    print("Checking log file for message: " + conn.fullOptions.logFile);
    let log = cat(conn.fullOptions.logFile);
    assert(
        /COMMAND .* command test.foo appName:MongoDB Shell command: count { count: "foo", query: { \$where: function ()/
            .test(log),
        "received client metadata log line missing in mongod log file!");

    MongoRunner.stopMongod(conn);

})();
