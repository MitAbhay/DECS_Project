# Real-Time Leaderboard System (DECS Project)

This project implements a **real-time leaderboard service** that demonstrates how two different
execution paths affect system performance — one that is **CPU-bound (in-memory cache)** and
another that is **I/O-bound (PostgreSQL database)**. A **hybrid mode** is also included to show
how real systems balance speed and data persistence.

---

## 🚀 Features

| Feature                       | Description                                               |
| ----------------------------- | --------------------------------------------------------- |
| Update player score           | `/update_score?player_id=<id>&score=<value>` (POST)       |
| Fetch top players leaderboard | `/leaderboard?top=<N>` (GET)                              |
| Three Execution Modes         | Demonstrates Cache-only, DB-only, and Hybrid processing   |
| Request Debug Logging         | Shows latency and whether cache or DB was used            |
| Realistic Design              | Similar to leaderboard logic in games / live ranking apps |

---

## 🧠 Execution Modes

| Mode | Description | Path Type     | Behavior Characteristics                              |
| ---- | ----------- | ------------- | ----------------------------------------------------- |
| `0`  | DB-Only     | **I/O-bound** | Every request hits PostgreSQL → Slower, Persistent    |
| `1`  | Cache-Only  | **CPU-bound** | Entire leaderboard in RAM → Very Fast, Not Persistent |
| `2`  | Hybrid      | **Balanced**  | Writes go to DB + Cache; Reads served from Cache      |
