cmake_minimum_required(VERSION 3.20)
project(VIDEO_SEATCH_FINDER)

# 1. 核心編譯設定
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. 尋找 FFmpeg
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    # 使用 IMPORTED_TARGET 是現代 CMake 最穩的做法
    pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
    pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
    pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
    pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
    
    set(FFMPEG_TARGETS 
        PkgConfig::AVCODEC 
        PkgConfig::AVFORMAT 
        PkgConfig::AVUTIL 
        PkgConfig::SWSCALE
    )
else()
    message(STATUS "PkgConfig not found, trying manual FFmpeg search")
    find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
    find_library(AVCODEC_LIBRARY avcodec)
    find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
    find_library(AVFORMAT_LIBRARY avformat)
    find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
    find_library(AVUTIL_LIBRARY avutil)
    find_path(SWSCALE_INCLUDE_DIR libswscale/swscale.h)
    find_library(SWSCALE_LIBRARY swscale)
    
    set(FFMPEG_INCLUDE_DIRS ${AVCODEC_INCLUDE_DIR} ${AVFORMAT_INCLUDE_DIR} ${AVUTIL_INCLUDE_DIR} ${SWSCALE_INCLUDE_DIR})
    set(FFMPEG_LIBRARIES ${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY})
endif()

# 3. 定義執行檔
add_executable(dupe_engine 
    main.cpp 
    include/xxhash.c
)

# 4. 指定標頭檔與連結庫
target_include_directories(dupe_engine PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

if(FFMPEG_TARGETS)
    target_link_libraries(dupe_engine PRIVATE ${FFMPEG_TARGETS})
else()
    target_include_directories(dupe_engine PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(dupe_engine PRIVATE ${FFMPEG_LIBRARIES})
endif()

if(NOT MSVC)
    target_link_libraries(dupe_engine PRIVATE pthread m)
endif()

# 6. (選填) 針對 Mac i7 的優化：開啟 AVX2 指令集
if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    target_compile_options(dupe_engine PRIVATE -mavx2 -msse4.2)
endif()

# 7. RocksDB 快取支援
find_path(ROCKSDB_INCLUDE_DIR rocksdb/db.h)
find_library(ROCKSDB_LIBRARY rocksdb)

if (ROCKSDB_INCLUDE_DIR AND ROCKSDB_LIBRARY)
    target_include_directories(dupe_engine PRIVATE ${ROCKSDB_INCLUDE_DIR})
    target_link_libraries(dupe_engine PRIVATE ${ROCKSDB_LIBRARY})
    target_compile_definitions(dupe_engine PRIVATE USE_ROCKSDB=1)
    message(STATUS "⚡ Found RocksDB: ${ROCKSDB_LIBRARY}")
else()
    message(WARNING "⚠️ RocksDB not found. Caching will be disabled. (brew install rocksdb)")
endif()