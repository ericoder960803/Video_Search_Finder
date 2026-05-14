#!/bin/bash
set -e

STRESS_DIR="test_videos/stress"
mkdir -p "$STRESS_DIR"
echo "🚀 Generating stress test videos in $STRESS_DIR (using testsrc)..."

# 1. 生成 20 對重複影片 (40 個檔案)
for i in {1..20}; do
    DURATION=$(( (i % 5) + 3 )) # 3~7 秒
    WIDTH=$(( 320 + (i * 2) )) # 確保寬度是 2 的倍數
    BASE_FILE="$STRESS_DIR/base_$i.mp4"
    DUPE_FILE="$STRESS_DIR/dupe_$i.mp4"
    
    ffmpeg -y -f lavfi -i "testsrc=size=${WIDTH}x240:duration=$DURATION:rate=30" -c:v libx264 -pix_fmt yuv420p "$BASE_FILE" -loglevel error
    
    cp "$BASE_FILE" "$DUPE_FILE"
    echo -n "."
done
echo " Done generating 20 pairs."

# 2. 生成毒藥影片
echo "☠️ Generating poison videos..."
touch "$STRESS_DIR/poison_empty.mp4"
head -c 1048576 </dev/urandom >"$STRESS_DIR/poison_garbage.mp4"
ffmpeg -y -f lavfi -i sine=frequency=440:duration=5 -c:a aac "$STRESS_DIR/poison_audio_only.mp4" -loglevel error
cp "$STRESS_DIR/base_1.mp4" "$STRESS_DIR/poison_truncated.mp4"
truncate -s 50k "$STRESS_DIR/poison_truncated.mp4"
cp "$STRESS_DIR/base_2.mp4" "$STRESS_DIR/poison_bad_header.mp4"
printf "THIS IS NOT A VIDEO" | dd of="$STRESS_DIR/poison_bad_header.mp4" bs=1 count=20 conv=notrunc 2>/dev/null

echo "✅ Stress test suite ready!"
ls -lh "$STRESS_DIR" | head -n 10
