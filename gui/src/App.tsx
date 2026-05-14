import { useState, useEffect, useRef } from "react";
import { invoke, convertFileSrc } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { open } from "@tauri-apps/plugin-dialog";
import { revealItemInDir } from "@tauri-apps/plugin-opener";
import "./App.css";

interface VideoResult {
  path: string;
  type: "基底" | "重複";
  resolution: string;
  duration: string;
  size: string;
  bitrate: string;
  matchDetail: string;
}

interface DuplicateGroup {
  id: string;
  hash: string;
  files: VideoResult[];
}

const PreviewVideo = ({ path }: { path: string }) => {
  const [isHovered, setIsHovered] = useState(false);
  const videoRef = useRef<HTMLVideoElement>(null);

  // 將路徑轉換為 Tauri 可存取的 URL
  const assetUrl = convertFileSrc(path);

  return (
    <div 
      className="preview-container"
      onMouseEnter={() => setIsHovered(true)}
      onMouseLeave={() => setIsHovered(false)}
    >
      {isHovered ? (
        <video
          ref={videoRef}
          src={assetUrl}
          muted
          loop
          autoPlay
          className="preview-video"
          onLoadedMetadata={() => {
            if (videoRef.current) videoRef.current.currentTime = 5; // 跳過開頭幾秒
          }}
        />
      ) : (
        <div className="preview-placeholder">
          <span style={{fontSize: '1.2rem'}}>🎬</span>
        </div>
      )}
    </div>
  );
};

