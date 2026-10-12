#
# CMake build system file for EVEmu.
#
# eve-market-sim:  the market simulation's offline harness and the tests of the server code that has no server
#  dependencies (market/MarketMatch.h and the sim core in market/sim).  It links no EVEmu library.
#

##############
# Initialize #
##############
SET( TARGET_NAME        "eve-market-sim" )
SET( TARGET_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src/${TARGET_NAME}" )
SET( TARGET_SOURCE_DIR  "${PROJECT_SOURCE_DIR}/src/${TARGET_NAME}" )
SET( SERVER_SOURCE_DIR  "${PROJECT_SOURCE_DIR}/src/eve-server" )

# a simulated year is millions of cell visits:  the harness is built optimised in every configuration (these
#  variables are scoped to this directory)
IF( MSVC )
  STRING( REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}" )
  STRING( REPLACE "/Od" "/O2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}" )
  STRING( REPLACE "/Ob0" "/Ob2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}" )
ELSE()
  SET( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O2" )
ENDIF()

#########
# Files #
#########
SET( INCLUDE
     "${TARGET_INCLUDE_DIR}/HarnessData.h"
     "${TARGET_INCLUDE_DIR}/HarnessFacilities.h"
     "${TARGET_INCLUDE_DIR}/HarnessMarket.h"
     "${TARGET_INCLUDE_DIR}/Json.h"
     "${TARGET_INCLUDE_DIR}/TestCheck.h"
     "${SERVER_SOURCE_DIR}/market/MarketMatch.h" )
SET( SOURCE
     "${TARGET_SOURCE_DIR}/eve-market-sim.cpp"
     "${TARGET_SOURCE_DIR}/FacilitiesTest.cpp"
     "${TARGET_SOURCE_DIR}/Harness.cpp"
     "${TARGET_SOURCE_DIR}/HarnessData.cpp"
     "${TARGET_SOURCE_DIR}/HarnessFacilities.cpp"
     "${TARGET_SOURCE_DIR}/HarnessMarket.cpp"
     "${TARGET_SOURCE_DIR}/LogisticsTest.cpp"
     "${TARGET_SOURCE_DIR}/MatchTest.cpp"
     "${TARGET_SOURCE_DIR}/Phase3Test.cpp"
     "${TARGET_SOURCE_DIR}/Phase4Test.cpp"
     "${TARGET_SOURCE_DIR}/SimTest.cpp" )
# the sim core, shared with eve-server
SET( core_INCLUDE
     "${SERVER_SOURCE_DIR}/market/sim/IMarketGateway.h"
     "${SERVER_SOURCE_DIR}/market/sim/SimClock.h"
     "${SERVER_SOURCE_DIR}/market/sim/SimEconomy.h"
     "${SERVER_SOURCE_DIR}/market/sim/SimFacilities.h"
     "${SERVER_SOURCE_DIR}/market/sim/SimLineCapacity.h"
     "${SERVER_SOURCE_DIR}/market/sim/SimLedger.h"
     "${SERVER_SOURCE_DIR}/market/sim/SimLogistics.h"
     "${SERVER_SOURCE_DIR}/market/sim/SimProduction.h"
     "${SERVER_SOURCE_DIR}/market/sim/SimRng.h"
     "${SERVER_SOURCE_DIR}/market/sim/SimStaticData.h"
     "${SERVER_SOURCE_DIR}/market/sim/SimTypes.h"
     "${SERVER_SOURCE_DIR}/market/sim/SimWorld.h" )
SET( core_SOURCE
     "${SERVER_SOURCE_DIR}/market/sim/SimActors.cpp"
     "${SERVER_SOURCE_DIR}/market/sim/SimEconomy.cpp"
     "${SERVER_SOURCE_DIR}/market/sim/SimFacilities.cpp"
     "${SERVER_SOURCE_DIR}/market/sim/SimHaulers.cpp"
     "${SERVER_SOURCE_DIR}/market/sim/SimLineCapacity.cpp"
     "${SERVER_SOURCE_DIR}/market/sim/SimLedger.cpp"
     "${SERVER_SOURCE_DIR}/market/sim/SimLogistics.cpp"
     "${SERVER_SOURCE_DIR}/market/sim/SimProduction.cpp"
     "${SERVER_SOURCE_DIR}/market/sim/SimStaticData.cpp"
     "${SERVER_SOURCE_DIR}/market/sim/SimWorld.cpp" )

########################
# Setup the executable #
########################
SOURCE_GROUP( "src" FILES ${INCLUDE} ${SOURCE} )
SOURCE_GROUP( "src\\core" FILES ${core_INCLUDE} ${core_SOURCE} )

ADD_EXECUTABLE( "${TARGET_NAME}"
                ${INCLUDE} ${SOURCE} ${core_INCLUDE} ${core_SOURCE} )
TARGET_INCLUDE_DIRECTORIES( "${TARGET_NAME}"
                            "${TARGET_INCLUDE_DIR}"
                            "${SERVER_SOURCE_DIR}" )
IF( MSVC )
  # CMake 3.31+ adds the runtime checks through this property, and they don't go with /O2
  SET_PROPERTY( TARGET "${TARGET_NAME}" PROPERTY MSVC_RUNTIME_CHECKS "" )
ENDIF()

#########
# Tests #
#########
ADD_TEST( NAME "MarketMatchTest"
          COMMAND "${TARGET_NAME}" --test match )
ADD_TEST( NAME "MarketSimEconomyTest"
          COMMAND "${TARGET_NAME}" --test economy )
ADD_TEST( NAME "MarketSimLedgerTest"
          COMMAND "${TARGET_NAME}" --test ledger )
ADD_TEST( NAME "MarketSimWorldTest"
          COMMAND "${TARGET_NAME}" --test world )
ADD_TEST( NAME "MarketSimFacilitiesTest"
          COMMAND "${TARGET_NAME}" --test facilities )
ADD_TEST( NAME "MarketSimProductionTest"
          COMMAND "${TARGET_NAME}" --test production )
ADD_TEST( NAME "MarketSimActorsTest"
          COMMAND "${TARGET_NAME}" --test actors )
ADD_TEST( NAME "MarketSimLogisticsTest"
          COMMAND "${TARGET_NAME}" --test logistics )
# the phase 2 acceptance tests:  a simulated year at EconomyScale 0.01, five seeds, plus a determinism rerun
ADD_TEST( NAME "MarketSimAcceptance"
          COMMAND "${TARGET_NAME}" --synthetic --days 365 --seeds 5 )
SET_TESTS_PROPERTIES( "MarketSimAcceptance" PROPERTIES TIMEOUT 3600 )
# the phase 3 acceptance tests:  a mineral shock and a Drake losses shock against a control run, no negatives, T1 price /
#  bill of materials at steady state (Phase3Test.cpp)
ADD_TEST( NAME "MarketSimPhase3"
          COMMAND "${TARGET_NAME}" --synthetic --phase3 --days 120 --seeds 3 )
SET_TESTS_PROPERTIES( "MarketSimPhase3" PROPERTIES TIMEOUT 3600 )
# the phase 4 acceptance tests:  hub price ratios (as calibrated, and pulled apart), RiskLowSec x10 against a control run,
#  shipment losses and hull replacements, the books (Phase4Test.cpp)
ADD_TEST( NAME "MarketSimPhase4"
          COMMAND "${TARGET_NAME}" --synthetic --phase4 --days 120 --seeds 2 )
SET_TESTS_PROPERTIES( "MarketSimPhase4" PROPERTIES TIMEOUT 3600 )
