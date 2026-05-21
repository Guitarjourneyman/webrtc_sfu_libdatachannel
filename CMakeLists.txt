cmake_minimum_required(VERSION 3.24)

project(webcodecs_libdatachannel_toy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LDC_TOY_FETCH_LIBDATACHANNEL "Fetch libdatachannel with CMake FetchContent" ON)

if(LDC_TOY_FETCH_LIBDATACHANNEL)
  include(FetchContent)
  FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.23.0
  )

  set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
  set(NO_TESTS ON CACHE BOOL "" FORCE)
  set(USE_NICE OFF CACHE BOOL "" FORCE)
  set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
  set(USE_MBEDTLS OFF CACHE BOOL "" FORCE)
  set(USE_OPENSSL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(libdatachannel)
else()
  find_package(LibDataChannel CONFIG REQUIRED)
endif()

add_library(ldc_toy_core
  src/encoded_chunk.cpp
  src/h264_rtp_packetizer.cpp
  src/native_sfu.cpp
)

target_include_directories(ldc_toy_core PUBLIC include)

if(TARGET datachannel)
  target_link_libraries(ldc_toy_core PUBLIC datachannel)
elseif(TARGET LibDataChannel::LibDataChannel)
  target_link_libraries(ldc_toy_core PUBLIC LibDataChannel::LibDataChannel)
else()
  message(FATAL_ERROR "Could not find a libdatachannel CMake target")
endif()

add_executable(ldc_toy_demo src/main.cpp)
target_link_libraries(ldc_toy_demo PRIVATE ldc_toy_core)

