#
# CMake build system file for EVEmu.
#
# Author: Bloody.Rabbit
#

##############
# Initialize #
##############
SET( TARGET_NAME        "eve-test" )
SET( TARGET_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src/${TARGET_NAME}" )
SET( TARGET_SOURCE_DIR  "${PROJECT_SOURCE_DIR}/src/${TARGET_NAME}" )

#########
# Files #
#########
SET( INCLUDE
     "${TARGET_INCLUDE_DIR}/eve-test.h" )
# No eve-test.cpp, generated on the fly.

# You must NOT use TARGET_SOURCE_DIR (or, to be
# exact, use absolute paths) when specifying
# the test sources.
SET( auth_SOURCE
     "auth/PasswordModuleTest.cpp" )
SET( marshal_SOURCE
     "marshal/EVEMarshalTest.cpp" )
SET( ship_SOURCE
     "ship/ChargeRulesTest.cpp"
     "ship/PropulsionTest.cpp"
     "ship/WarpCapacitorTest.cpp"
     "ship/WarpSpeedTest.cpp" )
SET( npcai_SOURCE
     "npcai/AmbientRulesTest.cpp"
     "npcai/FighterWarpTest.cpp"
     "npcai/LawRulesTest.cpp"
     "npcai/LootRollTest.cpp"
     "npcai/NpcMathTest.cpp"
     "npcai/NpcPolishTest.cpp" )
SET( utils_SOURCE
     "utils/DogmaOrderTest.cpp"
     "utils/EvilNumberTest.cpp"
     "utils/CustomsRulesTest.cpp"
     "utils/LogoffRulesTest.cpp"
     "utils/OutpostRulesTest.cpp"
     "utils/SkillMathTest.cpp"
     "utils/SovRulesTest.cpp"
     "utils/StructureTimingTest.cpp" )

########################
# Setup the executable #
########################
SOURCE_GROUP( "src"      ${INCLUDE} )
SOURCE_GROUP( "src\\auth"    ${auth_SOURCE} )
SOURCE_GROUP( "src\\marshal" ${marshal_SOURCE} )
SOURCE_GROUP( "src\\ship"    ${ship_SOURCE} )
SOURCE_GROUP( "src\\npcai"   ${npcai_SOURCE} )
SOURCE_GROUP( "src\\utils"   ${utils_SOURCE} )

CREATE_TEST_SOURCELIST( TARGET_SOURCELIST "eve-test.cpp"
                        ${auth_SOURCE}
                        ${marshal_SOURCE}
                        ${ship_SOURCE}
                        ${npcai_SOURCE}
                        ${utils_SOURCE}
                        EXTRA_INCLUDE "eve-test.h" )
# the npc ai formulas (eve-server/npc/ai) are pure and dependency free, so the tests build them directly
SET( npcai_SERVER_DIR "${PROJECT_SOURCE_DIR}/src/eve-server/npc/ai" )
ADD_EXECUTABLE( "${TARGET_NAME}"
                ${TARGET_SOURCELIST}
                "${npcai_SERVER_DIR}/NpcMath.cpp"
                "${TARGET_SOURCE_DIR}/ProfilerStub.cpp" )

#TARGET_BUILD_PCH( "${TARGET_NAME}"
#                  "${TARGET_INCLUDE_DIR}/eve-test.h"
#                  "${CMAKE_CURRENT_BINARY_DIR}/eve-test.cpp" )
target_precompile_headers( "${TARGET_NAME}" PUBLIC
                  "${TARGET_INCLUDE_DIR}/eve-test.h" )
TARGET_INCLUDE_DIRECTORIES( "${TARGET_NAME}"
                            ${eve-common_INCLUDE_DIRS}
                            "${TARGET_INCLUDE_DIR}"
                            "${npcai_SERVER_DIR}" )
TARGET_LINK_LIBRARIES( "${TARGET_NAME}"
                       "eve-common" )
# the tests run from the build tree:  put the mariadb client dll (eve-core needs it) next to them
IF( WIN32 AND TARGET "mysql" )
  ADD_CUSTOM_COMMAND( TARGET "${TARGET_NAME}" POST_BUILD
                      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:mysql>" "$<TARGET_FILE_DIR:${TARGET_NAME}>" )
ENDIF( WIN32 AND TARGET "mysql" )

#########
# Tests #
#########
ADD_TEST( NAME "PasswordModuleTest"
          COMMAND "${TARGET_NAME}" "auth/PasswordModuleTest" )
ADD_TEST( NAME "EVEMarshalTest"
          COMMAND "${TARGET_NAME}" "marshal/EVEMarshalTest" )
ADD_TEST( NAME "EvilNumberTest"
          COMMAND "${TARGET_NAME}" "utils/EvilNumberTest" )
ADD_TEST( NAME "SkillMathTest"
          COMMAND "${TARGET_NAME}" "utils/SkillMathTest" )
ADD_TEST( NAME "SovRulesTest"
          COMMAND "${TARGET_NAME}" "utils/SovRulesTest" )
ADD_TEST( NAME "StructureTimingTest"
          COMMAND "${TARGET_NAME}" "utils/StructureTimingTest" )
ADD_TEST( NAME "CustomsRulesTest"
          COMMAND "${TARGET_NAME}" "utils/CustomsRulesTest" )
ADD_TEST( NAME "LogoffRulesTest"
          COMMAND "${TARGET_NAME}" "utils/LogoffRulesTest" )
ADD_TEST( NAME "OutpostRulesTest"
          COMMAND "${TARGET_NAME}" "utils/OutpostRulesTest" )
ADD_TEST( NAME "ChargeRulesTest"
          COMMAND "${TARGET_NAME}" "ship/ChargeRulesTest" )
ADD_TEST( NAME "PropulsionTest"
          COMMAND "${TARGET_NAME}" "ship/PropulsionTest" )
ADD_TEST( NAME "WarpCapacitorTest"
          COMMAND "${TARGET_NAME}" "ship/WarpCapacitorTest" )
ADD_TEST( NAME "WarpSpeedTest"
          COMMAND "${TARGET_NAME}" "ship/WarpSpeedTest" )
ADD_TEST( NAME "NpcMathTest"
          COMMAND "${TARGET_NAME}" "npcai/NpcMathTest" )
ADD_TEST( NAME "LawRulesTest"
          COMMAND "${TARGET_NAME}" "npcai/LawRulesTest" )
ADD_TEST( NAME "NpcPolishTest"
          COMMAND "${TARGET_NAME}" "npcai/NpcPolishTest" )
ADD_TEST( NAME "FighterWarpTest"
          COMMAND "${TARGET_NAME}" "npcai/FighterWarpTest" )
ADD_TEST( NAME "LootRollTest"
          COMMAND "${TARGET_NAME}" "npcai/LootRollTest" )
ADD_TEST( NAME "AmbientRulesTest"
          COMMAND "${TARGET_NAME}" "npcai/AmbientRulesTest" )
