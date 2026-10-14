# EVEmu server image (Debian 12, GCC 12).  Needs BuildKit (the default builder for docker and docker compose).
#
# Stages:
#   build-env   the toolchain and libraries.  Cached as a layer, and a usable build container on its own:
#                 docker build --target build-env -t evemu-build-env .
#                 docker run --rm -v "$PWD":/src:ro -v evemu-build-env-cache:/build evemu-build-env \
#                     sh -c 'cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Debug && make -C /build -j"$(nproc)"'
#   evedbtool   the EVEDBTool binary, fetched once per EVEDBTOOL_VERSION instead of on every sql/ change
#   app-build   compiles the server.  The build tree and ccache live in cache mounts (ids evemu-build and
#               evemu-ccache), so a rebuild only recompiles what changed.  To start clean (e.g. a stale CMake
#               cache) rename the ids, or `docker builder prune --filter type=exec.cachemount` (clears every
#               project's cache mounts)
#   app         the runtime image

ARG EVEDBTOOL_VERSION=0.0.6

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
    libutfcpp-dev \
    mariadb-client \
    passwd

# EVEDBTool, pinned so the download is cached with this layer
FROM debian:12 AS evedbtool
ARG EVEDBTOOL_VERSION
ARG TARGETARCH
ADD --chmod=755 https://github.com/EvEmu-Project/EVEDBTool/releases/download/${EVEDBTOOL_VERSION}/evedbtool /out/evedbtool-amd64
ADD --chmod=755 https://github.com/EvEmu-Project/EVEDBTool/releases/download/${EVEDBTOOL_VERSION}/evedb_aarch64 /out/evedbtool-arm64
RUN mv /out/evedbtool-${TARGETARCH:-amd64} /out/evedbtool && rm -f /out/evedbtool-*

# Build stage
FROM build-env AS app-build

# Add project files
ADD CMakeLists.txt /src/
ADD config.h.in /src/
ADD /cmake/ /src/cmake
ADD /dep/ /src/dep
ADD /src/ /src/src
ADD /utils/ /src/utils
ADD /.git/ /src/.git

# Create necessary directories
RUN mkdir -p /app /app/logs /app/server_cache /app/image_cache

# Set working directory
WORKDIR /src/build

# Configure, build and install.  /src/build and the ccache persist between builds (cache mounts), so only
#  changed files recompile;  ccache also covers anything make thinks is stale
ENV CCACHE_DIR=/ccache
RUN --mount=type=cache,id=evemu-build,target=/src/build \
    --mount=type=cache,id=evemu-ccache,target=/ccache \
    cmake -DCMAKE_INSTALL_PREFIX=/app -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
          -DCMAKE_C_COMPILER_LAUNCHER=ccache .. && \
    make -j$(nproc) && \
    make install

# Final runtime image
FROM build-env AS app

LABEL description="EVEmu Server"

# Copy built assets
COPY --from=app-build /src/utils/ /src/utils
COPY --from=app-build /app/ /app

# SQL loading tools (EVEDBTool comes from its own cached stage, not a download per build)
ADD /sql/ /src/sql
COPY --from=evedbtool /out/evedbtool /src/sql/evedbtool

# Expose server ports
EXPOSE 26000
EXPOSE 26001

# Change ownership of the application to evemu
RUN useradd evemu
RUN chown -R evemu:evemu /app
RUN chown -R evemu:evemu /src

ENTRYPOINT ["/bin/sh", "/src/utils/container-scripts/entry.sh"]

# Default command
CMD ["/src/utils/container-scripts/start.sh"]
