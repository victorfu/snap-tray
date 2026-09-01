# SnapTray 全庫 Code Review 修正追蹤

- 審查日期：2026-09-01
- 審查基準：main @ 82611586a549f4ada3c7131b92854ebaddcaf0e1
- 審查模式：唯讀、Double Confirm、全庫掃描
- 目前結論：37 項 Confirmed Issue、2 項 Potential Issue

## 使用方式

這份文件是修正進度的唯一追蹤表。每次處理問題時：

1. 更新總表狀態。
2. 在該項目的「修正證據」補上 commit／PR、測試名稱與平台驗證結果。
3. 只有符合「完成條件」並通過相關回歸測試，才能標為 Verified。
4. 不刪除已否決、重複或不修的項目；改用 Rejected、Duplicate 或 Won't Fix，並記錄理由。

狀態定義：

- Open：尚未開始。
- In Progress：已有候選修正，但尚未完成驗證。
- Fix Ready：修正已完成，等待完整驗證或跨平台驗證。
- Verified：完成條件與回歸測試皆通過。
- Blocked：被環境、平台或外部條件阻擋。
- Potential：仍需指定的 runtime／產品契約確認。

優先度定義：

- P0：Release blocker、資料遺失、隱私遮罩失效或 crash／UAF。
- P1：主要功能錯誤、資源耗盡、嚴重 UI／輸出不一致。
- P2：範圍較窄但可達的功能錯誤。

> 行號以審查基準為主；後續修改可能使行號漂移，函式／類別名稱才是長期定位依據。

## 進度摘要

| 類型 | 數量 |
|---|---:|
| Confirmed / Open | 35 |
| Confirmed / In Progress | 2 |
| Confirmed / Verified | 0 |
| Potential / 待確認 | 2 |

建立本文件時，工作樹已存在兩組未提交候選修正：

- REV-004：Settings test isolation，涉及 include/settings/Settings.h、tests/CMakeLists.txt、tests/TestSettingsIsolation.cpp 等。
- REV-009：Screen Canvas text interaction cache，涉及 src/ScreenCanvasSession.cpp、src/region/TextAnnotationEditor.cpp 與相關測試。

上述變更尚未在本文件建立時完成獨立驗證，因此只能標為 In Progress，不能視為已修復。其餘既有工作樹變更不由本文件認領。

## Confirmed Issue 總表

| ID | 狀態 | 優先度 | 信心 | 平台／區域 | 摘要 |
|---|---|---:|---|---|---|
| REV-001 | Open | P0 | High | macOS Release | 官方 DMG 宣稱 macOS 14，相依 Qt payload 實際要求 macOS 26 |
| REV-002 | Open | P0 | High | Mosaic / HiDPI | Gaussian 自動遮罩只覆蓋部分實體像素 |
| REV-003 | Open | P0 | High | Windows OCR | 未遵守 OcrEngine MaxImageDimension |
| REV-004 | In Progress | P0 | High | Tests / Settings | 測試會刪寫真實 SnapTray 設定 |
| REV-005 | Open | P0 | High | Save / Concurrency | 唯一檔名存在 TOCTOU，可靜默覆寫 |
| REV-006 | Open | P1 | High | Windows Capture UI | Annotation cache 無上限成長，可耗盡記憶體 |
| REV-007 | Open | P0 | High | Windows Video | 強制 terminate 並刪除 reader thread，可 crash／UAF |
| REV-008 | Open | P1 | High | macOS Recording | 首次麥克風授權阻塞主執行緒並破壞時間軸 |
| REV-009 | In Progress | P1 | High | Screen Canvas | 文字拖移／旋轉／縮放會重用舊快取 |
| REV-010 | Open | P1 | High | Region Selection | 建立與一般 resize 沒有 clamp 到 bounds |
| REV-011 | Open | P1 | High | Region Selection | mouse release 忽略最後座標 |
| REV-012 | Open | P1 | High | Eraser | 只在離散事件點擦除，快速拖曳會留下間隙 |
| REV-013 | Open | P2 | High | Arrow | 端點曲線結果依 mouse event 分割方式而變 |
| REV-014 | Open | P1 | High | Arrow / Polyline | 寬箭頭超出 bounding／hit geometry |
| REV-015 | Open | P1 | High | Polyline | 短末段把箭頭畫在倒數頂點，箭頭後仍有尾巴 |
| REV-016 | Open | P2 | High | Gizmo | 小物件的 handle hit zones 重疊，部分 handle 不可達 |
| REV-017 | Open | P1 | High | Text | wrapText 改變空白且不支援 CJK 字元換行 |
| REV-018 | Open | P1 | High | Save Metadata | detected-window metadata 在儲存前被清除 |
| REV-019 | Open | P1 | High | History | 跨螢幕保留選取後按 Enter 不寫入 History |
| REV-020 | Open | P2 | High | Screen Canvas | 自訂顏色沒有完整同步與持久化 |
| REV-021 | Open | P1 | High | CLI Pin | file pin 略過 EXIF transform 與大圖 auto-fit |
| REV-022 | Open | P2 | High | CLI Pin | clipboard pin 忽略 x／y |
| REV-023 | Open | P2 | High | CLI GUI | 非數字 delay 被接受為 0 |
| REV-024 | Open | P2 | High | CLI Full | 負數 screen 被當成未指定 |
| REV-025 | Open | P1 | High | Linux Save | filename 長度用 UTF-16 units 而非 UTF-8 bytes |
| REV-026 | Open | P1 | High | Linux Runtime | XDG session 與 Qt QPA 衝突時錯判為 X11 |
| REV-027 | Open | P2 | High | QML | CursorTokens 未註冊 singleton |
| REV-028 | Open | P2 | High | Settings / CLI | install／uninstall 失敗後 busy 永久不解除 |
| REV-029 | Open | P2 | High | Color Picker | triangle value 軸使用 height 而非 width |
| REV-030 | Open | P1 | High | QML Dialog | setModal(true) 實際仍為 NonModal |
| REV-031 | Open | P2 | High | Pin Info | 顯示新值但單項 Copy 複製舊值 |
| REV-032 | Open | P1 | High | macOS Recording | SCK 未排除錄影 tooltip window |
| REV-033 | Open | P1 | High | Windows Recording | Windows 10 2004 前 exclusion 退化成黑色矩形 |
| REV-034 | Open | P1 | High | Recording Audio | encoder 靜默降級無音訊，呼叫端未察覺 |
| REV-035 | Open | P1 | High | Windows Recording | DXGI worker 固定 30 fps，忽略使用者 frame rate |
| REV-036 | Open | P1 | High | macOS Recording | 麥克風中途斷線／session runtime error 無監聽 |
| REV-037 | Open | P2 | High | Region Toolbar | StepBadge／Mosaic toggle-off 未同步 ToolManager |

