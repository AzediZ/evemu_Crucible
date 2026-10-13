# EVEmu server image.
#
# Two stages:
#   build   - compilers and development libraries; compiles the server.  A compile cache (ccache)
#             is kept between builds, so a rebuild only recompiles files that changed.
#   runtime - a slim image with just the server, the libraries it links against and the tools
#             the start scripts need.  This is the image that runs.

# ---------------------------------------------------------------------------
# Build stage
# ---------------------------------------------------------------------------
FROM debian:12 AS build

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    ccache \
    cmake \
    libboost-all-dev \
    libmariadb-dev \
    libtinyxml-dev \
    libutfcpp-dev \
    zlib1g-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Project files.  .git is left out on purpose: the commit hash would be compiled into every
# file and make each new commit a full rebuild.
COPY CMakeLists.txt config.h.in /src/
COPY cmake /src/cmake
COPY dep /src/dep
COPY src /src/src
COPY utils /src/utils

WORKDIR /src/build

# Compile.  The ccache directory is a build cache that survives between builds.
#  Release: optimised, much smaller and faster to build than the old Debug build.
#  The build folder is deleted in the same step, so this layer only holds the installed server.
ENV CCACHE_DIR=/ccache \
    CCACHE_MAXSIZE=3G \
    CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime
RUN --mount=type=cache,id=evemu-ccache,target=/ccache \
    cmake -DCMAKE_INSTALL_PREFIX=/app -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
          -DCMAKE_CXX_FLAGS="-fpch-preprocess" .. && \
    make -j$(nproc) && \
    make install && \
    ccache --show-stats | head -20 && \
    cd / && rm -rf /src/build

# Gather the shared libraries the server needs (except the C/C++ runtime, which every image has).
RUN mkdir -p /app/lib && \
    ldd /app/bin/eve-server | awk '/=> \// {print $3}' | \
    grep -v -E '/(libc|libm|libpthread|libdl|librt|libgcc_s|libstdc\+\+|ld-linux[^/]*)\.so' | \
    xargs -r -I{} cp -L {} /app/lib/ && \
    ls /app/lib

# ---------------------------------------------------------------------------
# Runtime stage
# ---------------------------------------------------------------------------
FROM debian:12-slim AS app

LABEL description="EVEmu Server"

# mariadb-client and curl are used by the database setup scripts; passwd provides useradd.
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    mariadb-client \
    passwd \
    && apt-get clean && rm -rf /var/lib/apt/lists/* \
    && useradd evemu

# The server and its libraries, owned by the evemu user as they are copied in
#  (a separate chown step would store a second copy of every file in the image)
COPY --from=build --chown=evemu:evemu /app/ /app
COPY --from=build --chown=evemu:evemu /src/utils/ /src/utils
RUN echo "/app/lib" > /etc/ld.so.conf.d/evemu.conf && ldconfig

# Add SQL loading tools
COPY --chown=evemu:evemu sql/ /src/sql

# Run SQL tool script
RUN cd /src/sql && ./get_evedbtool.sh && chown evemu:evemu evedbtool

# Expose server ports
EXPOSE 26000
EXPOSE 26001

ENTRYPOINT ["/bin/sh", "/src/utils/container-scripts/entry.sh"]

# Default command
CMD ["/src/utils/container-scripts/start.sh"]
