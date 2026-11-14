# CSC 3320 Assignment 5 Concurrent File Management and Deadlocks

For this program everything is run using one machine and ubuntu. The program is written in C and this is made for linx with POSIX pthread

## how to run it on ubuntu

Step one: Copy the zip file into ubuntu after succefully doing that extract the whole folder with the files application.

Step two: Opem the new extracted folder then right click and chose open in terminal then you should be able to see file_manager.c

Step three: If gcc ane make aren6 installed yet you must tun this:

```bash
sudo apt update
sudo apt install build-essential python3
```

step 4: compile

```bash
make
```

if you want to skip this, make this does the same thing

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -pthread file_manager.c -o file_manager
```

step five: run the demos, note this must be done one at a time

```bash
./file_manager deadlock --reset
./file_manager prevention --reset
./file_manager recovery --reset
```

you should run them separtely this way it is easier to see the detail and expalin each result before continuing.
--reset will put a sample student graee audit and then backup the data for that mode, This will not tocuch other modes or folders, each run replaces that modes log.

note if you exit --reset off the already made records stay and grades that are already there wont duplicate for class please use --reset each time to ensure output is the same.

please ensure no to run multiple copies of the program in the same project folder at the same time.

## what you should see

-deadlock thread one will hold A and ask for B while thread two holds B and asks for A. main will print "DEADLOCK_DETECTED" and T1 -> T2
- Thread one and both will back out, this way the program finishes and the student and grade file stay exactly how they started
- prevention thread one and thread two ask dor A before B and both finish
- prevention thread one and thread two each thread asks for A before B and both finish
- recovery main spots the cycle. then get thread two to abandon and thread two lets go of its lock and tries again with A then B. in out put find RETRY_SUCCESS

each mode starts six worker threads with the addtion of main
thread 3 to thread 6 will await until both writers are done after they do the counting auditing statistics and backup
they can overlap with each other when their locks dont clash

## output files

```text
data/deadlock/
data/prevention/
data/recovery/
```

each run has students.txt grades.txt audit.txt backup.txt execution.log

when finised terminal will display: final contents, txt files with their size and last modified time 

after prevention or recovery Ben switched to Benjamin, his grade is 94 and S101s Assignment5 grade of 95 has been added once
the grade stats are count 3 and average 92.33 and minimum 88 and maximum 95

to see for your self run:
```bash
cat data/recovery/students.txt
cat data/recovery/grades.txt
cat data/recovery/execution.log
```

audit.txt holds the operations that finished
execution.log holds the thread, lock, error and retry events

## tests

```bash
make test
```
test are run in a temp folder so dont worry about demo records

you can also show an expected error after running prevention

```bash
./file_manager read missing.txt
```

this will report an error and leaves the student and grade records alone

the read command only reads from data/prevention
names with a slash or .. in them get rejected
to see an invalid operation try ./file_manager invalid-mode

## whats in the folder

- file_manager.c the whole program with short comments
- Makefile the compile and test commands
- test_project.py integration tests and repeat run checks
- samples the three starting txt files
- evidence output and test results and logs from real linux runs
- REPORT_DRAFT.md a draft of the technical report so fill in real names and contributions
- DEMO_GUIDE.md a simple order to present things in plus what to say



