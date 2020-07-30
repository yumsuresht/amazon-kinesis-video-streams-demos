cmake_minimum_required(VERSION 3.11.4)
project(KVSWebRTCCanary LANGUAGES C)

include(FetchContent)
FetchContent_Declare(
  webrtc
  GIT_REPOSITORY https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c
  GIT_TAG        master
)

FetchContent_GetProperties(webrtc)
if(NOT webrtc_POPULATED)
  FetchContent_Populate(webrtc)
  add_subdirectory(${webrtc_SOURCE_DIR} ${webrtc_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

include_directories(${webrtc_SOURCE_DIR}/src/include)
include_directories(${webrtc_SOURCE_DIR}/open-source/include)
link_directories(${webrtc_SOURCE_DIR}/open-source/lib)
add_executable(
  kvsWebrtcClientMaster2
  ${CMAKE_CURRENT_SOURCE_DIR}/samples/Common.c
  ${CMAKE_CURRENT_SOURCE_DIR}/samples/kvsWebrtcClientMaster.c)
target_link_libraries(kvsWebrtcClientMaster2 kvsWebrtcClient kvsWebrtcSignalingClient kvspicUtils)
