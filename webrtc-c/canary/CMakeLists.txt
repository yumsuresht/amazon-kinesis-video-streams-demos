cmake_minimum_required(VERSION 3.14)
project(KVSWebRTCCanary LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)

include(FetchContent)

# AWS SDK options
set(CUSTOM_MEMORY_MANAGEMENT OFF)
set(ENABLE_TESTING OFF)
set(BUILD_ONLY "monitoring;logs")

FetchContent_Declare(
  webrtc
  GIT_REPOSITORY https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c
  GIT_TAG        master
)

FetchContent_Declare(
  cloudwatch
  GIT_REPOSITORY https://github.com/aws/aws-sdk-cpp
  GIT_TAG        1.8.17
)

FetchContent_MakeAvailable(webrtc cloudwatch)

include_directories(${cloudwatch_SOURCE_DIR}/aws-cpp-sdk-core/include)
include_directories(${cloudwatch_SOURCE_DIR}/aws-cpp-sdk-monitoring/include)
include_directories(${cloudwatch_SOURCE_DIR}/aws-cpp-sdk-logs/include)
include_directories(${webrtc_SOURCE_DIR}/src/include)
include_directories(${webrtc_SOURCE_DIR}/open-source/include)
link_directories(${webrtc_SOURCE_DIR}/open-source/lib)
add_executable(
  kvsWebrtcClientMaster2
  src/Config.cpp
  src/CloudwatchLogger.cpp
  src/Cloudwatch.cpp
  src/Common.cpp
  src/Master.cpp)
target_link_libraries(
  kvsWebrtcClientMaster2
  kvsWebrtcClient
  kvsWebrtcSignalingClient
  kvspicUtils
  aws-cpp-sdk-core
  aws-cpp-sdk-monitoring
  aws-cpp-sdk-logs)
