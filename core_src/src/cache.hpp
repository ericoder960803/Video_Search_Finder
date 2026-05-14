#pragma once
#include <string>
#include <iostream>
#include <sstream>
#include "types.hpp"

#ifdef USE_ROCKSDB
#include <rocksdb/db.h>
#include <rocksdb/options.h>

class VideoCache {
private:
    rocksdb::DB* db = nullptr;

public:
    VideoCache(const std::string& db_path = "./.dupe_cache_rocksdb") {
        rocksdb::Options options;
        options.create_if_missing = true;
        rocksdb::Status status = rocksdb::DB::Open(options, db_path, &db);
        if (!status.ok()) {
            std::cerr << "[Cache] ⚠️ RocksDB 打開失敗，將以降級模式執行: " << status.ToString() << std::endl;
            db = nullptr;
        } else {
            std::cout << "[Cache] ⚡ RocksDB 快取引擎已啟動" << std::endl;
        }
    }

    ~VideoCache() {
        if (db) {
            delete db;
        }
    }

    // 將路徑、大小、修改時間組成 Unique Key
    std::string build_key(const VideoTask& task) {
        return task.path + ":" + std::to_string(task.size) + ":" + std::to_string(task.mtime);
    }

    bool get(const VideoTask& task, ResultTask& res) {
        if (!db) return false;
        std::string value;
        rocksdb::Status s = db->Get(rocksdb::ReadOptions(), build_key(task), &value);
        if (s.ok()) {
            try {
                std::stringstream ss(value);
                std::string item;
                std::getline(ss, item, '|'); res.hash = std::stoull(item);
                std::getline(ss, item, '|'); res.filesize_mb = std::stoll(item);
                std::getline(ss, item, '|'); res.resolution = item;
                std::getline(ss, item, '|'); res.duration_sec = std::stoi(item);
                std::getline(ss, item, '|'); res.bitrate_kbps = std::stoi(item);
                std::getline(ss, item, '|'); res.match_detail = item;
                std::getline(ss, item, '|'); res.is_poison = (item == "1");
                res.path = task.path;
                return true;
            } catch(...) {
                return false; // 如果解析失敗，視為 Cache Miss
            }
        }
        return false;
    }

    void put(const VideoTask& task, const ResultTask& res) {
        if (!db) return;
        std::string value = 
            std::to_string(res.hash) + "|" +
            std::to_string(res.filesize_mb) + "|" +
            res.resolution + "|" +
            std::to_string(res.duration_sec) + "|" +
            std::to_string(res.bitrate_kbps) + "|" +
            res.match_detail + "|" +
            (res.is_poison ? "1" : "0");
        db->Put(rocksdb::WriteOptions(), build_key(task), value);
    }
};

#else

// 降級版本 (未安裝 RocksDB 時編譯通過)
class VideoCache {
public:
    VideoCache(const std::string& db_path = "") {
        std::cout << "[Cache] ⚠️ 未編譯 RocksDB 支援，停用快取" << std::endl;
    }
    bool get(const VideoTask& task, ResultTask& res) { return false; }
    void put(const VideoTask& task, const ResultTask& res) {}
};

#endif