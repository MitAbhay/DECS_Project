#ifndef CONFIG_H
#define CONFIG_H

// PostgreSQL connection
#define PG_CONNINFO "host=127.0.0.1 port=5432 dbname=leaderboard_db user=leaderboard_user password=leaderboard_pw"

// server config
#define DEFAULT_PORT 8080
#define THREAD_POOL_SIZE 8
#define DEFAULT_TOP_N 10

// cache limits
#define SKIPLIST_MAX_LEVEL 16

#endif // CONFIG_H
