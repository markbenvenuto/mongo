// https://docs.mongodb.com/manual/reference/command/replSetReconfig/

typedef string ObjectId;

struct Member {
    1: i32 _id,
    2: string host,
    3: bool arbiterOnly,
    4: bool buildIndexes = true,
    5: bool hidden,
    6: double  prioroity,
    7: map<string, string> tags,
    8: i32  slaveDelay,
    9: i32 votes = 1,
}

struct WriteConcern {
    // ISSUE: This is really a variant type or just plain BSONObj
    1: string w,
    2: bool j,
    3: i32 wtimeout,
}

struct LastErrorModesMappings {
    1: i32 foo,
    // ISSUE: This untyped nested map<string, map<string, int>
    // See https://docs.mongodb.com/manual/tutorial/configure-replica-set-tag-sets/
    2: map<string, map<string, i32> mappings,
}

struct Settings {
    1: bool chainingAllowed = true,

    // ISSUE: These numbers must be > 0
    2: i32  heartbeatIntervalMillis,
    3: i32 heartbeatTimeoutSecs = 10,
    4: i32 electionTimeoutMillis = 11000,
    5: i32 catchUpTimeoutMillis = 2000,
    6: LastErrorModesMappings getLastErrorModes,
    7: WriteConcern getLastErrorDefaults,
    8: ObjectId replicaSetId,
}

struct ReplSetconfig {
    1: string _id,
    2: i32 version,
    3: i64 protocolVersion = 1,

    // Flags default changes based on protocolVersion 
    4: bool writeConcernMajorityJournalDefault,

    5: bool configsvr,

    6: list<Member> members,

    7: Settings settings,
}

