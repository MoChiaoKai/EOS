#!/bin/bash
# 腳本名稱: six_client_no_server.sh
# 說明: 使用 tmux 啟動 6 個客戶端面板，自動完成註冊、排程和數據傳輸。
# IMPORTANT: 您必須在執行此腳本前，在另一個終端機中手動啟動伺服器 (./server 8888)。

# --- 配置參數 ---
SESSION="six_client_only"
SERVER_PORT=8888
SERVER_IP="127.0.0.1"
CLIENT_APP="./client"
CLIENT_COUNT=6 

# --- 顏色輸出 ---
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

# =================================================================
# 1. 輔助函式
# =================================================================

# 函式: 計算未來的時間 (HH:MM)
calculate_future_time() {
    local OFFSET=$1
    # 計算當前時間加上 OFFSET 分鐘後的結果 (24小時制 HH:MM)
    date -d "+$OFFSET minutes" +%H:%M
}

# 函式: 檢查並編譯程式
compile_project() {
    echo -e "${GREEN}--- 1. Cleaning and Compiling ---${NC}"
    make clean > /dev/null 2>&1
    make
    if [ $? -ne 0 ]; then
        echo -e "${RED}Error: Compilation failed. Please check your C files.${NC}"
        exit 1
    fi
}

# =================================================================
# 2. 準備工作區塊
# =================================================================

compile_project

# 檢查 tmux 是否已安裝
if ! command -v tmux &> /dev/null; then
    echo -e "${RED}Error: tmux is not installed. Please install it first (e.g., sudo apt install tmux).${NC}"
    exit 1
fi

# 刪除舊的 tmux 會話
tmux has-session -t $SESSION 2>/dev/null
if [[ $? -eq 0 ]]; then
    echo -e "${YELLOW}Killing old tmux session: $SESSION${NC}"
    tmux kill-session -t $SESSION
fi

# =================================================================
# 3. Tmux 介面配置區塊 (6 個客戶端面板)
# =================================================================

# 創建新的 tmux 會話 (Panel 0: Client 1)
tmux new-session -d -s $SESSION

# 垂直分割為上下兩排 (Panel 1: Client 4)
tmux split-window -v -p 50 

# --- 創建頂排的 Panel (Panel 0, 2, 3) ---
tmux select-pane -t 0
tmux split-window -h -p 50 # Panel 2: Client 2
tmux select-pane -t 2
tmux split-window -h -p 50 # Panel 3: Client 3

# --- 創建底排的 Panel (Panel 1, 4, 5) ---
tmux select-pane -t 1
tmux split-window -h -p 50 # Panel 4: Client 5
tmux select-pane -t 4
tmux split-window -h -p 50 # Panel 5: Client 6

tmux select-layout tiled

# =================================================================
# 4. 啟動程式並執行自動排程和傳輸
# =================================================================

echo -e "${GREEN}--- 2. Starting Clients, Auto-Scheduling, and Sending Data ---${NC}"

# 隨機產生 ID 的基準值
BASE_ID=$(shuf -i 1000-5000 -n 1)

# Panel 0-5: 啟動客戶端並發送自動輸入
for i in $(seq 0 $((CLIENT_COUNT - 1))); do
    PANEL_INDEX=$i
    
    # 設置隨機/獨特的 ID
    USER_ID=$((BASE_ID + i))
    ROOM_ID=$((100 + i))
    
    # 計算未來的排程時間 (錯開 i * 2 分鐘，確保時間有效性)
    TARGET_TIME=$(calculate_future_time $((i * 5 + 1)))

    # 根據客戶端編號設置不同的排程裝置組合
    case $i in
        0) SCHEDULED_DEVICES="1";;          # Client 1: Device 1
        1) SCHEDULED_DEVICES="2 3";;        # Client 2: Device 2 and 3
        2) SCHEDULED_DEVICES="1 3";;        # Client 3: Device 1 and 3
        3) SCHEDULED_DEVICES="2";;          # Client 4: Device 2
        4) SCHEDULED_DEVICES="1 2";;        # Client 5: Device 1 and 2
        5) SCHEDULED_DEVICES="1 2 3";;      # Client 6: All devices
    esac

    echo "Client $((i + 1)) (Panel $i, ID: $USER_ID) auto-scheduling for $TARGET_TIME..."
    
    # --- 完整的自動輸入流程 ---
    
    # 1. 啟動客戶端程式
    tmux send-keys -t $PANEL_INDEX "$CLIENT_APP $SERVER_IP $SERVER_PORT" C-m
    
    sleep 0.2
    
    # 2. Startup: 選擇 '1' (Sign up)
    tmux send-keys -t $PANEL_INDEX "1" C-m
    
    # 3. Sign up: User ID
    tmux send-keys -t $PANEL_INDEX "$USER_ID" C-m
    
    # 4. Sign up: Room ID
    tmux send-keys -t $PANEL_INDEX "$ROOM_ID" C-m
    
    # 5. Sign up: Devices (e.g., 1 2 3)
    tmux send-keys -t $PANEL_INDEX "1 2 3" C-m
    
    # (程式已進入 Main Menu)
    
    # 6. Main Menu: 選擇 '2' (Set Estimated Time)
    tmux send-keys -t $PANEL_INDEX "2" C-m
    
    # 7. Set Time: Estimated Arrival time (HH:MM)
    tmux send-keys -t $PANEL_INDEX "$TARGET_TIME" C-m
    
    # 8. Set Time: Scheduled Devices (e.g., 1 3)
    tmux send-keys -t $PANEL_INDEX "$SCHEDULED_DEVICES" C-m
    
    # 9. Press Enter and back to main menu... (處理 getchar() 暫停)
    tmux send-keys -t $PANEL_INDEX C-m
    
    # 10. Main Menu: 選擇 '3' (Send Task Data to Server)
    tmux send-keys -t $PANEL_INDEX "3" C-m
    
    # 11. Main Menu: 選擇 '0' (Exit)
    tmux send-keys -t $PANEL_INDEX "1" C-m
    
    # 延遲，防止發送衝突
    sleep 1.5 
done

# =================================================================
# 5. 附著到 Tmux 會話
# =================================================================

echo "Press Enter to attach to the tmux session (6 client panels)..."
read

# 附著到 tmux 會話
tmux -2 attach-session -t $SESSION