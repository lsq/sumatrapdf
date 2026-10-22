/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Win.h"
#include "base/UITask.h"

// #include "SumatraPDF.h" // MessageBoxWarning / Tr（如在别处，换对应头）
#include "Translations.h"
#include "DashbeamSend.h"

#include "SumatraLog.h"

// --- dll 的 FFI 签名（与 dashbeam.h 一致）---------------------------------

typedef void (*dashbeam_event_cb)(const char* event, const char* payload, void* user_data);
typedef i64 (*Sig_send_start)(const char* const* paths, size_t path_count, const char* options_json,
                              dashbeam_event_cb cb, void* user_data);
typedef i32 (*Sig_send_stop)(i64 handle);
typedef i64 (*Sig_completed_peers)(i64 handle);
typedef const char* (*Sig_last_error)(void);

static HMODULE gDashbeamDll = nullptr;
static Sig_send_start DynSendStart = nullptr;
static Sig_send_stop DynSendStop = nullptr;
static Sig_completed_peers DynCompletedPeers = nullptr;
static Sig_last_error DynLastError = nullptr;

static i64 gDashbeamHandle = -1;

constexpr const char* kDashbeamDllName = "dashbeam_ffi.dll";

// --- 加载 ------------------------------------------------------------------

bool DashbeamEnsureLoaded(HWND hwndParent) {
    if (gDashbeamDll) {
        return true;
    }
    // dll 必须和 exe 同目录
    TempStr dllPath = GetPathInExeDirTemp(Str(kDashbeamDllName));
    if (!file::Exists(dllPath)) {
        Str msg = Tr("dashbeam_ffi.dll not found next to SumatraPDF.exe");
        MsgBox(hwndParent, msg, Str(kDashbeamDllName), MB_OK | MB_ICONERROR);
        logf("(SendMe)DashbeamEnsureLoaded: missing %s\n", dllPath);
        return false;
    }
    WCHAR* dllPathW = CWStrTemp(dllPath);
    HMODULE h = LoadLibraryW(dllPathW);
    if (!h) {
        MsgBox(hwndParent, Tr("Failed to load dashbeam_ffi.dll"), Str(kDashbeamDllName), MB_OK | MB_ICONERROR);
        logf("(SendMe)DashbeamEnsureLoaded: LoadLibraryW failed for %s\n", dllPath);
        return false;
    }
    DynSendStart = (Sig_send_start)GetProcAddress(h, "dashbeam_send_start");
    DynSendStop = (Sig_send_stop)GetProcAddress(h, "dashbeam_send_stop");
    DynCompletedPeers = (Sig_completed_peers)GetProcAddress(h, "dashbeam_send_completed_peers");
    DynLastError = (Sig_last_error)GetProcAddress(h, "dashbeam_last_error");
    if (!DynSendStart || !DynSendStop || !DynLastError) {
        FreeLibrary(h);
        DynSendStart = nullptr;
        DynSendStop = nullptr;
        DynCompletedPeers = nullptr;
        DynLastError = nullptr;
        MsgBox(hwndParent, Tr("dashbeam_ffi.dll is missing required exports"), Str(kDashbeamDllName),
               MB_OK | MB_ICONERROR);
        return false;
    }
    gDashbeamDll = h;
    return true;
}

// --- 事件 marshal 到 UI 线程 ----------------------------------------------

struct DlEventMsg {
    Str event;   // 堆拷贝
    Str payload; // 堆拷贝
};

static void DlDeliverEvent(DlEventMsg* msg) {
    // UI 线程：交给对话框更新
    DownloadProgressNotifyEvent(msg->event, msg->payload);
    str::Free(msg->event);
    str::Free(msg->payload);
    delete msg;
}

// 注意：此回调在 dll 的 tokio worker 线程上触发，event/payload 仅在回调期间有效，
// 必须复制后再投回 UI 线程。
static void DashbeamOnEvent(const char* event, const char* payload, void* /*user_data*/) {
    auto* msg = new DlEventMsg;
    msg->event = str::Dup(Str(event ? event : ""));
    msg->payload = str::Dup(Str(payload ? payload : ""));
    auto fn = MkFunc0<DlEventMsg>(DlDeliverEvent, msg);
    uitask::Post(fn, nullptr);
}

// --- json 转义（strip_suffix 等字符串字段）--------------------------------

static TempStr JsonEscape(Str s) {
    str::Builder b(GetTempArena());
    for (int i = 0; i < s.len; i++) {
        char c = s.s[i];
        if (c == '"' || c == '\\') {
            b.AppendChar('\\');
            b.AppendChar(c);
        } else if (c == '\n') {
            b.Append(StrL("\\n"));
        } else if (c == '\r') {
            b.Append(StrL("\\r"));
        } else if (c == '\t') {
            b.Append(StrL("\\t"));
        } else {
            b.AppendChar(c);
        }
    }
    return ToStrTemp(b);
}

// --- 开始 / 停止 -----------------------------------------------------------

i64 DashbeamSendStart(const StrVec& paths, Str stripSuffix) {
    if (!DynSendStart) {
        return -1;
    }
    // paths 转成 const char*[]（dll 要求 UTF-8 路径）
    int n = len(paths);
    if (n <= 0) {
        return -1;
    }
    const char** cPaths = AllocArrayTemp<const char*>(n);
    for (int i = 0; i < n; i++) {
        cPaths[i] = paths.At(i).s; // Str 内部是以 \0 结尾的 utf8
        logf("(SendMe)正在提取文件：%s\n", paths.At(i));
    }

    // options：公共 relay + RelayAndAddresses ticket + 可选 strip_suffix
    str::Builder ob(GetTempArena());
    ob.Append(StrL("{\"ticket_type\":\"RelayAndAddresses\""));
    if (len(stripSuffix) > 0) {
        ob.Append(StrL(",\"strip_suffix\":\""));
        ob.Append(JsonEscape(stripSuffix));
        ob.Append(StrL("\""));
    }
    ob.Append(StrL("}"));
    TempStr opts = ToStrTemp(ob);

    i64 h = DynSendStart(cPaths, (size_t)n, opts.s, DashbeamOnEvent, nullptr);
    if (h < 0) {
        const char* err = DynLastError ? DynLastError() : nullptr;
        logf("(SendMe)DashbeamSendStart failed: %s\n", err ? Str(err) : StrL("unknown"));
        return -1;
    }
    gDashbeamHandle = h;
    return h;
}

void DashbeamSendStop() {
    if (DynSendStop && gDashbeamHandle >= 0) {
        DynSendStop(gDashbeamHandle);
        gDashbeamHandle = -1;
    }
}

// 返回当前分享会话已完成传输的对端数；无会话/句柄无效/未加载返回 -1
i64 DashbeamSendCompletedPeers() {
    if (!DynCompletedPeers || gDashbeamHandle < 0) {
        return -1;
    }
    return DynCompletedPeers(gDashbeamHandle);
}
