#pragma once
#include <string>
#include <cstdint>
#include <vector>

struct VideoTask {
    std::string path;
    uintmax_t size;
    uint64_t mtime; // 修改時間，用作快取判定
};

struct ResultTask {
    // 💡 升級：不再使用單一 64位元 Hash，改用序列化指紋
    std::vector<uint64_t> fingerprint;
    
    // 為了相容性，我們保留一個輔助的 hash 用於快速比對或 RocksDB Key
    uint64_t hash = 0; 

    std::string path;           
    int64_t filesize_mb = 0;    
    std::string resolution;     
    int duration_sec = 0;       
    int bitrate_kbps = 0;       

    std::string match_detail; 
    bool is_poison = false; 
};