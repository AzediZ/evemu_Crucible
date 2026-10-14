#
# CMake build system file for EVEmu
#
# EVEDBTool (Go):  the database installer/migrator, built from the vendored source here
#  (EvEmu-Project/EVEDBTool 0.0.6, 2ab3897, with its modules in vendor/ so no network is needed)
#
# Needs a Go toolchain >= 1.19.  Without one the target is skipped;  sql/get_evedbtool.sh still
#  downloads the release binary
#

SET( EVEDBTOOL_VERSION "0.0.6-evemu"
     CACHE STRING "Version string compiled into evedbtool" )

FIND_PROGRAM( GO_EXECUTABLE "go" )

IF( NOT GO_EXECUTABLE )
  MESSAGE( STATUS "Go not found: evedbtool will not be built (install Go >= 1.19, or use sql/get_evedbtool.sh)" )
  RETURN()
ENDIF( NOT GO_EXECUTABLE )

IF( WIN32 )
  SET( EVEDBTOOL_BINARY "${CMAKE_CURRENT_BINARY_DIR}/bin/evedbtool.exe" )
ELSE( WIN32 )
  SET( EVEDBTOOL_BINARY "${CMAKE_CURRENT_BINARY_DIR}/bin/evedbtool" )
ENDIF( WIN32 )

FILE( GLOB_RECURSE EVEDBTOOL_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.go"
  "${CMAKE_CURRENT_SOURCE_DIR}/go.mod"
  "${CMAKE_CURRENT_SOURCE_DIR}/go.sum"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/modules.txt" )

# static and offline (-mod=vendor, GOPROXY=off), no VCS stamping (the Docker build has a .git without
#  objects), and with Go's caches inside the build tree so the build needs no writable $HOME
ADD_CUSTOM_COMMAND(
  OUTPUT "${EVEDBTOOL_BINARY}"
  COMMAND "${CMAKE_COMMAND}" -E env
          CGO_ENABLED=0 GOFLAGS=-mod=vendor GOTOOLCHAIN=local GOPROXY=off
          "GOCACHE=${CMAKE_CURRENT_BINARY_DIR}/go-cache"
          "GOPATH=${CMAKE_CURRENT_BINARY_DIR}/go-path"
          "${GO_EXECUTABLE}" build -trimpath -buildvcs=false
          "-ldflags=-X main.version=${EVEDBTOOL_VERSION}"
          -o "${EVEDBTOOL_BINARY}" .
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS ${EVEDBTOOL_SOURCES}
  COMMENT "Building evedbtool (Go)"
  VERBATIM )

ADD_CUSTOM_TARGET( "evedbtool" ALL
  DEPENDS "${EVEDBTOOL_BINARY}" )

INSTALL( PROGRAMS "${EVEDBTOOL_BINARY}"
         DESTINATION "bin" )