## 詳細問題與完成條件

### REV-001 — 官方 macOS DMG 的實際最低版本高於宣告

- 狀態：Open
- 證據：CMakeLists.txt:3-7；.github/workflows/release.yml:107-125,272-282,308-316；packaging/macos/package.sh:126-185；cmake/Info.plist.in:50-51；docs/_data/i18n/en.yml:77-80,101-104。
- 觸發：release 使用浮動 macos-latest 與 brew install qt@6，macdeployqt 將較高 deployment target 的預編譯 Qt framework 打入 DMG。
- 後果：v1.0.62 的 Info.plist、Sparkle appcast 與網站宣稱 macOS 14，但抽查官方 DMG 的必要 Qt frameworks／plugins 為 minos 26.0；macOS 14／15 使用者會收到看似相容、實際無法啟動的更新。
- 完成條件：固定 runner 與 Qt；打包後遞迴檢查所有 Mach-O 的 LC_BUILD_VERSION；實際最大 minos 與 Info.plist、appcast、網站一致；在乾淨 macOS 14 完成安裝、啟動、Region、Settings/QML 與 updater smoke。
- 修正證據：待補。

### REV-002 — HiDPI Gaussian 自動遮罩可能只覆蓋左上區域

- 狀態：Open
- 證據：src/annotations/MosaicRectAnnotation.cpp:172-234,237-274。
- 觸發：來源 QPixmap 的 DPR 大於 1，MosaicRectAnnotation 使用 Gaussian；來源 QImage 保留 DPR，但 resultImage 是 DPR 1，drawImage 會按 device-independent size 縮小繪製。
- 後果：resultImage 剩餘區域保持透明；套回原圖後部分敏感內容未被 Gaussian 遮罩，屬隱私風險。Pixelate 走直接像素填入，不是同一問題。
- 完成條件：以 DPR 2／1.5、四象限高對比來源測試 Gaussian rect，斷言整個目標 rect 每個實體像素皆被處理；同時驗證 Pin／Region credential auto-blur。
- 修正證據：待補。

### REV-003 — Windows OCR 未處理 MaxImageDimension

- 狀態：Open
- 證據：src/OCRManager_win.cpp:91-120,173-176；src/PinWindow.cpp:1910-1915,5027-5045。
- 觸發：輸入任一實體像素軸超過 OcrEngine::MaxImageDimension，例如 4K／5K 或 HiDPI 大範圍截圖。
- 後果：手動 OCR 失敗；credential auto-blur 無法得到 credential regions，敏感文字保持未遮蔽。
- 完成條件：Windows 以 MaxImageDimension、MaxImageDimension + 1 與大型雙軸圖片驗證；若縮放或分塊，OCR bounding boxes 必須正確映回原圖；覆蓋 Pin／Region OCR 與 credential auto-blur。
- 修正證據：待補。

### REV-004 — 測試會修改真實 SnapTray settings

- 狀態：In Progress（目前工作樹有未提交候選修正）
- 證據：tests/App/tst_MainApplicationTrayMenu.cpp:143-164,359-384；tests/Settings/tst_AnnotationSettingsManager.cpp:84-110；tests/Settings/tst_PinWindowSettingsManager.cpp:68-87；tests/Settings/tst_RegionCaptureSettingsManager.cpp:28-44；tests/Update/tst_UpdateCoordinator.cpp:64-71,95-108；tests/Update/tst_UpdateSettingsManager.cpp:43-60；include/settings/Settings.h:571-581。
- 觸發：執行上述 test binaries 或 canonical test suite。
- 後果：tests 直接 remove／write 真實 namespace。Debug 測試可刪除 SnapTray-Debug 的 hotkey、annotation、pin、region、update 偏好；Release-config test 可能污染 production namespace。
- 實際副作用：本次 Debug 驗證已執行相關 suites；若原本存在這些 Debug keys，可能已被刪除。事前沒有 snapshot，不能安全自動復原；production release namespace 未受這次 Debug build 影響。
- 完成條件：每個 test process 在 main 前切到獨立暫存 store；以 sentinel 驗證正常結束與異常終止都不改變真實 key 的 existence、type、value；全套測試通過。
- 修正證據：候選變更位於 include/settings/Settings.h、tests/CMakeLists.txt、tests/TestSettingsIsolation.cpp 與 tests/Settings/tst_SettingsStorageLocation.cpp；尚待驗證。

