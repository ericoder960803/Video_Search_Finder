use tauri::{Emitter, Window, AppHandle};
use tauri_plugin_shell::ShellExt;
use std::io::{BufRead, BufReader};
use std::thread;

#[tauri::command]
fn run_dupe_engine(app_handle: AppHandle, window: Window, paths: Vec<String>) {
    let window_log = window.clone();
    let _ = window_log.emit("scan-log", "[GUI] 啟動引擎 (Sidecar Mode)...");

    // 獲取 Sidecar 實例
    let sidecar_command = match app_handle.shell().sidecar("dupe_engine") {
        Ok(cmd) => cmd.args(paths),
        Err(e) => {
            let _ = window_log.emit("scan-log", format!("[Error] 無法初始化 Sidecar: {}", e));
            return;
        }
    };

    thread::spawn(move || {
        let (mut rx, mut child) = match sidecar_command.spawn() {
            Ok(res) => res,
            Err(e) => {
                let _ = window_log.emit("scan-log", format!("[Error] 無法啟動 Sidecar: {}", e));
                return;
            }
        };

        // 監聽輸出
        while let Some(event) = rx.blocking_recv() {
            match event {
                tauri_plugin_shell::process::CommandEvent::Stdout(line) => {
                    let l = String::from_utf8_lossy(&line).trim().to_string();
                    let _ = window_log.emit("scan-log", l);
                }
                tauri_plugin_shell::process::CommandEvent::Stderr(line) => {
                    let l = String::from_utf8_lossy(&line).trim().to_string();
                    let _ = window_log.emit("scan-log", format!("[Error] {}", l));
                }
                tauri_plugin_shell::process::CommandEvent::Terminated(payload) => {
                    let _ = window_log.emit("scan-log", format!("[Main] ✅ 任務結束，狀態碼: {:?}", payload.code));
                    break;
                }
                _ => {}
            }
        }
    });
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_shell::init())
        .invoke_handler(tauri::generate_handler![run_dupe_engine])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}