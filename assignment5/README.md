# CSC 3320 Assignment 5 — Concurrent File Management and Deadlocks

This is a terminal program for Linux, written in C with POSIX pthreads.
No website, network server, IP address, or second computer is needed.

## Run on Ubuntu

1. Copy this ZIP into Ubuntu and extract it using the Files app.
2. Open the extracted `assignment5` folder. Right-click an empty space and
   select **Open in Terminal**. You should see `file_manager.c` in this folder.
3. If GCC and make are not installed, run:

```bash
sudo apt update
sudo apt install build-essential python3
```

4. Compile:

```bash
make
```

Or compile without make:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -pthread file_manager.c -o file_manager
```

5. Run each demonstration:

```bash
./file_manager deadlock --reset
./file_manager prevention --reset
./file_manager recovery --reset
```

Run these separately so you can explain each result. `--reset` resets the
sample student, grade, audit, and backup data for that mode. It does not touch
other modes or the samples folder. Each execution replaces that mode's log.
Without `--reset`, existing records are retained and an existing grade is not
added twice. For a predictable class demonstration, always use `--reset`.
Do not run two copies of the program in the same project folder at once.

## What to expect

- **deadlock**: T1 holds A and requests B; T2 holds B and requests A.
  Main prints `DEADLOCK_DETECTED` and `T1 -> T2 -> T1`. Both workers safely
  abort so the program finishes. Student and grade files stay at initial values.
- **prevention**: T1 and T2 both request A before B. Both complete; no cycle.
- **recovery**: Main detects the cycle and requests T2 to abort. T2 releases its
  own lock and retries with A then B. Look for `RETRY_SUCCESS`.

All modes create six worker threads plus main. T3–T6 wait for both writers to
finish, then perform counting, auditing, statistics, and backup tasks. Their
work can overlap when their required locks do not conflict. This deliberate
ordering makes final statistics reproducible.

## Output files

The program creates these mode folders inside the project:

```text
data/deadlock/
data/prevention/
data/recovery/
```

Each contains students.txt, grades.txt, audit.txt, backup.txt, and execution.log.
The initial samples are also provided in `samples/`; initialization uses the
same contents embedded in the C source, so no copying is required.
The terminal displays final contents and lists the four TXT files with size
and last-modified time (Unix timestamp).

After prevention/recovery, Ben becomes Benjamin, his grade becomes 94, and
S101's Assignment5 grade of 95 is added once. Grade statistics are count 3,
average 92.33, minimum 88, maximum 95.

To open the results in a terminal:

```bash
cat data/recovery/students.txt
cat data/recovery/grades.txt
cat data/recovery/execution.log
```

The audit file contains completed business operations; execution.log contains
thread, lock, error, and retry events. They have different purposes.

## Test

```bash
make test
```

Tests run in a temporary directory and do not change your demo records.
An expected missing-file error can also be shown after running prevention:

```bash
./file_manager read missing.txt
```

This returns an error; your student and grade records stay unchanged.
The read command reads from `data/prevention/`. Names containing slashes or
`..` are rejected. Try `./file_manager invalid-mode` for an invalid operation.

## Files included

- file_manager.c: complete program with short explanatory comments.
- Makefile: compile and test commands.
- test_project.py: integration and repeat-run checks.
- samples/: three initial TXT files.
- evidence/: captured output, tests, and logs from actual Linux runs.
- REPORT_DRAFT.md: technical report draft; fill in real names/contributions.
- DEMO_GUIDE.md: a simple presentation sequence and explanation.

## Design limits

Locks A–D are application locks in an ownership table, guarded by a pthread
mutex and condition variable. They coordinate this program's threads only;
other applications do not honor them. A second mutex protects whole log lines.
Cooperative cancellation happens before file writes. T2 stages both files after
acquiring both locks and restores the first file if its second rename fails.
This is not a power-loss-safe transaction system; do not edit the private data
folder externally while running. Input files are limited to 65,535 bytes.

Before submitting, all team members should understand their assigned code,
add truthful contributions to the report, and capture any additional evidence
required by the professor. Recompile on your own Ubuntu VM before class.
