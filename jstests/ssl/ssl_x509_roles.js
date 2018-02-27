var SERVER_CERT = "jstests/libs/server.pem";
var CA_CERT = "jstests/libs/ca.pem";
var CLIENT_CERT = "jstests/libs/client_roles.pem";

var SERVER_USER = "C=US,ST=New York,L=New York City,O=MongoDB,OU=Kernel,CN=server";
var CLIENT_USER = "C=US,ST=New York,L=New York City,O=MongoDB,OU=Kernel Users,CN=Kernel Client Peer Role";

function authAndTest(mongod) {
    var mongo = runMongoProgram("mongo",
                                "--host",
                                "localhost",
                                "--port",
                                mongod.port,
                                "--ssl",
                                "--sslCAFile",
                                CA_CERT,
                                "--sslPEMKeyFile",
                                CLIENT_CERT,
                                "jstests/ssl/libs/ssl_x509_role_auth.js"
                                );

    // runMongoProgram returns 0 on success
    assert.eq(
        0,
        mongo,
        "Connection attempt failed FOOOOOAR");

}

print("1. Testing x.509 auth to mongod");
var x509_options = {sslMode: "requireSSL", sslPEMKeyFile: SERVER_CERT, sslCAFile: CA_CERT};

var mongo = MongoRunner.runMongod(Object.merge(x509_options, {auth: ""}));

authAndTest(mongo);
MongoRunner.stopMongod(mongo);

// print("2. Testing x.509 auth to mongos");

// var st = new ShardingTest({
//     shards: 1,
//     mongos: 1,
//     other: {
//         keyFile: 'jstests/libs/key1',
//         configOptions: x509_options,
//         mongosOptions: x509_options,
//         shardOptions: x509_options,
//         useHostname: false,
//     }
// });

// authAndTest(new Mongo("localhost:" + st.s0.port));
// st.stop();