### REV-005 — 唯一檔名配置存在 TOCTOU

- 狀態：Open
- 證據：src/utils/FilenameTemplateEngine.cpp:258-308；src/region/RegionExportManager.cpp:200-205,246-295；src/cli/CaptureOutputHelper.cpp:73-74,112-120；src/PinWindow.cpp:1648-1663,6049-6070；src/utils/ImageSaveUtils.cpp:75-103。
- 觸發：兩個 producer 在同一 output directory、timestamp 與 filename context 下並行儲存，且都在任一方 commit 前完成 QFile::exists 檢查。
- 後果：兩個操作都可能回報成功，但後完成的 QSaveFile::commit 取代先前檔案，只剩一張，造成靜默資料遺失。
- 完成條件：以 barrier 同步兩個 producer 和兩個 process；必須取得不同保留路徑並保留兩份不同內容。Recording 既有 collision retry 不得退化。
- 修正證據：待補。

### REV-006 — CaptureChrome annotation cache 無上限成長

- 狀態：Open
- 證據：include/annotations/AnnotationLayer.h:218-249；src/AnnotationLayer.cpp:1019-1054,1075-1130；src/region/CaptureChromeWindow.cpp:386-392；src/region/RegionPainter.cpp:592-623。
- 觸發：Windows capture overlay 已有 annotation，使用者反覆移動／resize selection，使 annotation viewport origin／size 持續產生新 CacheKey，而 layer revision 沒有改變。
- 後果：m_annotationCaches 為無上限 map，每個 key 保留一張高 DPI QPixmap；長時間操作會持續增加 RAM／圖形資源，最終可能 OOM 或使程式／系統不穩定。
- 完成條件：對數千個不同 viewport 做壓力測試，cache entries 與 retained bytes 必須有明確上限；畫面與 full repaint pixel parity 一致。
- 修正證據：待補。

### REV-007 — Media Foundation player 強制終止 reader thread

- 狀態：Open
- 證據：src/video/MediaFoundationPlayer_win.cpp:532-556。
- 觸發：close／reload 時 reader thread 在一秒內未退出。
- 後果：程式呼叫 QThread::terminate，再立即 wait／delete；thread 可能在持有 mutex、執行 COM 或存取 player state 時被終止，造成 deadlock、heap corruption、UAF 或 crash。
- 完成條件：移除強制 terminate 路徑；以可取消的 blocking read／cooperative shutdown 完成；反覆 load、seek、close 與損壞檔案壓力測試不 hang、不 crash，並通過 sanitizer／Application Verifier。
- 修正證據：待補。

### REV-008 — 首次麥克風授權會凍結 UI 並錯置錄影時間軸

- 狀態：Open
- 證據：src/capture/CoreAudioCaptureEngine_mac.mm:766-806；src/RecordingManager.cpp:953-985,1057-1075。
- 觸發：macOS 麥克風權限為 NotDetermined，倒數結束後開始錄影。
- 後果：主執行緒在 dispatch_semaphore_wait(DISPATCH_TIME_FOREVER) 等授權；RecordingManager 已先啟動 elapsed timer。使用者停留在系統 prompt 的時間會被算入第一個 video timestamp，而 audio timeline 從授權後的零點開始，造成 UI 凍結、開頭長空洞或 A/V 錯位。
- 完成條件：授權流程不得阻塞主執行緒；只有音訊與影像皆準備好後才建立共同 epoch／進入 Recording；以延遲 5–10 秒授權、拒絕與允許三種情境驗證。
- 修正證據：待補。

### REV-009 — Screen Canvas 文字 transform 重用舊 annotation cache

- 狀態：In Progress（目前工作樹有未提交候選修正）
- 證據：src/ScreenCanvasSession.cpp 的 drawAnnotations；src/region/TextAnnotationEditor.cpp 的 start／update／finish dragging/transform；tests/ScreenCanvas/tst_AnnotationRenderHelper.cpp。
- 觸發：選取文字後拖移、旋轉或縮放；文字 transform 在 interaction 期間不增加 layer revision，舊邏輯也未把 text editor 視為 active interaction。
- 後果：draw path 可重用含舊文字位置／transform 的快取，畫面出現殘影、舊位置或操作結束後仍顯示 stale content。
- 完成條件：position／rotation／scale 三種 interaction 的 live frame 與明確 dirty-path frame pixel-equal；release 後 cache revision 更新；Screen Canvas、Region、Pin 均無回歸。
- 修正證據：目前候選變更涉及 include/ScreenCanvasSession.h、src/ScreenCanvasSession.cpp、src/region/TextAnnotationEditor.cpp 與相關 tests；尚待驗證。

### REV-010 — selection create／一般 resize 沒有 clamp 到 bounds

