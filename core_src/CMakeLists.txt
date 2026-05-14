cmake_minimum_required(VERSION 3.20)
project(VIDEO_SEATCH_FINDER)

# 1. 核心編譯設定
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. 自動尋找系統中的 FFmpeg (Mac Homebrew 路徑)
# pkg-config 是找 FFmpeg 最穩的方式
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)

# 3. 定義執行檔
# 注意：我們把 xxhash.c 直接編進去，這樣就不怕 brew 鎖住了
add_executable(dupe_engine 
    main.cpp 
    include/xxhash.c
)

# 4. 指定標頭檔路徑
# 包含你的自訂 include 資料夾，以及 FFmpeg 的系統路徑
target_include_directories(dupe_engine PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
)

# 5. 連結動態庫
# 這裡連結的是 FFmpeg 的 .dylib 檔案
target_link_libraries(dupe_engine PRIVATE 
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    pthread    # 無鎖隊列需要執行緒支援
)

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