'use strict';

function update(conn, range, count) {
    //var fragDB = conn.getDB('frag');
    var fragDB = db.getSiblingDB('frag');
    while (true) {
        var l = Math.random() * 200;
        var x = '';

        while (x.length < l) {
            x += 'xxxxxxxxxxxx';
        }

        var id = Math.floor(Math.random() * range);
        fragDB.c1.update({ _id: id }, { $push: { x: x } }, { upsert: true });
    }
}

// TODO: Does not generate enough fragementation
function parallel_update(port, dir_prefix) {
    var shells = [];
    for (var i = 0; i < 10; i++) {
        shells.push(startParallelShell(
            function () {
                const update_range = 100000;
                const update_count = 100000;
                var fragDB = db.getSiblingDB('frag');
                for (var i = 0; i < update_count; i++) {
                    var l = Math.random() * 200;
                    var x = '';

                    while (x.length < l) {
                        x += 'xxxxxxxxxxxx';
                    }

                    var id = Math.floor(Math.random() * update_range);
                    fragDB.c1.update({ _id: id }, { $push: { x: x } }, { upsert: true });
                }
            }
            , port));
    }

    shells.forEach(function (join) {
        print(join());
    });
}

function workload(port, dir_prefix, threads, duration) {
    const file_name_stats = dir_prefix + "workload.stats";

    //var ret = run('/home/mark/tcmalloc/WorkLoad/a.out', '-h', 'localhost:17017', '-p', '1024', '-d', '1200');
    const root = '/home/mark/tcmalloc/WorkLoad'
    //const root = '/root'
    var ret = run(root + '/a.out', '-h', 'localhost:' + port, '-p', threads, '-d', duration);

    ret = run('sh', '-c', 'python ' + root + '/genstats.py localhost:' + port + ' > ' + file_name_stats);
}


function workloadYCSB(port, dir_prefix, threads) {
    // load takes ~90 seconds
    // run - 95/5 - 4.5 minutes
    // run - 50/50 - ?

    var ycsb_path = '/home/mark/repo/YCSB/ycsb-mongodb';

    var ret = run('sh', '-c', 'cd ' + ycsb_path + '; ./bin/ycsb load  mongodb -s -P dsi/load.workload -p mongodb.url=mongodb://127.0.0.1:' + port + '/ycsb -threads ' + threads);

    ret = run('sh', '-c', 'cd ' + ycsb_path + '; ./bin/ycsb run mongodb -s -P dsi/95read_5update.workload -p mongodb.url=mongodb://127.0.0.1:' + port + '/ycsb -threads ' + threads + " -p measurementtype=timeseries -p timeseries.granularity=5000 > timeseries_95read_5update.dat");

    var cp_ret = run('sh', '-c', 'cp -r ' + ycsb_path + '/timeseries_95read_5update.dat ' + dir_prefix + "/timeseries_95read_5update.dat");

    ret = run('sh', '-c', 'cd ' + ycsb_path + '; ./bin/ycsb run mongodb -s -P dsi/50read_50update.workload -p mongodb.url=mongodb://127.0.0.1:' + port + '/ycsb -threads ' + threads+ " -p measurementtype=timeseries -p timeseries.granularity=5000 > timeseries_50read_50update.dat");

    cp_ret = run('sh', '-c', 'cp -r ' + ycsb_path + '/timeseries_50read_50update.dat ' + dir_prefix + "/timeseries_50read_50update.dat");

}

function workloadBenchmark(name, markThreadIdleMode, cache_size, fun) {
    'use strict';

    var options = {
        //setParameter: "markThreadIdleMode=" + markThreadIdleMode,
        // setParameter: "tcmallocMaxTotalThreadCacheBytes=" + cache_size,
        slowms: 50000,
        //storageEngine: 'inMemory',
        // dbpath: '/data/db',
        // dbpath: '/home/mark/mongo/foo',
        dbpath: '/tmp/foo',
        noCleanData: true,
    };

    resetDbpath('/tmp/foo/');
    //resetDbPath('/data/db/');
    //resetDbpath('/mnt/a/db/');
    //resetDbpath('/mnt/b/journal/');
    //const lns = run('ln', '-s', '/mnt/b/journal', '/data/db/journal');
    //assert.eq(lns, 0);

    const conn = MongoRunner.runMongod(options);

    const port = conn.port;

    const dir_prefix = 'runs/' + name + '/';
    const name_data_dir = dir_prefix + "diagnostic_data";
    const diag_data_dir = conn.dbpath + "/diagnostic.data";
    mkdir(dir_prefix);

    // Set this parameter at one time since the options dictionary can only contain one setParameter
    var adminDB = conn.getDB('admin');
    //var res = adminDB.runCommand({ "setParameter": 1, "tcmallocMaxTotalThreadCacheBytes": cache_size });
    //assert.commandWorked(res, 0);

    //print(tojson(adminDB.serverStatus()));

    fun(port, dir_prefix);

    MongoRunner.stopMongod(conn);

    // Save Diagnostic data
    var cp_ret = run('sh', '-c', 'cp -r ' + diag_data_dir + ' ' + name_data_dir);

}

