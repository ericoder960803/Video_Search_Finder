use tauri::{Emitter, Window};
use std::process::{Command, Stdio};
use std::io::{BufRead, BufReader};
use std::thread;
use std::path::PathBuf;

#[tauri::command]
fn run_dupe_engine(window: Window, paths: Vec<String>) {
    // 優先使用當前目錄下的執行檔，若找不到則提示
    let exe_name = if cfg!(windows) { "dupe_engine.exe" } else { "../../core_src/dupe_engine" };
    let current_dir = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
    let exe_path = current_dir.join(exe_name);

    if !exe_path.exists() {
        let _ = window.emit("scan-log", format!("[Error] 找不到引擎: {:?}。請確保已執行 make", exe_path));
        return;
    }

    let _ = window.emit("scan-log", format!("[GUI] 啟動引擎: {:?}", exe_path));

    thread::spawn(move || {
        let mut child = match Command::new(&exe_path)
            .args(&paths)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn() 
        {
            Ok(c) => c,
            Err(e) => {
                let _ = window.emit("scan-log", format!("[Error] 無法啟動: {}", e));
                return;
            }
        };

        // 建立 stdout 監聽執行緒
        let stdout = child.stdout.take().expect("Failed to open stdout");
        let window_out = window.clone();
        let stdout_handle = thread::spawn(move || {
            let reader = BufReader::new(stdout);
            for line in reader.lines() {
                if let Ok(l) = line {
                    let _ = window_out.emit("scan-log", l);
                }
            }
        });

        // 建立 stderr 監聽執行緒
        let stderr = child.stderr.take().expect("Failed to open stderr");
        let window_err = window.clone();
        let stderr_handle = thread::spawn(move || {
            let reader = BufReader::new(stderr);
            for line in reader.lines() {
                if let Ok(l) = line {
                    let _ = window_err.emit("scan-log", format!("[Error] {}", l));
                }
            }
        });

        // 等待子進程結束，防止殭屍進程
        let status = child.wait().expect("Failed to wait on child");
        
        // 確保輸出的執行緒也都處理完畢
        let _ = stdout_handle.join();
        let _ = stderr_handle.join();

        let _ = window.emit("scan-log", format!("[Main] ✅ 任務結束，狀態碼: {}", status));
    });
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![run_dupe_engine])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}