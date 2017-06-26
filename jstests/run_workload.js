'use strict';

function workloadBenchmark(name, markThreadIdleMode, cache_size, threads, duration) {
    'use strict';

    var options = {
        setParameter:  "markThreadIdleMode=" + markThreadIdleMode,
        // setParameter: "tcmallocMaxTotalThreadCacheBytes=" + cache_size,
        slowms: 10000,
    };

    const conn = MongoRunner.runMongod(options);

    const port = conn.port;

    const dir_prefix = 'runs/' + name + '/';
    const file_name_stats = dir_prefix + "workload.stats";
    const name_data_dir = dir_prefix + "diagnostic_data";
    const diag_data_dir = conn.dbpath + "/diagnostic.data";
    mkdir(dir_prefix);

    // Set this parameter at one time since the options dictionary can only contain one setParameter
    var adminDB = conn.getDB('admin');
    var res = adminDB.runCommand({"setParameter": 1, "tcmallocMaxTotalThreadCacheBytes": cache_size});
    assert.commandWorked(res, 0);

    print(tojson(adminDB.serverStatus()));

    //let ret = run('/home/mark/tcmalloc/WorkLoad/a.out', '-h', 'localhost:17017', '-p', '1024', '-d', '1200');
    let ret = run('/home/mark/tcmalloc/WorkLoad/a.out', '-h', 'localhost:' + port, '-p', threads, '-d', duration);

    ret = run('sh', '-c', 'python /home/mark/tcmalloc/WorkLoad/genstats.py localhost:' + port + ' > ' + file_name_stats);

    MongoRunner.stopMongod(conn);

    ret = run('sh', '-c', 'cp -r ' + diag_data_dir + ' ' + name_data_dir);
    
}

(function () {
    'use strict';

    // markThreadIdleMode
    // 0 = none
    // 1 - mark thread idle
    // 2 - mark thread temp idle

    const threads = 1024;
    const duration = 20 * 60;
    const idle_seconds = 30;

    const markThreadChoices = { 'none' : 0, 'markThreadIdle' : 1, 'markThreadTemporaryIdle' : 2};
    const cacheChoices = {'32mb': 32 * 1024 * 1024, '1gb' : 1024 * 1024 * 1024};

    const prefix = '347';
    for(let thread in markThreadChoices) {
        for(let cache in cacheChoices) {
            //let name = `${prefix}_${markThreadChoices[thread]}_${cacheChoices[cache]}`;
            let name = `${prefix}_${thread}_${cache}`;
            print(name);
            workloadBenchmark(name, markThreadChoices[thread], cacheChoices[cache], threads, duration);

            // Sleep for a little while between runs
            sleep(30 * 1000);
        }
    }
    
//    workloadBenchmark("test1", true, 400000);
})();