function runAllWorkloadTests_TC() {
    'use strict';

    // markThreadIdleMode
    // 0 = none
    // 1 - mark thread idle
    // 2 - mark thread temp idle

    //const threads = 1024;
    //const duration = 40 * 60;
    const duration = 20 * 60;
    const idle_seconds = 30;

    //const markThreadChoices = { 'markThreadTemporaryIdleFixed': 2 };
    // const markThreadChoices = { 'none': 0, 'markThreadIdle': 1, 'markThreadTemporaryIdleFixed': 2 };
    const markThreadChoices = { 'none': 0, 'markThreadIdle': 1 };
    //const markThreadChoices = { 'none': 0, 'markThreadIdle': 1 };
    const cacheChoices = { '32mb': 32 * 1024 * 1024, '1gb': 1024 * 1024 * 1024 };
    //const threadChoices = { '32' : 32, '64' : 64, '128' : 128, '1024' : 1024 };
    // const threadChoices = { '64' : 64, '1024' : 1024 };
    //const markThreadChoices = { 'none': 0 };
    //const cacheChoices = { '1gb': 1024 * 1024 * 1024 };
    const threadChoices = { '1024' : 1024 };

    // const markThreadChoices = { 'none': 0};
    // const cacheChoices = { '32mb': 32 * 1024 * 1024};

    const prefix = '361_je_workloada';
    for (var thread in markThreadChoices) {
        for (var cache in cacheChoices) {
            for (var threads in threadChoices) {
                //var name = `${prefix}_${markThreadChoices[thread]}_${cacheChoices[cache]}`;
                var name = `${prefix}__${thread}_${cache}`;
                print(name);
                workloadBenchmark(name, markThreadChoices[thread], cacheChoices[cache], function (port, dir_prefix) { workload(port, dir_prefix, threads, duration); });

                // Sleep for a little while between runs
                sleep(30 * 1000);
            }
        }

    }

    //    workloadBenchmark("test1", true, 400000);
}


function workloadBenchmark(name, markThreadIdleMode, cache_size, fun) {
    'use strict';

    var options = {
        //setParameter: "markThreadIdleMode=" + markThreadIdleMode,
        // setParameter: "tcmallocMaxTotalThreadCacheBytes=" + cache_size,
        slowms: 50000,
        //storageEngine: 'inMemory',
        // dbpath: '/data/db',
        // dbpath: '/home/mark/mongo/foo',
        dbpath: '/tmp/foo',
        noCleanData: true,
    };

    resetDbpath('/tmp/foo/');
    //resetDbPath('/data/db/');
    //resetDbpath('/mnt/a/db/');
    //resetDbpath('/mnt/b/journal/');
    //const lns = run('ln', '-s', '/mnt/b/journal', '/data/db/journal');
    //assert.eq(lns, 0);

    const conn = MongoRunner.runMongod(options);

    const port = conn.port;

    const dir_prefix = 'runs/' + name + '/';
    const name_data_dir = dir_prefix + "diagnostic_data";
    const diag_data_dir = conn.dbpath + "/diagnostic.data";
    mkdir(dir_prefix);

    // Set this parameter at one time since the options dictionary can only contain one setParameter
    var adminDB = conn.getDB('admin');
    //var res = adminDB.runCommand({ "setParameter": 1, "tcmallocMaxTotalThreadCacheBytes": cache_size });
    //assert.commandWorked(res, 0);

    //print(tojson(adminDB.serverStatus()));

    fun(port, dir_prefix);

    MongoRunner.stopMongod(conn);

    // Save Diagnostic data
    var cp_ret = run('sh', '-c', 'cp -r ' + diag_data_dir + ' ' + name_data_dir);

}


