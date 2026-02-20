cmake_minimum_required(VERSION 3.14)
project(roram CXX)
set(CMAKE_CXX_STANDARD 17)

set(RORAM_SOURCES
  src/types.cpp
  src/block.cpp
  src/crypto.cpp
  src/position_map.cpp
  src/storage_mem.cpp
  src/storage_file.cpp
  src/sub_oram.cpp
  src/roram.cpp
)

add_library(roram ${RORAM_SOURCES})
target_include_directories(roram PUBLIC include)
# Optional: enable OpenSSL for crypto: -DRORAM_USE_OPENSSL=ON
if(RORAM_USE_OPENSSL)
  find_package(OpenSSL REQUIRED)
  target_compile_definitions(roram PRIVATE RORAM_USE_OPENSSL)
  target_link_libraries(roram PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

add_executable(roram_main src/main.cpp)
target_link_libraries(roram_main PRIVATE roram)
target_include_directories(roram_main PRIVATE include)
