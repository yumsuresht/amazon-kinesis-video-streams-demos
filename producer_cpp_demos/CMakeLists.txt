cmake_minimum_required(VERSION 3.0)
include(ExternalProject)
project(KinesisVideoProducerCppSamples)

set(KINESIS_VIDEO_OPEN_SOURCE_SRC ${CMAKE_BINARY_DIR}/open-source)

# User Flags
option(BUILD_DEPENDENCIES "Whether or not to build depending libraries from source" ON)
option(BUILD_OPENSSL_PLATFORM "If buildng OpenSSL what is the target platform" OFF)

find_package(PkgConfig REQUIRED)

ExternalProject_Add(kinesis_video_producer_cpp
    GIT_REPOSITORY https://github.com/awslabs/amazon-kinesis-video-streams-producer-sdk-cpp
    INSTALL_DIR ${KINESIS_VIDEO_OPEN_SOURCE_SRC}
    CMAKE_ARGS
      -DBUILD_DEPENDENCIES=${BUILD_DEPENDENCIES}
      -DBUILD_OPENSSL_PLATFORM=${BUILD_OPENSSL_PLATFORM}
      -DBUILD_GSTREAMER_PLUGIN=ON
)

pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
include_directories(${GST_APP_INCLUDE_DIRS})
link_directories(${GST_APP_LIBRARY_DIRS})

add_executable(kinesis_video_gstreamer_sample_app samples/kinesis_video_gstreamer_sample_app.cpp)
add_dependencies(kinesis_video_gstreamer_sample_app kinesis_video_producer_cpp)
target_link_libraries(kinesis_video_gstreamer_sample_app ${GST_APP_LIBRARIES} KinesisVideoProducer)

add_executable(kinesis_video_gstreamer_sample_multistream_app samples/kinesis_video_gstreamer_sample_multistream_app.cpp)
add_dependencies(kinesis_video_gstreamer_sample_multistream_app kinesis_video_producer_cpp)
target_link_libraries(kinesis_video_gstreamer_sample_multistream_app ${GST_APP_LIBRARIES} KinesisVideoProducer)

add_executable(kinesis_video_gstreamer_audio_video_sample_app samples/kinesis_video_gstreamer_audio_video_sample_app.cpp)
add_dependencies(kinesis_video_gstreamer_audio_video_sample_app kinesis_video_producer_cpp)
target_link_libraries(kinesis_video_gstreamer_audio_video_sample_app ${GST_APP_LIBRARIES} KinesisVideoProducer)

add_executable(kinesis_file_gstreamer_sample_app samples/kinesis_file_gstreamer_sample_app.cpp)
add_dependencies(kinesis_file_gstreamer_sample_app kinesis_video_producer_cpp)
target_link_libraries(kinesis_file_gstreamer_sample_app ${GST_APP_LIBRARIES} KinesisVideoProducer)
