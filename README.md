# Leaderboard Server - Performance Testing Project

A high-performance leaderboard server implementation with PostgreSQL backend and LRU caching, designed for load testing and bottleneck analysis.

## 📋 Project Overview

This project implements a concurrent HTTP server that handles player score updates and leaderboard queries. It includes:

- **Multi-threaded HTTP server** with connection pooling
- **PostgreSQL database** backend with connection pooling
- **LRU cache** for in-memory score storage
- **Load generator** for stress testing
- **Multiple operating modes** (DB-only, Cache-only, Hybrid)

## 🎯 Features

- **Three Operating Modes:**

  - Mode 0: Database-only (persistent storage)
  - Mode 1: Cache-only (in-memory, fast)
  - Mode 2: Hybrid (cache + DB for consistency)

- **REST API Endpoints:**

  - `POST /update_score?player_id=X&score=Y` - Update player score
  - `GET /leaderboard?top=N` - Fetch top N players
  - `GET /get_score?player_id=X` - Get individual player score

- **Performance Features:**
  - Connection pooling (configurable size)
  - Thread pool for HTTP handling
  - LRU cache with automatic eviction
  - Microsecond-level latency logging

## 🛠️ Prerequisites

```bash
# Required libraries
sudo apt-get install libmicrohttpd-dev libpq-dev libcurl4-openssl-dev

# PostgreSQL
sudo apt-get install postgresql postgresql-contrib
```

## 📦 Installation

### 1. Database Setup

```bash
# Switch to postgres user
sudo -u postgres psql

# Create database and user
CREATE DATABASE leaderboard_db;
CREATE USER leaderboard_user WITH PASSWORD 'leaderboard_pw';
GRANT ALL PRIVILEGES ON DATABASE leaderboard_db TO leaderboard_user;

# Connect to database
\c leaderboard_db

# Create table
CREATE TABLE leaderboard (
    player_id INT PRIMARY KEY,
    score INT NOT NULL,
    last_updated TIMESTAMP DEFAULT NOW()
);

# Grant permissions
GRANT ALL ON TABLE leaderboard TO leaderboard_user;
```

### 2. Compile Server

```bash
gcc -O2 -Wall server.c -o server -lmicrohttpd -lpq -pthread
```

### 3. Compile Load Generator

```bash
gcc -O2 -Wall loadgen.c -o loadgen -lcurl -lpthread
```

## 🚀 Usage

### Start Server

```bash
# Basic usage
./server <port> <mode>

# Examples
./server 8080 0    # DB-only mode on port 8080
./server 8080 1    # Cache-only mode
./server 8080 2    # Hybrid mode (default)
```

### Run Load Tests

```bash
# Usage
./loadgen <server_url> <threads> <requests_per_thread> <mode>

# Mode options:
# 0 = Update only
# 1 = Leaderboard GET only
# 2 = Mixed (update + get)
# 3 = Get score only

# Examples
./loadgen http://127.0.0.1:8080 16 100 0    # 16 threads, 100 updates each
./loadgen http://127.0.0.1:8080 8 50 2      # Mixed workload
./loadgen http://127.0.0.1:8080 4 200 1     # Leaderboard queries only
```

## ⚙️ Configuration

### Server Configuration (server.c)

```c
#define MAX_CACHE_SIZE 1000    // LRU cache capacity
#define POOL_SIZE 64           // PostgreSQL connection pool size
#define DEFAULT_PORT 8080      // Default HTTP port
#define DEFAULT_TOP 10         // Default leaderboard size
```

### PostgreSQL Tuning (postgresql.conf)

```ini
max_connections = 200
shared_buffers = 256MB
effective_cache_size = 1GB
maintenance_work_mem = 128MB
```

## 🔧 CPU Pinning for Bottleneck Analysis

Pin processes to specific cores to isolate CPU and I/O bottlenecks:

```bash
# Pin server to core 0
taskset -c 0 ./server 8080 0

# Pin PostgreSQL to cores 1-2
sudo taskset -cp 1,2 $(pgrep -f postgres)

# Pin load generator to cores 3-5
taskset -c 3,4,5 ./loadgen http://127.0.0.1:8080 16 100 0
```

## 📊 Performance Testing

### Achieving I/O Bottleneck

To maximize I/O utilization:

1. **Increase connection pool size** (64-128 connections)
2. **Use DB-only mode** (mode 0)
3. **Run high-concurrency load test:**
   ```bash
   ./loadgen http://127.0.0.1:8080 16 1000 0
   ```

### Achieving CPU Bottleneck

To maximize CPU utilization:

1. **Use cache-only mode** (mode 1)
2. **Pin server to single core**
3. **Run mixed workload:**
   ```bash
   taskset -c 0 ./server 8080 1
   ./loadgen http://127.0.0.1:8080 32 1000 2
   ```

### Monitoring Performance

```bash
# Monitor I/O
iostat -x 1

# Monitor CPU
htop -p $(pgrep server)

# Monitor PostgreSQL
psql -U leaderboard_user -d leaderboard_db -c "SELECT * FROM pg_stat_activity;"

# Server logs (microsecond latency per request)
# Check terminal output for:
# [UPDATE] mode=0 wrote_db=1 latency=1234 us
# [LEADERBOARD] mode=0 cache_hit=0 latency=2345 us
```

## 📈 Expected Results

### DB-Only Mode (Mode 0)

- **Latency:** 1-3ms per update
- **Throughput:** ~5,000-15,000 req/sec (depends on I/O)
- **Bottleneck:** Disk I/O

### Cache-Only Mode (Mode 1)

- **Latency:** 10-50μs per update
- **Throughput:** ~50,000-200,000 req/sec
- **Bottleneck:** CPU

### Hybrid Mode (Mode 2)

- **Latency:** 1-3ms per update (DB-limited)
- **Throughput:** Similar to DB-only
- **Benefits:** Cache hits for reads

## 🐛 Troubleshooting

### Connection Errors

```bash
# Check PostgreSQL is running
sudo systemctl status postgresql

# Test connection manually
psql -h 127.0.0.1 -U leaderboard_user -d leaderboard_db
```

### Low I/O Utilization

- Increase `POOL_SIZE` in server.c (try 64 or 128)
- Increase load generator threads
- Check disk performance with `iostat`

### Server Crashes

- Increase PostgreSQL max_connections
- Check system ulimits: `ulimit -n`
- Monitor memory usage: `free -h`

## 📁 Project Structure

```
.
├── server.c          # Main server implementation
├── loadgen.c         # Load testing tool
├── uthash.h          # Hash table library (required)
└── README.md         # This file
```

## 📝 Dependencies

- **libmicrohttpd:** HTTP server framework
- **libpq:** PostgreSQL C library
- **libcurl:** HTTP client (load generator)
- **uthash:** Hash table implementation ([download](https://troydhanson.github.io/uthash/))

## 🤝 Contributing

This is an educational project for performance testing and bottleneck analysis. Feel free to experiment with different configurations!

## 📄 License

MIT License - Free to use for educational purposes.

## 🎓 Learning Objectives

- Understanding I/O vs CPU bottlenecks
- Connection pooling strategies
- Caching vs persistence trade-offs
- Load testing methodologies
- Concurrent programming patterns

---

**Note:** This project is designed for controlled testing environments. Do not expose to public networks without proper security measures.
