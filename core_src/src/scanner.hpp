#pragma once
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <atomic>
#include <thread>

// 引入你的任務小紙條定義與無鎖隊列
#include "types.hpp"
#include "readerwriterqueue.h"

namespace fs = std::filesystem;

class Scanner {
public:
    // 構造函數：可以設定想要掃描的副檔名
    Scanner() {
        video_extensions = {".mp4", ".mkv", ".avi", ".mov", ".flv", ".ts"};
    }

    // 核心執行邏輯：給它路徑、任務隊列、以及一個用來標記「我找完了」的信號燈
    void run(const std::string& root_path, 
             moodycamel::ReaderWriterQueue<VideoTask>& queue, 
             std::atomic<bool>& scan_done) 
    {
        std::cout << "[Scanner] 🚀 開始掃描目錄: " << root_path << std::endl;

        try {
            // 使用遞迴遍歷，會進到子資料夾
            for (const auto& entry : fs::recursive_directory_iterator(root_path)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    
                    // 轉小寫檢查，避免 .MP4 漏掉
                    for (auto & c: ext) c = std::tolower(c);

                    if (is_video(ext)) {
                        VideoTask task;
                        task.path = entry.path().string();
                        task.size = entry.file_size();

                        // 無鎖寫入：如果隊列滿了就讓 CPU 喘口氣再試
                        while (!queue.try_enqueue(task)) {
                            std::this_thread::yield();
                        }
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[Scanner] ❌ 檔案系統存取錯誤: " << e.what() << std::endl;
        }

        // 找完了，把燈關掉（設定為 true），讓後面的 Worker 知道不用再等新任務了
        scan_done.store(true);
        std::cout << "[Scanner] ✅ 掃描完畢" << std::endl;
    }

private:
    std::vector<std::string> video_extensions;

    bool is_video(const std::string& ext) {
        for (const auto& v_ext : video_extensions) {
            if (ext == v_ext) return true;
        }
        return false;
    }
};