- 狀態：Open
- 證據：src/region/SelectionStateManager.cpp:94-129,203-373；相對地 updateMove／setFromDetectedWindow 會在 436-468 呼叫 clampToBounds。
- 觸發：拖曳到 capture widget 外建立 selection，或用未鎖比例的 handle resize 超出四個邊界。
- 後果：selection rect 可大於可擷取畫布；後續實體 copy 會被 intersect，但 UI dimension、toolbar placement、history／metadata 使用未截斷 rect，產生尺寸與實際輸出不一致或控制項跑出畫面。
- 完成條件：create 與八個 resize handle 在四邊越界時都維持於 bounds；鎖比例與最小尺寸契約不變；export／history rect 與實際 pixels 一致。
- 修正證據：待補。

### REV-011 — selection release 忽略最後座標

- 狀態：Open
- 證據：src/region/RegionInputHandler.cpp:385-445,1284-1344；handleSelectionRelease 對 pos 使用 Q_UNUSED。
- 觸發：最後一個 mouse move 被 coalesce／漏送，release 位置不同於上一個 move；小拖曳尤其容易發生。
- 後果：selection 終點停在舊位置；本應成立的 selection 可能被判成小點擊並改為 detected window 或 full-screen selection。
- 完成條件：release 前必須套用最後座標到 selecting／resizing／moving 狀態；加入「只有 press + release、沒有中間 move」及 coalesced final move 測試。
- 修正證據：待補。

### REV-012 — Eraser 快速拖曳會漏擦

- 狀態：Open
- 證據：src/tools/handlers/EraserToolHandler.cpp:33-55,119-130。
- 觸發：相鄰 mouse move event 的距離大於 eraser diameter，且中間有 annotation。
- 後果：eraseAt 只查詢離散 sample 圓／方位，沒有沿 m_lastPoint 到 pos 插值；使用者看到連續拖曳，但中間物件仍保留。
- 完成條件：按 brush 半徑或更小步距沿 segment 掃描；不同事件密度得到相同擦除結果；一個 gesture 仍只產生一個 undo command。
- 修正證據：待補。

### REV-013 — Arrow endpoint curve 依事件取樣而變

- 狀態：Open
- 證據：src/annotations/ArrowAnnotation.cpp:262-280；src/region/RegionInputHandler.cpp:1572-1582；src/PinWindow.cpp:5593-5603；src/ScreenCanvasSession.cpp:2608-2615。
- 觸發：拖動既有 curved arrow 的 start／end；總位移被拆成多個含奇數像素的 move delta。
- 後果：每次 setStart／setEnd 都以 QPoint 的 delta / 2 更新 control point並截斷。相同起終點若由不同事件序列到達，最終 control point 與曲線形狀不同。
- 完成條件：以 drag-start snapshot 計算一次絕對結果或使用無截斷浮點；一大步與多小步的最終 start/end/control 必須完全一致。
- 修正證據：待補。

### REV-014 — 寬箭頭的視覺超出 bounding／hit geometry

- 狀態：Open
- 證據：src/annotations/ArrowAnnotation.cpp:154-224,227-241；src/annotations/PolylineAnnotation.cpp:140-226,260-280。
- 觸發：使用較大 line width；arrowhead length 為 max(10, width × 3)，但 Arrow bounding 固定 margin 20，Polyline margin 也遠小於 3 × width；containsPoint 只 stroke 主路徑。
- 後果：arrowhead 可在 dirty repaint／cache／export 中被裁切，且點擊可見 arrowhead 可能無法選取 annotation。
- 完成條件：bounding rect 與 hit path 必須包含各種 line-end style 的實際 arrowhead geometry；寬度最小／最大與各角度做 pixel bounds、dirty repaint、selection 測試。
- 修正證據：待補。

### REV-015 — Polyline 短末段把箭頭放在倒數頂點

- 狀態：Open
- 證據：src/annotations/PolylineAnnotation.cpp:64-135。
- 觸發：polyline 至少三點，最後一段短於 arrowLength。
- 後果：arrowTipIdx 改為倒數頂點，但 path loop 仍繼續畫到真正末點；結果是箭頭出現在中途，箭頭尖端後還有一段線尾，與 end-arrow 語意相反。
- 完成條件：不論末段長度，end arrow 必須位於視覺終點且 shaft 正確縮短；涵蓋零長度、短折線、急角與各 arrow style。
- 修正證據：待補。

### REV-016 — 小 annotation 的 gizmo handles 可能不可達

- 狀態：Open
- 證據：include/TransformationGizmo.h:48-56；src/TransformationGizmo.cpp:119-153,224-258,306-337,394-430,452-472。
- 觸發：文字、emoji、shape、arrow 或相鄰 polyline vertices 的 handle centers 距離小於兩個 hit radius 總和。
- 後果：hit zones 重疊，固定的檢查順序永遠回傳前一個 handle；後面的 corner／endpoint／vertex 即使可見也選不到。
- 完成條件：重疊時依最近距離與穩定 tie-break 選擇；對小尺寸、重合 endpoints／vertices 與旋轉後物件做 data-driven hit test。
- 修正證據：待補。

### REV-017 — TextBox wrapText 破壞空白且無法換行 CJK

