// Validate registration works in a replica set
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

    const rst = new ReplSetTest({nodes: 2, nodeOptions: options});
    rst.startSet();
    rst.initiate();
    rst.awaitReplication();

    WaitForRegistration(rst.getPrimary());

    mock_web.waitRegisters(2);

    // Restart the secondary
    // Now we're going to shut down all nodes
    var s1 = rst.liveNodes.slaves[0];
    var s1Id = rst.getNodeId(s1);

    rst.stop(s1Id);
    rst.waitForState(s1, ReplSetTest.State.DOWN);

    rst.restart(s1Id);

    mock_web.waitRegisters(3);

    rst.stopSet();


    
    mock_web.stop();
})();
