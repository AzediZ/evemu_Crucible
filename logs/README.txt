Server logs
===========

The server writes everything it prints to a file in this folder, one file per start:

    DD-MM-YYYY-HH-MM.log        (the date and time it started, the server's clock)

That is the same output the control panel's Activity tab shows and that "docker logs server" gives, kept
where it can be read, searched and kept after the container is recreated.  It holds the whole start
(the database step, the static data, a time skip if one was asked for) and
then everything the server says while it runs.

The container start script keeps the newest 20 files here and removes the rest, so the folder doesn't grow
without end.  Deleting any of them while the server runs is safe, apart from the one it is writing.

A release build writes the file at most a second behind what the console shows, and line by line once
the server is stopping.

How it works: src/eve-core/log/LogNew.cpp (the file and its name), the logs line in docker-compose.yml
(maps this folder into the container at /app/logs) and utils/container-scripts/start.sh (the pruning).