- 狀態：Open
- 證據：src/annotations/TextBoxAnnotation.cpp:40-87,153-183。
- 觸發：文字含連續／前後空格，或沒有 ASCII space 的長 CJK／日文／泰文段落。
- 後果：split(' ') 後略過 empty token，會正規化原文空白；無空格長字串整段當成單一 word 並允許超寬，最終被 box 裁切。
- 完成條件：rendered text 保留原始 whitespace 語意；CJK 可在 grapheme boundary 換行且不切斷 surrogate／combining sequence；加入 mixed-script 與多空格 pixel／layout 測試。
- 修正證據：待補。

### REV-018 — Detected-window metadata 在儲存前被清除

- 狀態：Open
- 證據：src/region/RegionInputHandler.cpp:1284-1344；src/RegionSelector.cpp:1005-1011,4101-4116。
- 觸發：點擊 detected window 建立選取，之後按 Save／auto-save。
- 後果：selection release 呼叫 clearDetectionAndNotify，RegionSelector connection 同時 reset m_detectedWindow；saveToFile 因此把 windowTitle／ownerApp 設成空字串。使用 window／app token 的 filename 與 metadata 遺失。
- 完成條件：完成 detected-window selection 時保存 metadata snapshot，直到 selection 被替換／取消；Save、Copy history、quick pin 各入口使用一致 snapshot。
- 修正證據：待補。

### REV-019 — 跨螢幕保留 selection 後按 Enter 不寫 History

- 狀態：Open
- 證據：src/RegionSelector.cpp:2111-2156,4461-4470；一般 finishSelection／history submission 位於 1838-1892。
- 觸發：完成單一 selection 後把游標切到另一螢幕，selection 被保存成 m_preservedSelectionPixmap；此時按 Enter。
- 後果：finishPreservedSelection 只 emit regionSelected 並 close，繞過正常 pending history submission；截圖可成功交付，但 History 缺少這筆紀錄。
- 完成條件：preserved path 與一般 finish path 使用同一個 history contract；驗證圖片、global rect、DPR、annotations 與 history entry 完整一致。
- 修正證據：待補。

### REV-020 — Screen Canvas 自訂顏色沒有完整同步／保存

- 狀態：Open
- 證據：src/ScreenCanvasSession.cpp:2016-2053。
- 觸發：在 Screen Canvas 的 More Colors 選擇自訂顏色。
- 後果：preset color 走 onColorSelected，會同步 ToolManager、laser、inline editors、ViewModel 並 saveColor；custom callback 只更新 ToolManager 與 ViewModel。Laser／現有 inline editor 仍用舊色，重開後也回到舊設定。
- 完成條件：所有 color entry 共用同一同步與持久化函式；驗證 pencil／text／laser、所有 surface、重開 session 後顏色一致。
- 修正證據：待補。

### REV-021 — CLI file pin 略過正常圖片載入契約

- 狀態：Open
- 證據：src/MainApplication.cpp:183-216；正常路徑位於 616-629,659-698；src/PinWindow.cpp:519-572；src/pinwindow/PinWindowPlacement.cpp:57-98。
- 觸發：snaptray pin --file 載入含 EXIF rotation／mirror 的 JPEG，或尺寸遠大於螢幕的圖片。
- 後果：CLI 直接 QImage(filePath) 並自行置中，未 setAutoTransform、display conversion 或 computeInitialPinWindowPlacement；圖片方向錯誤，或以原尺寸大幅超出螢幕。
- 完成條件：CLI 與 GUI Pin from Image 共用 loader／placement；EXIF 6／8／mirrored fixtures pixels 一致；超大圖符合既有 90% available-geometry auto-fit。
- 修正證據：待補。

### REV-022 — CLI clipboard pin 忽略 x／y

- 狀態：Open
- 證據：src/cli/commands/PinCommand.cpp:81-112；src/MainApplication.cpp:179-218,849-901。
- 觸發：snaptray pin --clipboard -x 200 -y 120。
- 後果：IPC message 有 x／y，但 clipboard branch 呼叫無座標的 onPasteFromClipboard；Pin 固定在游標螢幕中央，CLI 仍回報成功。
- 完成條件：同時提供 x/y 時 top-left 精確符合；皆未提供或只提供一個時維持既有置中契約。
- 修正證據：待補。

### REV-023 — gui --delay 非數字被接受

- 狀態：Open
- 證據：src/cli/commands/GuiCommand.cpp:10-29；src/cli/CLIHandler.cpp:98-118；src/MainApplication.cpp:167-174。
- 觸發：已有主程式時執行 snaptray gui --delay abc 或 overflow 數字。
- 後果：preflight 未驗證；QString::toInt 未檢查 ok，失敗得到 0。非法命令回傳成功並立即啟動 Region Capture。
- 完成條件：非法、空值、overflow 回 InvalidArguments 且不發 IPC／不啟動 capture；0 與合法正整數維持契約。
- 修正證據：待補。

### REV-024 — full --screen 負數被當成未指定

- 狀態：Open
- 證據：src/cli/commands/FullCommand.cpp:43-79。
- 觸發：snaptray full --screen=-1 或其他可解析的負數。
- 後果：負數成功轉成 int，之後因 screenNum >= 0 不成立而落入游標螢幕 fallback；明確的非法輸入卻成功擷取另一個螢幕。
- 完成條件：任何提供但小於 0 的 screen 都回 InvalidArguments；只有完全未提供 option 才可 fallback。
- 修正證據：待補。