function runAllWorkloadTests() {
    'use strict';

    // markThreadIdleMode
    // 0 = none
    // 1 - mark thread idle
    // 2 - mark thread temp idle

    //const threads = 1024;
    //const duration = 40 * 60;
    const duration = 20 * 60;
    const idle_seconds = 30;

    //const markThreadChoices = { 'markThreadTemporaryIdleFixed': 2 };
    // const markThreadChoices = { 'none': 0, 'markThreadIdle': 1, 'markThreadTemporaryIdleFixed': 2 };
    const markThreadChoices = { 'none': 0, 'markThreadIdle': 1 };
    //const markThreadChoices = { 'none': 0, 'markThreadIdle': 1 };
    //const threadChoices = { '32' : 32, '64' : 64, '128' : 128, '1024' : 1024 };
    // const threadChoices = { '64' : 64, '1024' : 1024 };
    const threadChoices = { '64' : 64, '128' : 128, '256' : '256', '512' : 512, '1024' : 1024 };

    // const markThreadChoices = { 'none': 0};
    // const cacheChoices = { '32mb': 32 * 1024 * 1024};

    const prefix = '361a_je_workloada';
    for (var thread in markThreadChoices) {
        for (var threads in threadChoices) {
            var name = `${prefix}__${thread}`;
            print(name);
            workloadBenchmarkJE(name, markThreadChoices[thread], function (port, dir_prefix) { workload(port, dir_prefix, threads, duration); });

            // Sleep for a little while between runs
            sleep(30 * 1000);
        }

    }

    //    workloadBenchmark("test1", true, 400000);
}
function runAllYCSBTests() {
    'use strict';

    // markThreadIdleMode
    // 0 = none
    // 1 - mark thread idle
    // 2 - mark thread temp idle

    const idle_seconds = 30;

    //    const markThreadChoices = { 'none': 0, 'markThreadIdle': 1, 'markThreadTemporaryIdle': 2 };
    //    const cacheChoices = { '32mb': 32 * 1024 * 1024, '1gb': 1024 * 1024 * 1024 };

    // const threadChoices = { '8': 8, '16': 16, '24': 24, '32': 32, '48': 48, '64': 64 };
    // //const threadChoices = { '8': 8};
    // const prefix = '347a_ycsb_';
    // for (var threads in threadChoices) {
    //     var name = `${prefix}_${threads}`;
    //     print(name);
    //     workloadBenchmark(name, 1, 1024 * 1024 * 1024, function (port, dir_prefix) { workloadYCSB(port, dir_prefix, threads); });

    //     // Sleep for a little while between runs
    //     sleep(30 * 1000);
    //}

    //const markThreadChoices = { 'none': 0  };
    //const cacheChoices = { '1gb': 1024 * 1024 * 1024 };
    //const markThreadChoices = { 'none': 0, 'markThreadIdle': 1, 'markThreadTemporaryIdleFixed': 2 };
    const markThreadChoices = { 'none': 0, 'markThreadIdle': 1 };
    //const cacheChoices = { '32mb': 32 * 1024 * 1024 };
    const cacheChoices = { '32mb': 32 * 1024 * 1024, '1gb': 1024 * 1024 * 1024 };

    //const threadChoices = { '48': 48, '96' : 96, '256' : 256};
    //const threadChoices = { '48': 48, '256' : 256, '512' : 512};
    const threadChoices = { '256' : 256, '512' : 512};
    const prefix = '347j_ycsb_';
    for (var threads in threadChoices) {
        for (var mark in markThreadChoices) {
            for (var cache in cacheChoices) {

                var name = `${prefix}_${threads}_${cache}_${mark}`;
                print(name);
                workloadBenchmark(name, markThreadChoices[mark], cacheChoices[cache], function (port, dir_prefix) { workloadYCSB(port, dir_prefix, threads); });

                // Sleep for a little while between runs
                sleep(30 * 1000);
            }
        }
    }

    //    workloadBenchmark("test1", true, 400000);
}


(function () {

    //runAllYCSBTests();
    runAllWorkloadTests();
    //workloadBenchmark("347_frag_", 1, 1024 * 1024 * 1024, parallel_update);
})();



// var db = conn.getDB('fake');
// var t = new ParallelTester();

// for(var i = 0; i < 2; i++) {
//     t.add(update, [100000, 2000]);
// }

// t.run("Parallel tests failed", true);
