// Validate registration works
//
load("jstests/free_mon/libs/free_mon.js");

(function() {
    'use strict';

    let mock_web = new FreeMonWebServer();

    mock_web.start();

    let options = {
        setParameter: "cloudFreeMonitoringEndpointURL=" + mock_web.getURL(),
        enableFreeMonitoring: "on",
        verbose: 1,
    };

    // TODO: vary node count???
    const rst = new ReplSetTest({nodes: 2, nodeOptions: options});
    rst.startSet();
    rst.initiate();
    rst.awaitReplication();

    sleep(20 * 1000);

    WaitForRegistration(rst.getPrimary());

    const stats = mock_web.queryStats();
    print(tojson(stats));

    assert.eq(stats.registers, 2);

    
    rst.stopSet();

    mock_web.stop();
})();