### REV-025 — Linux filename 限制使用字元數而非 bytes

- 狀態：Open
- 證據：src/utils/FilenameTemplateEngine.cpp:311-339,360-389；src/region/RegionExportManager.cpp:191-198；src/utils/ImageSaveUtils.cpp:75-84。
- 觸發：ext4 等 NAME_MAX 以 bytes 計算的檔案系統，template 含長 CJK、emoji 或多 byte Unicode metadata。
- 後果：QString::length／left 判定少於 255，但 UTF-8 component 超過 255 bytes；QSaveFile::open 以 ENAMETOOLONG 失敗，auto-save 無輸出。
- 完成條件：Ubuntu 22.04 實際寫入 CJK、emoji、mixed ASCII；basename.toUtf8().size 不超過目標限制，保留 extension／collision suffix／hash，不切斷 grapheme。
- 修正證據：待補。

### REV-026 — Linux display-server 衝突時錯判 X11

- 狀態：Open
- 證據：src/platform/PlatformCapabilities.cpp:21-69,92-105；src/main.cpp:92-96,144-147。
- 觸發：XDG_SESSION_TYPE=wayland 但 Qt QPA=xcb，或 XDG_SESSION_TYPE=x11 但 QPA=offscreen／minimal。
- 後果：判斷以 OR 且先回傳 X11；runtime guard 錯誤放行並啟用只支援 Ubuntu 22.04 X11 的 capture、global hotkey、window detection。
- 完成條件：建立 session/QPA 決策矩陣並 fail closed；x11+xcb 才 supported，wayland、offscreen、minimal 與衝突訊號不得啟用 X11-only capabilities。
- 修正證據：待補。

### REV-027 — CursorTokens 未註冊為 QML singleton

- 狀態：Open
- 證據：CMakeLists.txt:910-923；src/qml/tokens/CursorTokens.qml:1-13；代表性引用 src/qml/controls/SettingsButton.qml:64-69。
- 觸發：建立任何引用 CursorTokens.* 的 SnapTrayQml 元件；目前約 41 個 references。
- 後果：runtime 可出現 ReferenceError；AOT 路徑會退回預設 Arrow，enabled button 沒有 PointingHand、drag 沒有 ClosedHand。
- 完成條件：生成 qmldir 宣告 singleton CursorTokens；all_qmllint 無相關 warning；runtime 驗證 SettingsButton 與 toolbar drag cursor。
- 修正證據：待補。

### REV-028 — CLI install／uninstall 失敗後 Settings 永久 busy

- 狀態：Open
- 證據：src/qml/settings/GeneralSettings.qml:89-132；src/qml/SettingsBackend.cpp:958-971；src/platform/PlatformFeatures_mac.mm:266-295；src/platform/PlatformFeatures_linux.cpp:183-209；Windows 實作 src/platform/PlatformFeatures_win.cpp:173-197。
- 觸發：使用者點 Install／Uninstall，之後取消 macOS 管理員授權，或 Linux mkdir／open／chmod／remove 失敗。
- 後果：QML 先設 busy=true，失敗時 backend 不 emit cliInstalledChanged，而這是唯一解除 busy 的路徑；按鈕停在 Please wait... 直到重建 Settings。Windows 寫 Registry 後也未 sync／檢查 status，可能 false-success。
- 完成條件：成功與失敗都發出 completion；失敗恢復 busy、保留原狀態並顯示錯誤；Windows 驗證實際 PATH store 狀態後才回成功。
- 修正證據：待補。

### REV-029 — ColorWheel triangle 的 value 軸使用錯誤尺寸

- 狀態：Open
- 證據：src/colorwidgets/ColorDialog.cpp:49-54；src/colorwidgets/ColorWheel.cpp:145-170,262-299,319-333,402-415。
- 觸發：預設 Triangle color picker，在 triangle 右側選擇高 value 顏色。
- 後果：x 軸長度是 width，render 卻用 x / height；最右端只畫約 86% value，但 selector／輸出可到 100%，指示點下顏色與實際 QColor 不一致。x=0 的 sliceH=0 亦可導致 saturation 除零。
- 完成條件：固定 hue，對多個 S/V 比較 selector pixel 與 color()；包含 V=0/128/255、邊界與不同 DPR，且不得有 NaN／invalid QColor。
- 修正證據：待補。

### REV-030 — QmlDialog::setModal(true) 沒有建立 modal window

- 狀態：Open
- 證據：src/qml/QmlDialog.mm:113-136,175-207；caller：src/MainApplication.cpp:1140-1149、src/PinWindow.cpp:1767-1791。
- 觸發：caller 在 show 前 setModal(true)。
- 後果：函式只保存 bool，未對 backing QQuickView 呼叫 setModality；QWindow::modality 仍為 NonModal。背景 PinWindow／其他視窗仍可能接受 input。
- 完成條件：modal true/false 的 backing QWindow modality 正確；用背景測試視窗驗證 mouse／key 阻擋；覆蓋 screen picker 與 share-password caller。
- 修正證據：待補。

### REV-031 — Pin Info 顯示新值但 Copy 複製舊值

