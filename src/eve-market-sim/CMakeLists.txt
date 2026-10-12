#
# CMake build system file for EVEmu.
#
# eve-market-sim:  the offline harness and tests of the server code that has no server dependencies
#  (market/MarketMatch.h).  It links no EVEmu library.
#

##############
# Initialize #
##############
SET( TARGET_NAME        "eve-market-sim" )
SET( TARGET_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src/${TARGET_NAME}" )
SET( TARGET_SOURCE_DIR  "${PROJECT_SOURCE_DIR}/src/${TARGET_NAME}" )
SET( SERVER_SOURCE_DIR  "${PROJECT_SOURCE_DIR}/src/eve-server" )

#########
# Files #
#########
SET( INCLUDE
     "${TARGET_INCLUDE_DIR}/TestCheck.h"
     "${SERVER_SOURCE_DIR}/market/MarketMatch.h" )
SET( SOURCE
     "${TARGET_SOURCE_DIR}/eve-market-sim.cpp"
     "${TARGET_SOURCE_DIR}/MatchTest.cpp" )

########################
# Setup the executable #
########################
SOURCE_GROUP( "src" FILES ${INCLUDE} ${SOURCE} )

ADD_EXECUTABLE( "${TARGET_NAME}"
                ${INCLUDE} ${SOURCE} )
TARGET_INCLUDE_DIRECTORIES( "${TARGET_NAME}"
                            "${TARGET_INCLUDE_DIR}"
                            "${SERVER_SOURCE_DIR}" )

#########
# Tests #
#########
ADD_TEST( NAME "MarketMatchTest"
          COMMAND "${TARGET_NAME}" --test match )
