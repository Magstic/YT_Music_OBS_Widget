cmake_minimum_required(VERSION 3.16)
project(obs-audio-ws-plugin)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 若指定 libobs_DIR，順便加入 OBS 原始碼的 cmake/finders 到 CMAKE_MODULE_PATH。
if(libobs_DIR)
    get_filename_component(_libobs_build_dir "${libobs_DIR}" DIRECTORY)    # .../obs-studio/build
    get_filename_component(_obs_source_dir "${_libobs_build_dir}" DIRECTORY) # .../obs-studio
    if(EXISTS "${_obs_source_dir}/cmake/finders")
        list(APPEND CMAKE_MODULE_PATH "${_obs_source_dir}/cmake/finders")
    endif()
endif()

# 先找 OBS::libobs，找不到時退回舊版 LibObs。
find_package(libobs)
if(NOT TARGET OBS::libobs)
    message(WARNING "No modern OBS::libobs target found, trying legacy LibObs.")
    find_package(LibObs REQUIRED)

    add_library(OBS::libobs INTERFACE IMPORTED)
    target_link_libraries(OBS::libobs INTERFACE ${LIBOBS_LIBRARIES})
    target_include_directories(OBS::libobs INTERFACE ${LIBOBS_INCLUDE_DIRS})
endif()

file(GLOB SRC_FILES
    src/module.cpp
    src/audio_ws_source.cpp
    src/websocket_server.cpp
)

add_library(obs-audio-ws-plugin MODULE ${SRC_FILES})

set_target_properties(obs-audio-ws-plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "Audio WebSocket Analyzer"
)

target_link_libraries(obs-audio-ws-plugin PRIVATE OBS::libobs)

if(WIN32)
    target_link_libraries(obs-audio-ws-plugin PRIVATE ws2_32)
endif()

# 安裝到 OBS 的 obs-plugins/64bit 目錄。
install(TARGETS obs-audio-ws-plugin
    DESTINATION "obs-plugins/64bit")
