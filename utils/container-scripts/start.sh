#!/bin/bash

echo "Initializing EVEmu..."

# A stop that comes in while the database is installed or updated waits for that to finish (docker allows 2
#  minutes, see docker-compose.yml);  then the server isn't started.  stop_server (below) replaces this trap
stopping=0
trap 'stopping=1; echo "=== Stop requested:  finishing the database step first, then stopping ==="' TERM

#Initialize the database
if ! /src/utils/container-scripts/db_init.sh; then
    exit 1
fi
if [ "$stopping" = 1 ]; then
    echo "=== Stopped before the server started ==="
    exit 0
fi

#Initialize configuration files
if [ ! -f "/app/etc/eve-server.xml" ]; then
    echo "eve-server.xml not found, installing..."
    cp /src/utils/config/eve-server.xml /app/etc/
fi
if [ ! -f "/app/etc/log.ini" ]; then
    echo "log.ini not found, installing..."
    cp /src/utils/config/log.ini /app/etc/
fi
if [ ! -f "/app/etc/motd.txt" ]; then
    echo "motd.txt not found, installing..."
    cp /src/utils/config/motd.txt /app/etc/
fi

# The server writes a new log file into /app/logs at every start (logDir in eve-server.xml), inside the container.
#  Keep the newest 20
ls -1t /app/logs/*.log 2>/dev/null | tail -n +21 | xargs -r rm -f --

echo "Installing devtools and GM tools..."
cp /src/utils/config/devtools.raw /app/etc/
cp /src/utils/config/gmtools.py /app/etc/

#Start eve-server
echo "Starting eve-server..."
cd /app/bin/
if [ "$RUN_WITH_GDB" == "TRUE" ] && ! command -v gdb >/dev/null 2>&1; then
    echo "=== RUN_GDB is TRUE, but this image was built without gdb.  Rebuild it with RUN_GDB=TRUE set to add gdb ==="
    RUN_WITH_GDB=FALSE
fi
# docker stop sends SIGTERM to this script, not to eve-server.  Pass it on:  on SIGTERM the server logs everyone
#  out, saves and exits (ServerShutdown in eve-server.cpp).  Before, bash died at once and took the server down
#  unsaved.  The server runs in the background so this script can take the signal, and keeps the console's input
#  (<&0) for docker attach.  This script stays the server's parent, rather than exec'ing it, so under gdb it can
#  close gdb once the server has gone.
#  Ctrl+C in docker attach reaches eve-server itself (it shares the terminal), so for SIGINT this script just
#  keeps waiting.
child=""
gdb_pid=""
# the eve-server process:  the child, or under gdb the program gdb runs
server_pid() {
    if [ -z "$gdb_pid" ]; then
        echo "$child"
        return
    fi
    local p comm
    for p in /proc/[0-9]*; do
        comm=""
        read -r comm < "$p/comm" 2>/dev/null
        if [ "$comm" = "eve-server" ]; then
            echo "${p#/proc/}"
            return
        fi
    done
}
stop_server() {
    stopping=1
    local pid
    pid=$(server_pid)
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        echo "=== Stop requested:  eve-server is saving and shutting down ==="
        kill -TERM "$pid" 2>/dev/null
    elif [ -n "$gdb_pid" ]; then
        kill -TERM "$gdb_pid" 2>/dev/null
    fi
}
trap stop_server TERM
trap : INT

if [ "$RUN_WITH_GDB" == "TRUE" ]; then
    echo "=== Running EVEmu with gdb ==="
    # SIGTERM (a stop, passed on above) goes through to the server instead of stopping it in gdb
    gdb -ex 'handle SIGTERM nostop noprint pass' -ex run ./eve-server <&0 &
    child=$!
    gdb_pid=$child
else
    echo "=== Running EVEmu normally ==="
    ./eve-server <&0 &
    child=$!
fi
# a stop that came in while the server was being started
if [ "$stopping" = 1 ]; then
    stop_server
fi

# wait returns early whenever a signal comes in, so wait until the program has really ended
while kill -0 "$child" 2>/dev/null; do
    wait "$child"
    if [ "$stopping" = 1 ] && [ -n "$gdb_pid" ]; then
        # gdb stays at its prompt after the server exits:  close it once the server has gone
        while [ -n "$(server_pid)" ]; do
            sleep 1
        done
        kill -TERM "$gdb_pid" 2>/dev/null
    fi
done
wait "$child"
status=$?

if [ -n "$gdb_pid" ]; then
    # gdb's own exit status says nothing about the server
    if [ "$stopping" = 1 ]; then
        status=0
    fi
fi
exit $status