#!/bin/bash

echo "Initializing EVEmu..."

#Initialize the database
/src/utils/container-scripts/db_init.sh

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

echo "Installing devtools and GM tools..."
cp /src/utils/config/devtools.raw /app/etc/
cp /src/utils/config/gmtools.py /app/etc/

#Start eve-server
echo "Starting eve-server..."
cd /app/bin/
if [ "$RUN_WITH_GDB" == "TRUE" ]; then
    echo "=== Running EVEmu with gdb ==="
    gdb -ex run ./eve-server
else
    echo "=== Running EVEmu normally ==="
    ./eve-server
fi