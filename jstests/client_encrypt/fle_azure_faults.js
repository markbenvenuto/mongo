/**
 * Verify the GCP KMS implementation can handle a buggy KMS.
 */

load("jstests/client_encrypt/lib/mock_kms.js");
load('jstests/ssl/libs/ssl_helpers.js');

(function() {
"use strict";

const x509_options = {
    sslMode: "requireSSL",
    sslPEMKeyFile: SERVER_CERT,
    sslCAFile: CA_CERT
};

const mockKey = {
    keyName: "my_key",
    keyVaultEndpoint: "https://localhost:80",
};

const randomAlgorithm = "AEAD_AES_256_CBC_HMAC_SHA_512-Random";

const conn = MongoRunner.runMongod(x509_options);
const test = conn.getDB("test");
const collection = test.coll;

function runKMS(mock_kms, func) {
    mock_kms.start();

    const azureKMS = {
        tenantId: "my_tentant",
        clientId: "access@mongodb.com",
        clientSecret: "secret",
        identityPlatformEndpoint: mock_kms.getURL(),
    };

    const clientSideFLEOptions = {
        kmsProviders: {
            azure: azureKMS,
        },
        keyVaultNamespace: "test.coll",
        schemaMap: {},
    };

    const shell = Mongo(conn.host, clientSideFLEOptions);
    const cleanCacheShell = Mongo(conn.host, clientSideFLEOptions);

    collection.drop();

    func(shell, cleanCacheShell);

    mock_kms.stop();
}

// OAuth faults must be tested first so a cached token can't be used
function testBadOAuthRequestResult() {
    const mock_kms = new MockKMSServerAzure(FAULT_OAUTH, false);

    runKMS(mock_kms, (shell) => {
        const keyVault = shell.getKeyVault();

        const error = assert.throws(() => keyVault.createKey("azure", mockKey, ["mongoKey"]));
        assert.eq(
            error,
            "Error: code 9: FailedToParse: Expecting '{': offset:0 of:Internal Error of some sort.");
    });
}

testBadOAuthRequestResult();

function testBadOAuthRequestError() {
    const mock_kms = new MockKMSServerAzure(FAULT_OAUTH_CORRECT_FORMAT, false);

    runKMS(mock_kms, (shell) => {
        const keyVault = shell.getKeyVault();

        const error = assert.throws(() => keyVault.createKey("azure", mockKey, ["mongoKey"]));
        assert.commandFailedWithCode(error, [ErrorCodes.OperationFailed]);
        assert.eq(
            error,
            "Error: Failed to make oauth request: Azure OAuth Error : FAULT_OAUTH_CORRECT_FORMAT");
    });
}

testBadOAuthRequestError();

function testBadEncryptResult() {
    const mock_kms = new MockKMSServerAzure(FAULT_ENCRYPT, false);

    runKMS(mock_kms, (shell) => {
        const keyVault = shell.getKeyVault();
        mockKey.keyVaultEndpoint = mock_kms.getURL();

        assert.throws(() => keyVault.createKey("azure", mockKey, ["mongoKey"]));
        assert.eq(keyVault.getKeys("mongoKey").toArray().length, 0);
    });
}

testBadEncryptResult();

function testBadEncryptError() {
    const mock_kms = new MockKMSServerAzure(FAULT_ENCRYPT_CORRECT_FORMAT, false);

    runKMS(mock_kms, (shell) => {
        const keyVault = shell.getKeyVault();
        mockKey.keyVaultEndpoint = mock_kms.getURL();

        let error = assert.throws(() => keyVault.createKey("azure", mockKey, ["mongoKey"]));
        assert.commandFailedWithCode(error, [5256105]);
    });
}

testBadEncryptError();

function testBadDecryptResult() {
    const mock_kms = new MockKMSServerAzure(FAULT_DECRYPT, false);

    runKMS(mock_kms, (shell) => {
        const keyVault = shell.getKeyVault();
        mockKey.keyVaultEndpoint = mock_kms.getURL();

        const keyId = keyVault.createKey("azure", mockKey, ["mongoKey"]);
        const str = "mongo";
        assert.throws(() => {
            const encStr = shell.getClientEncryption().encrypt(keyId, str, randomAlgorithm);
        });
    });
}

testBadDecryptResult();

function testBadDecryptKeyResult() {
    const mock_kms = new MockKMSServerAzure(FAULT_DECRYPT_WRONG_KEY, true);

    runKMS(mock_kms, (shell, cleanCacheShell) => {
        const keyVault = shell.getKeyVault();
        mockKey.keyVaultEndpoint = mock_kms.getURL();

        keyVault.createKey("azure", mockKey, ["mongoKey"]);
        const keyId = keyVault.getKeys("mongoKey").toArray()[0]._id;
        const str = "mongo";
        const encStr = shell.getClientEncryption().encrypt(keyId, str, randomAlgorithm);

        mock_kms.enableFaults();

        assert.throws(() => {
            let str = cleanCacheShell.decrypt(encStr);
        });
    });
}

testBadDecryptKeyResult();

function testBadDecryptError() {
    const mock_kms = new MockKMSServerAzure(FAULT_DECRYPT_CORRECT_FORMAT, false);

    runKMS(mock_kms, (shell) => {
        const keyVault = shell.getKeyVault();
        mockKey.keyVaultEndpoint = mock_kms.getURL();

        keyVault.createKey("azure", mockKey, ["mongoKey"]);
        const keyId = keyVault.getKeys("mongoKey").toArray()[0]._id;
        const str = "mongo";
        let error = assert.throws(() => {
            const encStr = shell.getClientEncryption().encrypt(keyId, str, randomAlgorithm);
        });
        assert.commandFailedWithCode(error, [5256103]);
    });
}

testBadDecryptError();

MongoRunner.stopMongod(conn);
})();
