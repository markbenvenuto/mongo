/*
 * MongoDB DTrace probes
 *
 * Inspired by:
 * ./maria/trunk/include/probes_mysql.d.base
 * ./src/backend/utils/probes.d
 *
 * SQL Server Trace - http://technet.microsoft.com/en-us/library/ms175481.aspx
 * SQL Server Extended Events
 *
 * TODO Events
 * - Add more events and more parameters
 * - Add auditing stuff ?
 * - Add explain events
 * - Structs vs simple types - Solaris io provider uses structs for instance
 * - How to handle bson? Convert to string is the best option
 */
provider mongodb {
    /* Network Events */
    probe connection_start();
    probe connection_done();

    /* Commands */
    probe command_start(string collection, string command, string document);
    probe command_end(int32_t code);

    /* These model our network layer
     * Need to think how not to confuse users
     * Add Legacy Flag */
    probe insert_start(string collection, int32_t flags, string documents, int8_t legacy);
    probe insert_end();

    probe update_start(string collection, int32_t flags, string select, string update, int8_t legacy);
    probe update_done();

    probe delete_start(string collection, int32_t flags, string selector, int8_t legacy);
    probe delete_done();

    probe query_start(string collection, int32_t skip, int32_t limit, string query);
    probe query_done(int64_t cursor_id, int32_t flags, int32_t start, int32_t count);

    probe get_more_start(string collection, int32_t limit, int64_t cursor_id);
    probe get_more_done();

    /* Write Commands */
    probe write_commands_start(string collection, int32_t count);
    probe write_commands_done();

    /* Cache */
    probe cache_hit();
    probe cache_miss();
    probe cache_insert();
    probe cache_remove();
    probe cache_flush();

    /* Cursor */
    probe cursor_delete(int64_t cursor_id);

    /* Locking */
    probe qlock_acquire(int8_t type);
    probe qlock_release(int8_t type);

    /* Exceptions & Asserts ?? */
    probe uassert();
    probe fassert();

    /* File stuff */
    probe journal_write_start();
    probe journal_write_done();

    probe data_file_fsync_start();
    probe data_file_fsync_done();
    /* Records when we allocate a new database file */
    probe file_allocate(string name, int64_t size);

    probe sort_spill();

    /*  OP_REPLY, OP_GETMORE, etc */
    probe network_op_start(int op);
};

#pragma D attributes Evolving/Evolving/Common provider mongodb provider
#pragma D attributes Evolving/Evolving/Common provider mongodb module
#pragma D attributes Evolving/Evolving/Common provider mongodb function
#pragma D attributes Evolving/Evolving/Common provider mongodb name
#pragma D attributes Evolving/Evolving/Common provider mongodb args