function App() {
  const [isScanning, setIsScanning] = useState(false);
  const [scanPaths, setScanPaths] = useState<string[]>([]);
  const [logs, setLogs] = useState<string[]>([]);
  const [stats, setStats] = useState({ time: "0", count: "0", rate: "0" });
  const [results, setResults] = useState<DuplicateGroup[]>([]);
  
  const terminalRef = useRef<HTMLDivElement>(null);
  
  // 使用 Ref 儲存中間狀態，避免頻繁觸發渲染
  const parserRef = useRef<{
    currentGroup: DuplicateGroup | null;
    currentFile: VideoResult | null;
  }>({ currentGroup: null, currentFile: null });

  const selectFolders = async () => {
    try {
      const selected = await open({
        directory: true,
        multiple: true,
        title: "選擇資料夾"
      });
      if (selected) {
        const paths = Array.isArray(selected) ? selected : [selected];
        setScanPaths(prev => Array.from(new Set([...prev, ...paths])));
      }
    } catch (e) {
      console.error(e);
    }
  };

  const removePath = (path: string) => {
    setScanPaths(prev => prev.filter(p => p !== path));
  };

  useEffect(() => {
    let unlistenFn: (() => void) | null = null;
    let isMounted = true;

    const init = async () => {
      // 確保只建立一個監聽器，並在元件卸載時清除
      const unlisten = await listen<string>("scan-log", (event) => {
        if (!isMounted) return;
        const line = event.payload;
        if (!line) return;

        setLogs((prev) => [...prev.slice(-100), line]);

        try {
          // 1. 效能指標解析
          if (line.includes("⏱️ 總耗時:")) {
            const m = line.match(/總耗時:\s+([\d.]+)/);
            if (m) setStats(s => ({ ...s, time: m[1] }));
          } else if (line.includes("📁 處理檔案:")) {
            const m = line.match(/處理檔案:\s+(\d+)/);
            if (m) setStats(s => ({ ...s, count: m[1] }));
          } else if (line.includes("🚀 處理速率:")) {
            const m = line.match(/處理速率:\s+([\d.]+)/);
            if (m) setStats(s => ({ ...s, rate: m[1] }));
          }

          // 2. 重複組解析 (狀態機)
          if (line.includes("📍 重複組")) {
            const m = line.match(/📍 重複組\s+(\d+)\s+\[指紋:\s+([0-9a-f]+)/i);
            if (m) {
              // 提交前一組
              if (parserRef.current.currentGroup && parserRef.current.currentGroup.files.length > 0) {
                const finished = { ...parserRef.current.currentGroup };
                setResults(prev => {
                  if (prev.find(g => g.id === finished.id)) return prev;
                  return [...prev, finished];
                });
              }
              parserRef.current.currentGroup = { id: m[1], hash: m[2], files: [] };
            }
          } 
          else if (line.includes("[基底]") || line.includes("[重複]")) {
            const m = line.match(/\[(基底|重複)\]\s+(.*)/);
            if (m && parserRef.current.currentGroup) {
              parserRef.current.currentFile = {
                type: m[1] as any,
                path: m[2].trim(),
                resolution: "-", duration: "-", size: "-", bitrate: "-", matchDetail: "-"
              };
            }
          }
          else if (line.includes("|") && line.includes("匹配點:") && parserRef.current.currentFile) {
            const parts = line.split("|").map(p => p.trim());
            const file = parserRef.current.currentFile;
            if (parts.length >= 5) {
              file.resolution = parts[0] || "-";
              file.duration = parts[1] || "-";
              file.size = parts[2] || "-";
              file.bitrate = parts[3] || "-";
              
              // 解析時間點，提取所有 (xx.xxs) 格式的內容
              const rawDetail = parts[4].replace("匹配點:", "").trim();
              const timeMatches = rawDetail.match(/\(([\d.]+s)\)/g);
              if (timeMatches) {
                // 儲存所有時間點，但在 UI 顯示時我們只取第一個作為 "Initial Time"
                file.matchDetail = timeMatches.map(tm => tm.replace(/[()]/g, "")).join(", ");
              } else {
                file.matchDetail = rawDetail;
              }
              
              if (parserRef.current.currentGroup) {
                parserRef.current.currentGroup.files.push({ ...file });
              }
              parserRef.current.currentFile = null;
            }
          }
          else if (line.includes("-------------------------------------------")) {
            if (parserRef.current.currentGroup && parserRef.current.currentGroup.files.length > 0) {
              const finished = { ...parserRef.current.currentGroup };
              setResults(prev => {
                const existingIdx = prev.findIndex(g => g.id === finished.id);
                if (existingIdx >= 0) {
                  const updated = [...prev];
                  updated[existingIdx] = finished;
                  return updated;
                }
                return [...prev, finished];
              });
            }
          }
          else if (line.includes("✅ 任務結束")) {
            // 這是 Rust 發出的最後訊號，代表進程完全結束
            if (parserRef.current.currentGroup && parserRef.current.currentGroup.files.length > 0) {
              const finished = { ...parserRef.current.currentGroup };
              setResults(prev => {
                if (prev.find(g => g.id === finished.id)) return prev;
                return [...prev, finished];
              });
            }
            setIsScanning(false);
            parserRef.current.currentGroup = null;
          }
        } catch (err) {
          console.warn("Parse error:", err);
        }
      });

      if (!isMounted) {
        unlisten();
      } else {
        unlistenFn = unlisten;
      }
    };

    init();
    return () => { 
      isMounted = false;
      if (unlistenFn) unlistenFn(); 
    };
  }, []);

  useEffect(() => {
    if (terminalRef.current) terminalRef.current.scrollTop = terminalRef.current.scrollHeight;
  }, [logs]);

  const startScan = async () => {
    if (scanPaths.length === 0 || isScanning) return;
    setLogs(["[GUI] Requesting scan start..."]);
    setResults([]);
    setStats({ time: "0", count: "0", rate: "0" });
    parserRef.current = { currentGroup: null, currentFile: null };
    setIsScanning(true);
    try {
      await invoke("run_dupe_engine", { paths: scanPaths });
    } catch (e) {
      setLogs(prev => [...prev, `[Fatal Error] Backend call failed: ${e}`]);
      setIsScanning(false);
    }
  };

  // 輔助函式：取得該組的基底檔案時間點
  const getBaseMatch = (group: DuplicateGroup, file: VideoResult) => {
    // 取得該檔案的第一個時間點 (Initial Time)
    const getFirstTime = (detail: string) => detail.split(',')[0].trim();
    
    if (file.type === "基底") {
      return `Start: ${getFirstTime(file.matchDetail)}`;
    }
    
    const baseFile = group.files.find(f => f.type === "基底");
    if (baseFile) {
      return `Matched Base @ ${getFirstTime(baseFile.matchDetail)}`;
    }
    return `Start: ${getFirstTime(file.matchDetail)}`;
  };

  return (
    <div className="app-container">
      <aside className="sidebar">
        <h1>Video Search</h1>
        
        <div className="stats-card">
          <div className="stat-label">TARGETS</div>
          <div className="path-list">
            {scanPaths.map((p, i) => (
              <div key={i} className="path-tag">
                <span className="path-text">{p.split('/').pop() || p}</span>
                <button className="remove-path" onClick={() => removePath(p)}>×</button>
              </div>
            ))}
            <button className="btn-secondary" onClick={selectFolders} style={{ width: '100%', marginTop: '8px' }}>
              + Add Folders
            </button>
          </div>
        </div>

        <div className="stats-card">
          <div className="stat-label">STATUS</div>
          <div className="stat-value" style={{ color: isScanning ? "#f59e0b" : "#10b981" }}>
            {isScanning ? "SCANNING" : "IDLE"}
          </div>
        </div>
        
        <button className="btn-primary" onClick={startScan} disabled={isScanning || scanPaths.length === 0}>
          {isScanning ? "BUSY..." : "START SCAN"}
        </button>

        <div className="stats-card">
          <div className="stat-label">FILES</div>
          <div className="stat-value">{stats.count}</div>
        </div>
        <div className="stats-card">
          <div className="stat-label">SPEED</div>
          <div className="stat-value">{stats.rate} <small style={{fontSize: '0.6rem'}}>f/s</small></div>
        </div>
      </aside>

      <main className="main-content">
        <div className="results-container">
          {results.length === 0 && !isScanning && (
            <div style={{ textAlign: "center", color: "#64748b", marginTop: "100px" }}>
              {scanPaths.length === 0 ? "Add folders to start." : "Ready."}
            </div>
          )}
          {results.map((group) => (
            <div key={group.id} className="duplicate-group">
              <div className="group-header">
                <span className="group-title">Group #{group.id}</span>
                <code style={{ fontSize: "0.6rem", color: "#64748b" }}>{group.hash}</code>
              </div>
              {group.files.map((file, idx) => (
                <div key={idx} className="file-entry">
                  <PreviewVideo path={file.path} />
                  <div className="file-info-container">
                    <div 
                      className="file-path" 
                      onClick={() => revealItemInDir(file.path)}
                      title="點擊在檔案管理員中顯示"
                      style={{ cursor: 'pointer' }}
                    >
                      <span className={`tag ${file.type === "基底" ? "tag-base" : ""}`}>{file.type}</span>
                      {file.path}
                    </div>
                    <div className="file-meta">
                      {file.resolution} • {file.duration}s • {file.size}MB • {file.bitrate}kbps • {getBaseMatch(group, file)}
                    </div>
                  </div>
                </div>
              ))}
            </div>
          ))}
          {isScanning && <div style={{ textAlign: "center", padding: "20px", color: "#38bdf8" }}>Scanning...</div>}
        </div>

        <div className="terminal" ref={terminalRef}>
          {logs.map((log, i) => <div key={i} className="terminal-line">{log}</div>)}
        </div>
      </main>
    </div>
  );
}

export default App;
