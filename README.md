# MLFQ Scheduler Simulator

A multi-threaded Multi-Level Feedback Queue (MLFQ) scheduler implementation in C that simulates CPU task scheduling with configurable parameters.

## Overview

This project implements a sophisticated MLFQ scheduler that manages task execution across multiple CPU threads.  The scheduler dynamically adjusts task priorities based on their behavior and execution patterns, providing an educational demonstration of operating system scheduling concepts.

### Key Features

- **Multi-level Priority Queues**: Four priority levels for dynamic task management
- **Multi-CPU Support**: Configurable number of CPU threads for parallel task execution
- **Priority Boost Mechanism**: Periodic promotion of all tasks to prevent starvation
- **I/O Simulation**: Tasks can simulate I/O operations with configurable probabilities
- **Performance Metrics**: Tracks turnaround time and response time per task type

## How It Works

The scheduler implements several key components:

- **Reader Thread**: Parses task files and introduces tasks into the system
- **Dispatcher Thread**: Signals CPU threads when tasks are available
- **Scheduler Thread**: Manages priority queues and implements the priority boost mechanism
- **CPU Threads**: Execute tasks according to MLFQ rules

### Scheduling Rules

1. Tasks start at the highest priority queue (Queue 1)
2. Each task runs for a quantum of 50 microseconds
3. After running for 200 microseconds total, tasks move to a lower priority queue
4. Every S microseconds, all tasks are boosted back to Queue 1
5. Tasks with I/O operations may yield their quantum early

## Building and Running

### Compilation

```bash
make
```

### Usage

```bash
./mlfq <num_CPUs> <S_time_microseconds> <task_file>
```

**Parameters:**
- `num_CPUs`: Number of CPU threads to simulate (1-8)
- `S_time_microseconds`: Time interval for priority boost (in microseconds)
- `task_file`: Path to the task specification file

**Example:**
```bash
./mlfq 1 200 tasks.txt
```

### Task File Format

The task file should contain one entry per line: 

```
<task_name> <type> <length_usec> <io_probability>
DELAY <milliseconds>
```

**Task Parameters:**
- `task_name`: Unique identifier for the task
- `type`: Task classification (0-3)
- `length_usec`: Total execution time in microseconds
- `io_probability`: Percentage chance (0-100) of performing I/O

**Example Task File:**
```
task1 0 1000 10
task2 1 2000 25
DELAY 100
task3 2 5000 50
```

## Performance Metrics

The simulator outputs two key metrics:

### Turnaround Time
Total time from task arrival to completion (microseconds)

### Response Time
Time from task arrival to first CPU scheduling (microseconds)

Both metrics are calculated and averaged per task type.

## Benchmark Results

### Turnaround Time

| Task Type | 1 CPU, 200μs | 1 CPU, 800μs | 2 CPUs, 200μs | 8 CPUs, 200μs |
|-----------|--------------|--------------|---------------|---------------|
| Type 0    | 64,763. 20    | 65,131.85    | 32,073.40     | 10,309.85     |
| Type 1    | 195,136.59   | 196,104.47   | 102,519.53    | 34,889.23     |
| Type 2    | 804,302.75   | 807,486.89   | 401,037.29    | 159,513.18    |
| Type 3    | 775,002.76   | 772,682.14   | 380,387.38    | 155,341.86    |

### Response Time

| Task Type | 1 CPU, 200μs | 1 CPU, 800μs | 2 CPUs, 200μs | 8 CPUs, 200μs |
|-----------|--------------|--------------|---------------|---------------|
| Type 0    | 1,381.95     | 1,383.45     | 645.70        | 230.00        |
| Type 1    | 1,290.00     | 1,299.12     | 624.35        | 240.41        |
| Type 2    | 1,742.96     | 1,780.07     | 912.64        | 299.18        |
| Type 3    | 1,510.86     | 1,576.38     | 746.45        | 275.45        |

## Implementation Details

- **Language**: C (98.3%)
- **Threading**:  POSIX threads (pthread)
- **Queue Implementation**: TAILQ from `sys/queue.h`
- **Synchronization**: Mutexes and condition variables
- **Time Management**: `clock_gettime()` for microsecond precision

## Technical Specifications

- **Quantum Length**: 50 microseconds
- **Time Allotment**: 200 microseconds per priority level
- **Priority Levels**: 4 (1 = highest, 4 = lowest)
- **Max Task Name Size**: 100 characters