- 狀態：Open
- 證據：src/PinWindow.cpp:1326-1348,2209-2230。
- 觸發：先開過一次 context menu，再 zoom／rotate／flip／opacity／resize／crop，重新開 Info 並點單一列。
- 後果：action text 被更新，但 triggered lambda capture 的 value 仍是第一次建立時的字串；clipboard 與畫面不同。Copy All 不受影響。
- 完成條件：Size／Zoom／Rotation／Opacity／Mirror／crop data rows 都驗證顯示值與 clipboard 同步。
- 修正證據：待補。

### REV-032 — macOS SCK 未排除錄影 tooltip window

- 狀態：Open
- 證據：src/RecordingManager.cpp:553-557；src/capture/SCKCaptureEngine_mac.mm:467-485；src/qml/QmlRecordingControlBar.mm:104-120,195-214。
- 觸發：ScreenCaptureKit stream 建立後，使用者 hover control bar 顯示獨立 tooltip window。
- 後果：content filter 初始化時只收到 control bar winId；tooltip 沒被加入 excludingWindows。setSharingType:NSWindowSharingNone 是 legacy window-sharing policy，不能替代 SCK content filter，因此 tooltip 可被錄進影片。
- 完成條件：錄影時建立／顯示 tooltip，逐 frame 確認輸出不含 control bar 與 tooltip；動態出現的 overlay 必須更新 filter 或在 stream 前完整建立並排除。
- 修正證據：待補。

### REV-033 — Windows 10 2004 前 exclusion 會變黑色矩形

- 狀態：Open
- 證據：src/platform/WindowLevel_win.cpp:59-94；src/qml/QmlRecordingControlBar.mm:214-224。
- 觸發：在 Windows 10 version 2004 前使用 WDA_EXCLUDEFROMCAPTURE (0x11)。
- 後果：API 在舊版本按 WDA_MONITOR 行為處理，視窗不是透明消失，而是在 capture 中成為黑色區塊；程式註解與 UI 契約誤以為只是「仍可見」。
- 完成條件：runtime version gate；舊版採明確 fallback／警告或不顯示 overlay；Windows 10 1909 與 2004+ 各做錄影 pixel smoke。
- 修正證據：待補。

### REV-034 — Native encoder 靜默關閉 audio，呼叫端仍啟動錄音

- 狀態：Open
- 證據：src/encoding/EncoderFactory.cpp:102-125；src/MediaFoundationEncoder.cpp:292-303,472-496；src/RecordingManager.cpp:781-892。
- 觸發：使用者要求 MP4 audio，但 Media Foundation audio stream configuration 失敗；encoder 仍讓 start() 成功並把內部 audioEnabled 改為 false。
- 後果：RecordingManager 不查 encoder->isAudioEnabled，仍建立／啟動 audio engine、顯示 audio enabled 並丟 PCM 給 worker；最後輸出是無聲影片且沒有對使用者警告。
- 完成條件：encoder startup result 明確回報 audio capability／fallback；呼叫端同步 UI 與 capture engine並發出一次可理解警告；以故意使 audio media type 失敗的 fake／Windows integration test 驗證。
- 修正證據：待補。

### REV-035 — DXGI capture cadence 固定 30 fps

- 狀態：Open
- 證據：include/capture/ICaptureEngine.h:80,142；src/RecordingInitTask.cpp:147-176；src/capture/DXGICaptureEngine_win.cpp:109,758-774。
- 觸發：設定 24、60 或任何非 30 frame rate。
- 後果：setFrameRate 只改 ICaptureEngine base member，DXGICaptureEngine::Private 有另一個固定 30 的 frameRate，QTimer 仍用 1000/30；encoder 卻用使用者選擇值。24 fps 會過度擷取／時間戳取樣不一致，60 fps 會重複／不足 frame，輸出 cadence 不符合設定。
- 完成條件：capture timer 使用同一 frameRate source；24／30／60 各量測固定時間的 callback 數與 frame timestamp cadence，容許合理 timer jitter。
- 修正證據：待補。

### REV-036 — macOS 麥克風中途失效沒有通知

- 狀態：Open
- 證據：src/capture/CoreAudioCaptureEngine_mac.mm:423-439,820-885,1046-1093；沒有註冊 AVCaptureDeviceWasDisconnectedNotification 或 AVCaptureSessionRuntimeErrorNotification。
- 觸發：錄影中拔除 USB microphone、Bluetooth input 斷線，或 AVCaptureSession 發生 runtime error。
- 後果：初始 running 檢查已通過後，程式不再更新 m_microphoneActive／mixer source，也不 emit warning；影片後半段靜音或缺少一個 source，UI 仍顯示音訊正常。
- 完成條件：監聽並在 cleanup 時解除 device/session notifications；中途故障要停用 source、更新 activeSourceChanged、警告使用者；拔除／重連與 system+mic fallback 實機測試。
- 修正證據：待補。

### REV-037 — StepBadge／Mosaic toggle-off 未同步 ToolManager

- 狀態：Open
- 證據：src/region/RegionToolbarHandler.cpp:94-155；src/RegionSelector.cpp:1023-1036,3455-3467；src/region/RegionInputHandler.cpp:970-997。
- 觸發：目前工具是 StepBadge 或 Mosaic，再點同一 toolbar button 切回 Selection。
- 後果：handler 與 inputState 已是 Selection，但 ToolManager::currentTool 仍是舊工具；選取輸入本身可運作，但後續 cursor refresh 會從 stale ToolManager 取回舊工具 cursor。
- 完成條件：兩工具的 toggle-off 都讓 handler、inputState、ViewModel、ToolManager 同步為 Selection；toolCursorRequested 不再回傳舊 cursor。
- 修正證據：待補。

