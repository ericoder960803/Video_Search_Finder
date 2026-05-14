#include "src/scanner.hpp"
#include "src/decoder.hpp"
#include "src/collector.hpp"
#include "src/cache.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <chrono>

int main(int argc, char* argv[]) {
    // 1. 處理輸入路徑
    std::vector<std::string> scan_paths;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            scan_paths.push_back(argv[i]);
        }
    } else {
        scan_paths.push_back("./test_videos"); // 預設路徑
    }

    // ⏱️ 開始計時
    auto start_time = std::chrono::high_resolution_clock::now();
    
    const int worker_count = std::thread::hardware_concurrency();
    std::cout << "[Main] 🚀 啟動多隊列並行模式，Worker 數量: " << worker_count << std::endl;
    std::cout << "[Main] 📂 掃描目標: ";
    for (const auto& p : scan_paths) std::cout << p << " ";
    std::cout << std::endl;

    std::vector<std::unique_ptr<moodycamel::ReaderWriterQueue<VideoTask>>> task_queues;
    std::vector<std::unique_ptr<moodycamel::ReaderWriterQueue<ResultTask>>> res_queues;
    
    for (int i = 0; i < worker_count; ++i) {
        task_queues.push_back(std::make_unique<moodycamel::ReaderWriterQueue<VideoTask>>(2048));
        res_queues.push_back(std::make_unique<moodycamel::ReaderWriterQueue<ResultTask>>(2048));
    }

    std::atomic<bool> scan_done(false);
    std::atomic<bool> workers_done(false);
    std::atomic<int> total_scanned(0);

    std::shared_ptr<VideoCache> cache = std::make_shared<VideoCache>();

    // 2. 啟動 Scanner (支援多路徑)
    std::thread s_thread([&]() {
        std::cout << "[Scanner] 🚀 開始掃描並分流任務..." << std::endl;
        int target_worker = 0;
        try {
            for (const auto& root : scan_paths) {
                if (!std::filesystem::exists(root)) {
                    std::cerr << "[Scanner] ⚠️ 路徑不存在: " << root << std::endl;
                    continue;
                }
                for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        for (auto & c: ext) c = std::tolower(c);
                        
                        if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".mov" || ext == ".flv" || ext == ".ts") {
                            VideoTask task;
                            task.path = entry.path().string();
                            task.size = entry.file_size();
                            
                            auto ftime = entry.last_write_time();
                            task.mtime = std::chrono::duration_cast<std::chrono::seconds>(ftime.time_since_epoch()).count();

                            while (!task_queues[target_worker]->try_enqueue(task)) {
                                std::this_thread::yield();
                            }
                            target_worker = (target_worker + 1) % worker_count;
                            total_scanned++;
                        }
                    }
                }
            }
        } catch (...) {}
        scan_done.store(true);
        std::cout << "[Scanner] ✅ 掃描與分派完畢" << std::endl;
    });

    // 3. 啟動 Decoder Workers
    std::vector<std::thread> w_threads;
    std::vector<std::unique_ptr<VideoDecoder>> decoders;

    for (int i = 0; i < worker_count; ++i) {
        decoders.push_back(std::make_unique<VideoDecoder>(cache));
        w_threads.emplace_back([&, i]() {
            decoders[i]->run(*task_queues[i], *res_queues[i], scan_done);
        });
    }

    // 4. 啟動 Collector
    Collector collector;
    std::thread c_thread([&]() {
        std::cout << "[Collector] 📦 整理員開始輪詢所有結果隊列..." << std::endl;
        ResultTask res;
        while (true) {
            bool found_any = false;
            for (int i = 0; i < worker_count; ++i) {
                if (res_queues[i]->try_dequeue(res)) {
                    collector.hash_groups[res.fingerprint].push_back(res);
                    found_any = true;
                }
            }

            if (!found_any) {
                if (workers_done.load()) {
                    bool still_empty = true;
                    for (int i = 0; i < worker_count; ++i) {
                        if (res_queues[i]->size_approx() > 0) still_empty = false;
                    }
                    if (still_empty) break;
                }
                std::this_thread::yield();
            }
        }
        std::cout << "[Collector] ✅ 所有結果整理完畢。" << std::endl;
    });

    s_thread.join();
    for (auto& t : w_threads) t.join();
    
    workers_done.store(true);
    c_thread.join();

    collector.print_results();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
    double total_seconds = diff.count();

    std::cout << "\n================ 效能分析 ================" << std::endl;
    std::cout << "⏱️ 總耗時:   " << total_seconds << " 秒" << std::endl;
    std::cout << "📁 處理檔案: " << total_scanned.load() << " 個" << std::endl;
    if (total_seconds > 0 && total_scanned.load() > 0) {
        std::cout << "🚀 處理速率: " << (total_scanned.load() / total_seconds) << " files/sec" << std::endl;
    }
    std::cout << "==========================================\n" << std::endl;

    return 0;
}