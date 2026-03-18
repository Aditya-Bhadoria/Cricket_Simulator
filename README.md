# 🏏 T20 Cricket Simulator (CSC-204 Operating Systems)

A highly concurrent, multi-threaded T20 Cricket Simulator built in C. This project maps real-world cricket mechanics to core OS concepts, including thread synchronization, CPU scheduling, and deadlock handling.

---

## 🧠 Key OS Concepts Implemented

### 🧵 Thread Management
- Batsmen (Consumers)  
- Bowlers (Producers)  
- Fielders  
- Third Umpire (Kernel/Daemon)  

All execute as concurrent POSIX threads.

---

### 🔒 Process Synchronization
- `pitch_mutex` → Protects the pitch (Critical Section)  
- `score_mutex` → Prevents dirty reads/writes to the scoreboard  
- `crease_sem` (Semaphore) → Limits the crease capacity to exactly two batsmen  
- `Global_Score` → Tracks total runs, strictly protected by `score_mutex` to prevent race conditions
- `pitch_status` → Indicates whether pitch currently contains an active delivery or not
- `ball_in_air` → Triggers Fielder threads to wake up and attempt a catch
- `match_over` → The global termination flag
- `wickets_fallen` → Tracks number of terminated Batsman processes

---

### ⚙️ CPU Scheduling
- **Shortest Job First (SJF):**  
  Batting order is sorted based on predicted burst times (estimated balls faced), prioritizing tail-enders 

- **Round Robin (RR):**  
  Bowler rotation is managed using a strict 6-ball time quantum  

---

### ⚠️ Deadlock Detection & Recovery
Simulates a **Circular Wait Deadlock** (run-out mix-up) using Wait-For graph matrices.

The Third Umpire thread:
- Detects the cycle  
- Executes **Process Termination** (run-out)  
- Recovers the system  

---

## 📦 Prerequisites
- GCC Compiler  
- POSIX Threads Library (`pthread`)  
- macOS / Linux environment (or WSL on Windows)  

---

## 🚀 Compilation and Execution

### 1️⃣ Navigate to source directory

```bash
cd src
```

---

### 2️⃣ Compile the simulator

```bash
gcc simulator.c -o simulator -lpthread
```

---

### 3️⃣ Run the executable

```bash
./simulator
```

---

## 📁 Project Structure

- `src/simulator.c` → Core C source (threads, scheduling, synchronization)  
- `logs/match_log.txt` → Ball-by-ball simulation output and analytics  

---

## 📊 Output Details

The program provides:

1. 🏏 Final Innings Score & Wickets  
2. 📈 Bowler Analytics  
   - Overs  
   - Runs Given  
   - Wickets Taken  
   - Extras  
3. ⏱️ Middle-Order Wait Time Analysis  
   - Highlights SJF scheduling metrics  

---

## ⭐ Highlights

- Real-world cricket mapped to OS concepts  
- Demonstrates synchronization, scheduling, and deadlock recovery  
- Clean modular simulation with logging support  