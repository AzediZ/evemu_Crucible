# EVEmu server image (Debian 12, GCC 12).  Needs BuildKit (the default builder for docker and docker compose).
#
# Stages:
#   build-env   the toolchain and libraries.  Cached as a layer, and a usable build container on its own:
#                 docker build --target build-env -t evemu-build-env .
#                 docker run --rm -v "$PWD":/src:ro -v evemu-build-env-cache:/build evemu-build-env \
#                     sh -c 'cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Debug && make -C /build -j"$(nproc)"'
#   app-build   compiles the server (Release) and EVEDBTool (Go, vendored in dep/evedbtool, built offline).  The
#               build tree and ccache live in cache mounts (ids evemu-build and evemu-ccache), so a rebuild
#               only recompiles what changed.  To start clean (e.g. a stale CMake cache) rename the ids, or
#               `docker builder prune --filter type=exec.cachemount` (clears every project's cache mounts)
#   app         the runtime image:  a slim Debian with just the server, EVEDBTool, the libraries the server
#               links against and the tools the container scripts use

# Toolchain and libraries
FROM debian:12 AS build-env

# keep downloaded .debs in a cache mount, so a change to this list doesn't download everything again
RUN rm -f /etc/apt/apt.conf.d/docker-clean
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    ccache \
    cmake \
    git \
    curl \
    wget \
    zlib1g-dev \
    libmariadb-dev \
    libboost-all-dev \
    libtinyxml-dev \
    ca-certificates \
    g++ \
    gdb \
    golang-go \
    libutfcpp-dev \
    mariadb-client \
    passwd

# Build stage
FROM build-env AS app-build

# Project files.  .git is left out on purpose (.dockerignore): the commit hash would be compiled into every
#  file and make each new commit a full rebuild.  The server's revision shows as "invalid" because of it
COPY CMakeLists.txt config.h.in /src/
COPY cmake /src/cmake
COPY dep /src/dep
COPY src /src/src
COPY utils /src/utils

# Create necessary directories
RUN mkdir -p /app /app/logs /app/server_cache /app/image_cache

# Set working directory
WORKDIR /src/build

# Configure, build and install.  /src/build and the ccache persist between builds (cache mounts), so only
#  changed files recompile;  ccache also covers anything make thinks is stale.
#  Release: optimised, and much faster to build than Debug.  Only the server and EVEDBTool are built:  the unit
#  tests (eve-test) and the market simulation harness (eve-market-sim) aren't needed in the image
ENV CCACHE_DIR=/ccache \
    CCACHE_MAXSIZE=3G \
    CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime
RUN --mount=type=cache,id=evemu-build,target=/src/build \
    --mount=type=cache,id=evemu-ccache,target=/ccache \
    cmake -DCMAKE_INSTALL_PREFIX=/app -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
          -DCMAKE_CXX_FLAGS="-fpch-preprocess" -DEVEMU_BUILD_TESTS=OFF .. && \
    make -j$(nproc) eve-server evedbtool && \
    cmake --install . && \
    test -x /app/bin/evedbtool && \
    ccache --show-stats | head -20

# Gather the shared libraries the server needs (except the C/C++ runtime, which every image has)
RUN mkdir -p /app/lib && \
    ldd /app/bin/eve-server | awk '/=> \// {print $3}' | \
    grep -v -E '/(libc|libm|libpthread|libdl|librt|libgcc_s|libstdc\+\+|ld-linux[^/]*)\.so' | \
    xargs -r -I{} cp -L {} /app/lib/ && \
    ls /app/lib

# Final runtime image
FROM debian:12-slim AS app

LABEL description="EVEmu Server"

# mariadb-client is used by the database setup script (db_init.sh); passwd provides useradd.
#  gdb (for RUN_GDB=TRUE) brings Python and tens of MB with it, so it's only installed when the image is built
#  with WITH_GDB=TRUE (docker-compose.yml passes RUN_GDB as WITH_GDB:  `RUN_GDB=TRUE docker compose up -d --build`)
ARG WITH_GDB=FALSE
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates \
    mariadb-client \
    passwd \
    $(if [ "$WITH_GDB" = "TRUE" ]; then echo gdb; fi) \
    && apt-get clean && rm -rf /var/lib/apt/lists/* \
    && useradd evemu

# The server, EVEDBTool and the libraries, owned by the evemu user as they are copied in
#  (a separate chown step would store a second copy of every file in the image)
COPY --from=app-build --chown=evemu:evemu /app/ /app
COPY --chown=evemu:evemu utils/ /src/utils
RUN echo "/app/lib" > /etc/ld.so.conf.d/evemu.conf && ldconfig

# SQL loading tools.  EVEDBTool is built from dep/evedbtool with the server (installed to /app/bin) and
#  goes where the container scripts expect it
COPY --chown=evemu:evemu sql/ /src/sql
RUN cp /app/bin/evedbtool /src/sql/evedbtool && chown evemu:evemu /src/sql/evedbtool

# Expose server ports
EXPOSE 26000
EXPOSE 26001

ENTRYPOINT ["/bin/sh", "/src/utils/container-scripts/entry.sh"]

# Default command
CMD ["/src/utils/container-scripts/start.sh"]
