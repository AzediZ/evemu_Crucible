#
# eve-bot: headless test client (not built by default; configure with -DEVEMU_BUILD_BOT=ON)
#
SET( TARGET_NAME        "eve-bot" )
SET( TARGET_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src/${TARGET_NAME}" )
SET( TARGET_SOURCE_DIR  "${PROJECT_SOURCE_DIR}/src/${TARGET_NAME}" )

ADD_EXECUTABLE( "${TARGET_NAME}"
                "${TARGET_INCLUDE_DIR}/eve-bot.h"
                "${TARGET_SOURCE_DIR}/eve-bot.cpp" )
TARGET_INCLUDE_DIRECTORIES( "${TARGET_NAME}"
                            ${eve-common_INCLUDE_DIRS}
                            "${TARGET_INCLUDE_DIR}" )
TARGET_LINK_LIBRARIES( "${TARGET_NAME}"
                       "eve-common" )
