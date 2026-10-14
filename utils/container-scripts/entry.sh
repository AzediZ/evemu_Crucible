#!/bin/sh

chown -R evemu:evemu /app
# Switch to the evemu user without a process in between, so a docker stop's SIGTERM reaches start.sh (which passes
#  it on to eve-server).  runuser stays as a parent and kills its child with SIGKILL two seconds after a SIGTERM,
#  which cut the server's shutdown save short.
if command -v setpriv >/dev/null 2>&1; then
    export HOME="$(getent passwd evemu | cut -d: -f6)" USER=evemu LOGNAME=evemu
    exec setpriv --reuid=evemu --regid=evemu --init-groups "$@"
fi
exec runuser -u evemu "$@"