## Potential Issues

### POT-001 — Pin toolbar 比 logical screen 寬時 qBound 範圍無效

- 狀態：Potential
- 信心水準：Medium
- 證據：src/qml/QmlWindowedToolbar.mm:409-439,680-701；src/tools/ToolRegistry.cpp:456-484；src/qml/PinToolbarViewModel.cpp:13-50。
- 待確認情境：m_view->width() > screen.width() - 20，例如窄 logical desktop 或 Windows 225%／250% scaling。
- 可能後果：qBound(minX, x, maxX) 收到 maxX < minX；Debug 可 assertion，Release 可讓右側 Save／Copy／Done 不可達。
- 升格條件：Windows debug 實機或抽出的 placement helper，以實際 toolbar width 和 500–640 px viewport 重現 assertion／不可達 action。
- 修正證據：待確認。

### POT-002 — 直接關閉 Recording Preview 會保留暫存 MP4

- 狀態：Potential
- 信心水準：Medium
- 證據：src/qml/RecordingPreviewBackend.mm:85-88,313-324；src/MainApplication.cpp:904-942；src/RecordingManager.cpp:1319-1329,1468-1499。
- 待確認情境：使用視窗 close button／系統 close，而不是明確按 Discard。
- 可能後果：closing 只 emit closed(false)，不 emit discardRequested；RecordingManager 清空 m_tempVideoPath，但不刪除檔案，直到下次 stale-temp cleanup。這可能是意外磁碟殘留，也可能是刻意的 crash-recovery 保留策略。
- 升格條件：先確認產品對「關閉預覽」的契約；若等同 Discard，關閉後 temp file 必須立即移除；若要保留，UI／cleanup retention 必須明文化並有上限測試。
- 修正證據：待確認。

## 已反證或不列入本次 tracker

- Share JPEG HiDPI flatten helper 有可重現 defect，但 Share 在目前 Region／Pin toolbar 都被刻意隱藏，沒有正式 UI、shortcut 或 CLI 入口，因此是 latent unreachable code，不列 Confirmed。
- 錄影開始後 100 ms 內 pause 的 state hole 目前沒有可達 pause UI／caller，不列 Confirmed。
- Rotated Pin double-click raw point 只影響已移除的 preview path，不列。
- WebP baseline 行為契約不明，不列 bug。
- 先前懷疑的 RGB/BGR、WASAPI、QScreen UAF、Watermark opacity、Eraser cancel/undo、time double-click 已被目前程式碼反證或已修復。
- qmllint 的純樣式／版本可接受 warnings 不列 bug；CursorTokens 是唯一經 runtime 證實並升格的 lint finding。
- 首次完整 test run 的 clipboard、IPC、SettingsBackend 三個失敗均由 sandbox 限制造成；在 sandbox 外重跑 3/3 通過，不列產品 bug。

## 審查覆蓋與驗證紀錄

- Tracked files：1,046。
- C++／Objective-C++／QML：687 files，約 151,659 lines。
- Automation／build files：49 files，約 10,227 lines。
- 其餘 CMake、packaging、Jekyll、docs source 亦已掃描；build、release 與 docs/_site 的生成／重複輸出不當成 source 重複計數。
- ./scripts/run-tests.sh：143 tests；sandbox 造成的 3 個失敗在 sandbox 外重跑後全數通過，因此有效結果為 143/143。
- cmake --build build --target all_qmllint：exit 0。
- Shell bash -n、JavaScript node --check、XML／QRC／plist／TS well-formed、resource references、git diff --check：通過。
- Jekyll temporary build：通過；內部 broken links：0。
- 建立 tracker 前的 repository 基準沒有被 review 修改；但 test 本身造成的 Debug settings 副作用已記錄於 REV-004。

## 參考資料

- SnapTray v1.0.62 release：https://github.com/victorfu/snap-tray/releases/tag/v1.0.62
- Microsoft OcrEngine.MaxImageDimension：https://learn.microsoft.com/en-us/uwp/api/windows.media.ocr.ocrengine.maximagedimension
- Microsoft OCR sample：https://github.com/Microsoft/Windows-universal-samples/blob/main/Samples/OCR/cs/OcrFileImage.xaml.cs
- Qt QThread termination warning：https://doc.qt.io/qt-6/qthread.html
- Apple AVCaptureSession：https://developer.apple.com/documentation/avfoundation/avcapturesession
- Apple NSWindow sharing none：https://developer.apple.com/documentation/appkit/nswindow/sharingtype-swift.enum/none
- Microsoft SetWindowDisplayAffinity：https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity
- Apple device-disconnected notification：https://developer.apple.com/documentation/avfoundation/avcapturedevice/wasdisconnectednotification
- Apple capture-session runtime error notification：https://developer.apple.com/documentation/avfoundation/avcapturesession/runtimeerrornotification

## 更新紀錄

| 日期 | 變更 |
|---|---|
| 2026-09-01 | 建立全庫 review tracker；37 Confirmed、2 Potential；REV-004 與 REV-009 因既有未提交候選修正標為 In Progress。 |
