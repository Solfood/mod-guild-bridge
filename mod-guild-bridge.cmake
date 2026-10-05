# Included by the core's modules/CMakeLists.txt after the `modules` target exists.
# Puts the module's short git SHA into the build so `bridge status` shows what is deployed.
execute_process(COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}" rev-parse --short=8 HEAD
                OUTPUT_VARIABLE GUILDBRIDGE_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT GUILDBRIDGE_SHA)
  set(GUILDBRIDGE_SHA "dev")
endif()
if(TARGET modules)
  target_compile_definitions(modules PRIVATE GUILDBRIDGE_VERSION="${GUILDBRIDGE_SHA}")
endif()
