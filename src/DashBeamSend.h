/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// 动态加载 dashbeam_ffi.dll 并封装文件分享（发送）。dll 必须和
// SumatraPDF.exe 在同一目录，否则 DashbeamEnsureLoaded 弹框并返回 false。

// 确保 dll 已加载并解析到所有导出函数。失败时弹 MessageBox（父窗口 hwndParent）。
bool DashbeamEnsureLoaded(HWND hwndParent);

// 开始分享 paths（UTF-8 路径）。stripSuffix 可为空。
// 返回 >0 的句柄，失败返回 -1（错误已记录到日志）。
// 进度/生命周期通过 DownloadProgressNotifyEvent(event,payload) 回到 UI 线程。
i64 DashbeamSendStart(const StrVec& paths, Str stripSuffix);

// 停止当前分享会话（幂等，可重复调用）。
void DashbeamSendStop();

// 由 DownloadProgressDialog.cpp 实现：在 UI 线程被调用，用事件更新界面。
void DownloadProgressNotifyEvent(Str event, Str payload);

// 返回当前分享会话已完成传输的对端数；无会话/句柄无效返回 -1
i64 DashbeamSendCompletedPeers();
