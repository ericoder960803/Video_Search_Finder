#pragma once
#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <atomic>
#include <iomanip>
#include <thread>

#include "types.hpp"
#include "readerwriterqueue.h"

// 💡 輔助：讓 std::vector<uint64_t> 可以作為 unordered_map 的 Key
struct VectorHash {
    size_t operator()(const std::vector<uint64_t>& v) const {
        size_t seed = v.size();
        for(auto x : v) {
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

class Collector {
public:
    // 帳本：Key 是序列化指紋
    std::unordered_map<std::vector<uint64_t>, std::vector<ResultTask>, VectorHash> hash_groups;

    void run(moodycamel::ReaderWriterQueue<ResultTask>& res_queue, 
             std::atomic<bool>& workers_done) 
    {
        ResultTask res;
        while (true) {
            if (res_queue.try_dequeue(res)) {
                if (!res.fingerprint.empty()) {
                    hash_groups[res.fingerprint].push_back(res);
                }
            } 
            else if (workers_done.load()) {
                if (res_queue.size_approx() == 0) break;
            } 
            else {
                std::this_thread::yield();
            }
        }
    }

    void print_results() {
        std::cout << "\n================ 掃描結果報表 ================" << std::endl;
        int duplicate_count = 0;

        for (auto const& [fp, group] : hash_groups) {
            if (group.size() > 1) {
                duplicate_count++;
                
                // 輸出第一個 Hash 作為代表 ID
                std::cout << "📍 重複組 " << duplicate_count 
                          << " [指紋: " << std::hex << fp[0] << std::dec << "...]" << std::endl;
                
                for (size_t i = 0; i < group.size(); ++i) {
                    const auto& res = group[i];
                    std::cout << "   [" << (i == 0 ? "基底" : "重複") << "] " << res.path << std::endl;
                    std::cout << "          " << res.resolution 
                              << " | " << res.duration_sec << "s"
                              << " | " << res.filesize_mb << "MB"
                              << " | " << res.bitrate_kbps << "kbps"
                              << " | 匹配點: " << res.match_detail << std::endl;
                }
                std::cout << "-------------------------------------------" << std::endl;
            }
        }

        if (duplicate_count == 0) {
            std::cout << "恭喜！沒有發現重複影片。" << std::endl;
        }
        std::cout << "==============================================" << std::endl;
    }
};