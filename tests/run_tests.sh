#!/bin/sh
# Automatic checks for CampusCompute.  Run with:  make test
# Each check runs the real program and looks for expected text / exit codes.
# The expected scheduling averages were worked out by hand and with a
# separate reference implementation.

BIN=./campuscompute
PASS=0
FAIL=0

# check "description" "expected text" command...
check() {
    desc="$1"; expect="$2"; shift 2
    out=$("$@" 2>&1)
    if echo "$out" | grep -qF -- "$expect"; then
        echo "  PASS  $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL  $desc   (expected: $expect)"
        FAIL=$((FAIL + 1))
    fi
}

# check_rc "description" expected_exit_code command...
check_rc() {
    desc="$1"; want="$2"; shift 2
    "$@" >/dev/null 2>&1
    got=$?
    if [ "$got" = "$want" ]; then
        echo "  PASS  $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL  $desc   (exit code $got, expected $want)"
        FAIL=$((FAIL + 1))
    fi
}

if [ ! -x "$BIN" ]; then
    echo "Build first: make"
    exit 1
fi

echo "FR2 scheduling"
check "FCFS average waiting time"        "Average waiting time    : 9.50"  $BIN sched fcfs
check "Round Robin (q=3) average waiting" "Average waiting time    : 12.17" $BIN sched rr -q 3
check "Priority average waiting time"    "Average waiting time    : 7.17"  $BIN sched priority
check "Priority tie-break (J2 before J5)" "Execution order: J1 J2 J5 J4 J3" $BIN sched priority
check "Idle time is shown"               "[IDLE 28-40]"                    $BIN sched fcfs
check "Comparison table"                 "Lowest average waiting time   : Priority" $BIN sched compare
check "Jobs from a file"                 "Average waiting time    : 9.50"  $BIN sched fcfs -f data/jobs.txt

echo "FR3 logger / FR4 IPC"
check "Logger: produced == consumed"     "every event produced was consumed exactly once - OK" $BIN logger -o /tmp/cc_test.log
check "Logger: log in order"             "sequence numbers in order: yes"  $BIN logger -p 2 -c 1 -n 5 -s 1 -o /tmp/cc_test.log
rm -f /tmp/cc_test.log
check "IPC data exchange"                "data exchanged correctly through shared memory: OK" $BIN ipc
check "IPC cleanup"                      "shared memory exists? no, semaphores exist? no"     $BIN ipc

echo "FR5 Banker's"
check "Safe state sequence"              "Safe sequence: <P1, P3, P0, P2, P4>" $BIN banker safe
check "Unsafe state detected"            "UNSAFE STATE"                        $BIN banker unsafe
check "Safe file input"                  "SAFE STATE"                          $BIN banker -f data/banker_safe.txt
check "Unsafe file input"                "UNSAFE STATE"                        $BIN banker -f data/banker_unsafe.txt

echo "FR6 workers / FR7 limits and cleanup"
check "Normal worker"                    "exited normally, code 0 -> SUCCESS"  $BIN worker normal
check "Simulated failure detected"       "NON-ZERO code 3 -> FAILURE DETECTED" $BIN worker fail
check "Simulated crash detected"         "killed by signal 9"                  $BIN worker crash
check "Open-file limit"                  "Too many open files"                 $BIN rlimit nofile
check "Memory limit"                     "returned NULL"                       $BIN rlimit mem
check "CPU limit"                        "killed by signal 24"                 $BIN rlimit cpu
check "Cleanup verification"             "CLEAN - nothing left behind"         $BIN verify

echo "FR8 error handling"
check_rc "no command            -> exit 2" 2 $BIN
check_rc "unknown command       -> exit 2" 2 $BIN nonsense
check_rc "unknown policy        -> exit 2" 2 $BIN sched sjf
check_rc "bad quantum           -> exit 2" 2 $BIN sched rr -q abc
check_rc "missing job file      -> exit 1" 1 $BIN sched fcfs -f no_such_file.txt
check_rc "bad logger option     -> exit 2" 2 $BIN logger -p 99
check_rc "bad worker mode       -> exit 2" 2 $BIN worker explode
check "usage message is shown"           "usage: campuscompute sched"          $BIN sched

# job files with bad numbers: a huge number must be rejected, not wrapped around
printf '4294967297 0 5 1\n' > /tmp/cc_bad_big.txt
printf '1 0 abc 2\n'        > /tmp/cc_bad_text.txt
printf '1 0 5 2\r\n2 1 3 1\r\n' > /tmp/cc_crlf.txt
check_rc "huge number in job file -> exit 1" 1 $BIN sched fcfs -f /tmp/cc_bad_big.txt
check_rc "non-number in job file  -> exit 1" 1 $BIN sched fcfs -f /tmp/cc_bad_text.txt
check "Windows (CRLF) job file is accepted" "Average waiting time    : 2.00"  $BIN sched fcfs -f /tmp/cc_crlf.txt
rm -f /tmp/cc_bad_big.txt /tmp/cc_bad_text.txt /tmp/cc_crlf.txt

echo
echo "Result: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
