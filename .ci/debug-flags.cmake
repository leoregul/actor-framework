set(CAF_ENABLE_ACTOR_PROFILER ON CACHE BOOL "")
set(CAF_ENABLE_EXAMPLES ON CACHE BOOL "")
set(CAF_ENABLE_RUNTIME_CHECKS ON CACHE BOOL "")
set(CAF_LOG_LEVEL TRACE CACHE STRING "")
if(NOT MSVC AND NOT CMAKE_SYSTEM MATCHES BSD)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "")
endif()
