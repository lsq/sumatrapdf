/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// Ctrl+U opens a modeless download progress window. Data arrives via
// OnProgressData(struct {bytes,total,speed}); the simulation below pumps an
// array of 10 samples over ~10s from a background thread.
#pragma comment(lib, "dwmapi.lib")

#include <windows.h>
#include "base/Base.h"
#include <dwmapi.h>
#include "base/Win.h"
#include "base/UITask.h"
#include "shared/SvgIcons.h"
#include "base/Pixmap.h"
#include "gui/Dpi.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/VirtCtrl.h"
#include "shared/Version.h"

#include "Settings.h"
#include "AppSettings.h"
#include "MainWindow.h"
#include "Theme.h"
#include "SumatraConfig.h"
#include "SumatraPDF.h"
#include "Translations.h"
#include "DarkMode.h"
#include "SumatraDialogs.h"
#include "shared/PdfToolsCommon.h"
#include "AppHelpersCommon.h"
#include "DashBeamSend.h"
#include "base/TempRenameGuard.h"

#include <atomic>
#include <cmath>

constexpr int kDlDlgDx = 400;
static const UINT_PTR kCopyDoneTimerId = 1001;
static const UINT kCopyDoneMs = 3000;

// ---------------------------------------------------------------------------
// data

// what a real download source reports: cumulative bytes, total, current speed
struct DownloadProgressData {
    i64 bytes = 0; // downloaded so far
    i64 total = 0; // total size
    i64 speed = 0; // bytes/sec; percent = bytes/total, eta = (total-bytes)/speed
};

// the simulated "real" source: 10 samples over ~10s, 100 MB total, varying speed
// static const DownloadProgressData gDlSamples[] = {
//     {8ll * 1024 * 1024, 100ll * 1024 * 1024, 8ll * 1024 * 1024},
//     {19ll * 1024 * 1024, 100ll * 1024 * 1024, 11ll * 1024 * 1024},
//     {28ll * 1024 * 1024, 100ll * 1024 * 1024, 9ll * 1024 * 1024},
//     {40ll * 1024 * 1024, 100ll * 1024 * 1024, 12ll * 1024 * 1024},
//     {50ll * 1024 * 1024, 100ll * 1024 * 1024, 10ll * 1024 * 1024},
//     {59ll * 1024 * 1024, 100ll * 1024 * 1024, 9ll * 1024 * 1024},
//     {70ll * 1024 * 1024, 100ll * 1024 * 1024, 11ll * 1024 * 1024},
//     {82ll * 1024 * 1024, 100ll * 1024 * 1024, 12ll * 1024 * 1024},
//     {91ll * 1024 * 1024, 100ll * 1024 * 1024, 9ll * 1024 * 1024},
//     {100ll * 1024 * 1024, 100ll * 1024 * 1024, 9ll * 1024 * 1024},
// };

// ---------------------------------------------------------------------------
// three solid concentric circles, darkest in the center, lighter outward

enum class DlCircleState {
    Idle,
    Transferring,
    Done
};

struct CircleCtrl : VirtCtrl {
    Color accent = kColorUnset;
    Color bg = kColorUnset;
    DlCircleState state = DlCircleState::Idle;
    int phase = 0; // 动画帧计数，由 OnAnimTick 递增

    Size GetIdealSize() override {
        int d = DpiScale(64);
        return {d, d};
    }

    // void Paint(VirtPaintCtx& ctx) override {
    //     Rect r = ctx.bounds;
    //     int d = std::min(r.dx, r.dy);
    //     r = {r.x + (r.dx - d) / 2, r.y + (r.dy - d) / 2, d, d};
    //
    //     int dMid = d * 2 / 3;
    //     int dIn = d / 3;
    //
    //     // a base fill so the alpha blends against the window background
    //     ctx.gfx->FillEllipse(r, bg);
    //     // outer -> inner: 90 -> 170 -> 255 alpha (inside darkest)
    //     ctx.gfx->FillEllipse(r, accent, 90);
    //     Rect mid{r.x + (d - dMid) / 2, r.y + (d - dMid) / 2, dMid, dMid};
    //     ctx.gfx->FillEllipse(mid, accent, 170);
    //     Rect in{r.x + (d - dIn) / 2, r.y + (d - dIn) / 2, dIn, dIn};
    //     ctx.gfx->FillEllipse(in, accent, 255);
    // }
    // 三层由内向外变浅；Transferring 时各层半径/透明度随 phase 脉动
    void Paint(VirtPaintCtx& ctx) override {
        Rect r = ctx.bounds;
        int d = std::min(r.dx, r.dy);
        r = {r.x + (r.dx - d) / 2, r.y + (r.dy - d) / 2, d, d};

        ctx.gfx->FillEllipse(r, bg); // 底色，保证 alpha 混合基准一致

        if (state == DlCircleState::Idle) {
            // 未开始：一个灰暗的小圆点
            int dd = d / 6;
            ctx.gfx->FillEllipse({r.x + (d - dd) / 2, r.y + (d - dd) / 2, dd, dd}, accent, 90);
            return;
        }
        if (state == DlCircleState::Done) {
            // 结束：收敛成一个实心圆
            int dd = d / 2;
            ctx.gfx->FillEllipse({r.x + (d - dd) / 2, r.y + (d - dd) / 2, dd, dd}, accent);
            return;
        }

        // Transferring：三层圆，半径做正弦脉动，相位互相错开 → 由内向外扩散的波纹
        for (int i = 0; i < 3; i++) {
            float frac = (float)(3 - i) / 3.f; // 1.0, 0.66, 0.33（内→外）
            float wave = 0.5f + 0.5f * sinf((float)phase * 0.15f + i * 2.1f);
            int dd = (int)(d * frac * (0.75f + 0.25f * wave));
            u8 alpha = (u8)(110 + 120 * ((float)i / 1.f) * wave + 25); // 内层更实、随相位明暗
            ctx.gfx->FillEllipse({r.x + (d - dd) / 2, r.y + (d - dd) / 2, dd, dd}, accent, alpha);
        }
    }
};

// ---------------------------------------------------------------------------
// window

// 传输状态指示：实心绿色小圆
struct StatusDotCtrl : VirtCtrl {
    Color col = kColorUnset;
    Size GetIdealSize() override {
        int d = DpiScale(10);
        return {d, d};
    }
    void Paint(VirtPaintCtx& ctx) override {
        Rect r = ctx.bounds;
        int d = std::min(r.dx, r.dy);
        ctx.gfx->FillEllipse({r.x + (r.dx - d) / 2, r.y + (r.dy - d) / 2, d, d}, col);
    }
};

// 简单的 stop 图标：圆角方块
// static const char* gDlStopSvg = R"(
//     <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
//     <rect x="3" y="3" width="10" height="10" rx="2" fill="red"/></svg>
//     )";

static const char* gDlStopSvg = R"(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
  <!-- 外圆：灰色线条，无填充 -->
  <circle cx="8" cy="8" r="7" fill="none" stroke="#c0c0c0" stroke-width="1.0"/>
  <!-- 内部红色圆角矩形（保持不变） -->
  <rect x="5" y="5" width="6" height="6" rx="1.5" fill="red"/>
</svg>
)";

static const char* gDlgCopy = R"(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#000040" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="icon icon-tabler icons-tabler-outline icon-tabler-copy">
	<path stroke="none" d="M0 0h24v24H0z" fill="none" />
	<path d="M7 9.667a2.667 2.667 0 0 1 2.667 -2.667h8.666a2.667 2.667 0 0 1 2.667 2.667v8.666a2.667 2.667 0 0 1 -2.667 2.667h-8.666a2.667 2.667 0 0 1 -2.667 -2.667l0 -8.666" />
	<path d="M4.012 16.737a2.005 2.005 0 0 1 -1.012 -1.737v-10c0 -1.1 .9 -2 2 -2h10c.75 0 1.158 .385 1.5 1" />
</svg>
)";

static const char* gCopyCheck = R"(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#00ff00" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="icon icon-tabler icons-tabler-outline icon-tabler-checks">
	<path stroke="none" d="M0 0h24v24H0z" fill="none" />
	<path d="M7 12l5 5l10 -10" />
	<path d="M2 12l5 5m5 -5l5 -5" />
</svg>
)";
// static const char* gDlStopSvg = R"(
// <svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="none"
// stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="icon icon-tabler icons-tabler-outline
// icon-tabler-circle-stop"> 	<path stroke="none" d="M0 0h24v24H0z" fill="none" /> 	<path d="M3 12a9 9 0 1 0 18 0a9
// 9 0 1 0 -18 0" stroke="currentColor"/> 	<path d="M9 10a1 1 0 0 1 1 -1h4a1 1 0 0 1 1 1v4a1 1 0 0 1 -1 1h-4a1 1 0
// 0 1 -1 -1v-4" fill="red"/>
// </svg>)";

struct PanelBgCtrl : VirtCtrl {
    Color fill = kColorUnset;
    int radius = 8;
    Size GetIdealSize() override { return {0, 0}; } // 不参与撑大小，由内容决定
    void Paint(VirtPaintCtx& ctx) override { ctx.gfx->FillRoundedRect(ctx.bounds, DpiScale(radius), fill); }
};

// SVG 图标按钮：点中即触发 onClick（VirtCtrl::OnMouseUp 已支持 onClick）
struct IconButtonCtrl : VirtCtrl {
    Pixmap* pix = nullptr; // 不持有，GetCachedPixmapForSvg 缓存所有
    bool enabled = true;
    int baseSize = 24;
    Size GetIdealSize() override {
        int d = DpiScale(baseSize);
        return {d, d};
    }
    void Paint(VirtPaintCtx& ctx) override {
        if (!pix) {
            return;
        }
        // 注意：请对照 src/gui/Gfx.h 确认画 Pixmap 的方法名，
        // 可能是 DrawPixmap / DrawImage / BlitPixmap 之类
        ctx.gfx->DrawPixmap(pix, ctx.bounds);
    }
    bool HitTest(Point) override { return enabled; }
};

struct ProgressBarCtrl : VirtCtrl {
    float frac = 0.f;          // 0..1
    Color c1 = kColorUnset;    // 渐变起点
    Color c2 = kColorUnset;    // 渐变终点
    Color track = kColorUnset; // 未完成轨道色
    Color border = kColorUnset;

    Size GetIdealSize() override { return {DpiScale(kDlDlgDx - 2 * kToolDlgPadDx), DpiScale(12)}; }

    // 注意：通道拆分按项目实际的 Color 帮助函数改写
    static Color Blend(Color a, Color b, float t) {
        auto lerp = [t](int x, int y) { return x + (int)((y - x) * t); };
        auto chan = [](Color c, int shift) { return (int)((u32)c >> shift & 0xff); };
        u8 r = (u8)lerp(chan(a, 0), chan(b, 0));
        u8 g = (u8)lerp(chan(a, 8), chan(b, 8));
        u8 bl = (u8)lerp(chan(a, 16), chan(b, 16));
        u8 al = (u8)lerp(chan(a, 24), chan(b, 24));
        return (Color)((u32)al << 24 | (u32)bl << 16 | (u32)g << 8 | r);
    }

    // void Paint(VirtPaintCtx& ctx) override {
    //     Rect r = ctx.bounds;
    //     int radius = r.dy / 2;
    //
    //     // 轨道：圆角矩形（未填充部分不画填充，只画轨道底色+边）
    //     ctx.gfx->FillRoundedRect(r, radius, track, border);
    //
    //     int w = (int)((float)r.dx * frac);
    //     if (w <= 0) {
    //         return;
    //     }
    //     if (w >= r.dx) {
    //         // 满格：整条圆角 + 整体渐变
    //         w = r.dx;
    //     }
    //     // 左端圆帽：一个直径=高度的圆，颜色取渐变最左端
    //     ctx.gfx->FillEllipse({r.x, r.y, r.dy, r.dy}, c1);
    //     // 填充主体：逐列切片，颜色按"整轨位置"取样 → 渐变是从整体截取的
    //     int x0 = r.x + radius;
    //     int x1 = r.x + w;
    //     for (int x = x0; x < x1; x++) {
    //         float t = (float)(x - r.x) / (float)r.dx; // 关键：除以整轨宽，不是 w
    //         ctx.gfx->FillRect({x, r.y, 1, r.dy}, Blend(c1, c2, t));
    //     }
    //     // 右端直角已由 FillRect 保证；若 w < radius 极小值时圆帽即可
    // }
    void Paint(VirtPaintCtx& ctx) override {
        Rect r = ctx.bounds;
        int h = DpiScale(12);
        r = {r.x, r.y + (r.dy - h) / 2, r.dx, h}; // 锁定高度并垂直居中
        int radius = r.dy / 2;

        // 轨道：圆角矩形
        ctx.gfx->FillRoundedRect(r, radius, track, border);

        int w = (int)((float)r.dx * frac + 0.5f);
        if (w <= 0) {
            return;
        }
        bool full = (w >= r.dx);
        if (full) {
            w = r.dx;
        }

        // 裁剪到填充区域，切片/圆帽画出去的部分自动丢掉
        ctx.gfx->PushClip({r.x, r.y, w, r.dy});

        // 左端圆帽（颜色取渐变起点）
        ctx.gfx->FillEllipse({r.x, r.y, r.dy, r.dy}, c1);

        // 右端：满格时补圆帽（终点色），未满时靠 clip 在 w 处切成直角
        if (full) {
            ctx.gfx->FillEllipse({r.x + r.dx - r.dy, r.y, r.dy, r.dy}, Blend(c1, c2, 1.f));
        }

        // 主体：逐列切片；t 除以整轨宽 → 渐变从整体截取
        int x0 = r.x + radius;
        int x1 = r.x + w - (full ? radius : 0);
        for (int x = x0; x < x1; x++) {
            float t = (float)(x - r.x) / (float)r.dx;
            ctx.gfx->FillRect({x, r.y, 1, r.dy}, Blend(c1, c2, t));
        }

        ctx.gfx->PopClip();

        // 描边压一遍，盖住可能溢出的像素
        ctx.gfx->FillRoundedRect(r, radius, kColorTransparent, border);
    }
};

// 把颜色向白色混合 t 份（0 = 原色，1 = 纯白）
static Color BlendLighter(Color c, float t) {
    auto up = [t](int v) { return (u8)(v + (int)((255 - v) * t)); };
    return MkRgb(up(GetRed(c)), up(GetGreen(c)), up(GetBlue(c)));
}

// 外层圆角矩形背景（文本框的容器）
struct RoundedBoxCtrl : VirtCtrl {
    Color fill = kColorUnset;
    Color border = kColorUnset;
    int radius = 0; // 未缩放
    int minDy = 0;  // 未缩放，保证最小高度

    Size GetIdealSize() override { return {DpiScale(kDlDlgDx - 2 * kToolDlgPadDx), DpiScale(minDy)}; }

    void Paint(VirtPaintCtx& ctx) override { ctx.gfx->FillRoundedRect(ctx.bounds, DpiScale(radius), fill, border); }
};

static const char* kDlLinkText = "blobadv4vhjx6nlnwktynp7lmog4u5v3u4py3onpxdomfz6uhli4pojuwajdnb";

struct DownloadProgressWnd : WindowBase {
    ~DownloadProgressWnd() override = default;

    MainWindow* win = nullptr;
    IconButtonCtrl* btnStopIcon = nullptr;
    IconButtonCtrl* copyIcon = nullptr;
    ProgressBarCtrl* bar = nullptr;
    CircleCtrl* circle = nullptr;
    Edit* linkEdit = nullptr;
    RoundedBoxCtrl* box = nullptr;
    VirtCloseButton* capCloseBtn = nullptr;
    HBox* captionRow = nullptr;
    VirtText* capTitle = nullptr;
    VirtText* titleLabel = nullptr;
    VirtText* subtitleLabel = nullptr;
    VirtText* percLabel = nullptr;
    VirtText* speedLabel = nullptr;
    VirtText* sizeLabel = nullptr;
    VirtText* etaLabel = nullptr;

    TempRenameGuard* renameGuard = nullptr;
    DownloadProgressData data{}; // last value delivered by the source
    DlCircleState circleState = DlCircleState::Idle;
    int animPhase = 0;
    int fileBoxLastDy = 0; // 上次算出的高度，避免反复重排
    int connectedPeers = 0;
    int activeConns = 0;
    std::atomic<bool> stopped{false};

    bool Create(MainWindow* win);
    void OnStopClicked(VirtMouseEvent* ev = nullptr);
    void OnCopyTicket(VirtMouseEvent*);
    void OnCopyClicked(VirtMouseEvent*);
    // entry point for real download data: the source calls this per chunk.
    // Must run on the ui thread (callers from other threads post via uitask)
    void OnProgressData(const DownloadProgressData& d);
    void RefreshLabels();
    void OnAnimTick();
    void OnGetMinMaxInfo(WindowBase::GetMinMaxInfoEvent* ev);
    void UpdateFileBoxHeight();
    void OnSize(WindowBase::SizeEvent* ev);
    void RefreshSubtitle();
    void Relayout();
    void OnTimer(WindowBase::TimerEvent* ev);

    void OnNcHitTest(WindowBase::NcHitTestEvent* ev);
    void OnWndProc(WindowBase::WndProcEvent* ev);
};

static DownloadProgressWnd* gDownloadWnd = nullptr;
static std::atomic<bool> gDlPolling{false}; // 轮询线程存活标志

// runs on the ui thread: 写入最新已完成数并刷新副标题
struct DlPeersMsg {
    DownloadProgressWnd* wnd = nullptr;
    i64 completed = 0;
};

static void DlDeliverPeers(DlPeersMsg* msg) {
    DownloadProgressWnd* wnd = msg->wnd;
    // 窗口已销毁/已换 → 丢弃（和 DlDeliverSample 同一套防护）
    if (gDownloadWnd == wnd && !wnd->stopped.load() && msg->completed >= 0) {
        if (wnd->connectedPeers != (int)msg->completed) {
            wnd->connectedPeers = (int)msg->completed; // 权威完成数覆盖显示
            wnd->RefreshSubtitle();
        }
    }
    delete msg;
}

static void ReversFileName() {
    if (gDownloadWnd && gDownloadWnd->renameGuard) {
        delete gDownloadWnd->renameGuard; // ~TempRenameGuard() 在这里还原
        gDownloadWnd->renameGuard = nullptr;
    }
}

static void ClearDownloadWnd() {
    gDlPolling = false; // 让轮询线程在下一拍退出
    ReversFileName();
    gDownloadWnd = nullptr;
}

constexpr int kDlDlgMinDy = 340; // 最小高度，保证各区块不被压没

void DownloadProgressWnd::OnCopyTicket(VirtMouseEvent*) {
    if (!linkEdit) {
        return;
    }
    TempStr s = HwndGetTextTemp(linkEdit->hwnd); // 读原生 Edit 的当前文本
    if (str::IsEmptyOrWhiteSpace(s)) {
        return;
    }
    CopyTextToClipboard(s);
}

void DownloadProgressWnd::OnCopyClicked(VirtMouseEvent*) {
    bool ok = CopyTextToClipboard(linkEdit->GetTextTemp()); // 或你保存的完整 ticket
    if (!ok || !copyIcon) {
        return;
    }
    int iconSz = DpiScale(20);
    Color colTxt = ThemeWindowTextColor();
    copyIcon->pix = GetCachedPixmapForSvg(Str(gCopyCheck), iconSz, iconSz, colTxt, kColorTransparent);
    copyIcon->Invalidate(); // 或 HwndInvalidate(hwnd);
    // 重置计时：先 Kill 再 Set，连点也只保留最后一次
    KillTimer(hwnd, kCopyDoneTimerId);
    SetTimer(hwnd, kCopyDoneTimerId, kCopyDoneMs, nullptr);
}

void DownloadProgressWnd::OnTimer(WindowBase::TimerEvent* ev) {
    if (ev->timerId != kCopyDoneTimerId) {
        return;
    }
    KillTimer(hwnd, kCopyDoneTimerId); // 一次性
    if (!copyIcon) {
        return;
    }
    int iconSz = DpiScale(20);
    Color colTxt = ThemeWindowTextColor();
    copyIcon->pix = GetCachedPixmapForSvg(Str(gDlgCopy), iconSz, iconSz, colTxt, kColorTransparent);
    copyIcon->Invalidate();
}

void DownloadProgressWnd::OnSize(WindowBase::SizeEvent* ev) {
    // autoLayout 的 DoLayout 已经跑完，这里只需按新宽度修正框高
    UpdateFileBoxHeight();
}

void DownloadProgressWnd::OnWndProc(WindowBase::WndProcEvent* ev) {
    if (ev->msg != WM_NCCALCSIZE || ev->wparam == FALSE) {
        return; // 其他消息 / wparam==FALSE 时交回框架默认处理
    }
    // wparam==TRUE: lparam 指向 NCCALCSIZE_PARAMS，rgrc[0] 是建议的新客户区
    auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(ev->lparam);
    RECT& client = p->rgrc[0];

    // 左右下三边保留系统 resize 边框（用系统边框宽度收缩），
    // 顶边不下移 -> 客户区吃掉顶部那条非客户区，灰底画到窗口最顶，消除白缝
    int cx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
    int cy = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
    client.left += cx;
    client.right -= cx;
    client.bottom -= cy;
    // client.top 不动：顶部 0 像素非客户区

    ev->result = 0;       // 返回 0：只用 rgrc[0]（不做源/目标矩形复制对齐）
    ev->didHandle = true; // 告诉框架用我们的 result，不再走默认
}

// void DownloadProgressWnd::OnNcHitTest(WindowBase::NcHitTestEvent* ev) {
//     Point pt = HwndScreenToClient(hwnd, ev->screenPos);
//     Rect client = HwndClientRect(hwnd);
//     if (!client.Contains(pt)) {
//         return; // 不在客户区（即在 THICKFRAME 边框上）→ 不处理，系统做 resize
//     }
//     // 关闭按钮区域让给按钮本身
//     if (capCloseBtn && capCloseBtn->lastBounds.Contains(pt)) {
//         ev->result = HTCLIENT;
//         ev->didHandle = true;
//         return;
//     }
//     // caption 行内的空白 → 整窗拖动；其它客户区按你需要也可返回 HTCLIENT
//     int capBar_bottom_y = captionRow ? captionRow->lastBounds.Bottom() : 0;
//     if (pt.y <= capBar_bottom_y) { // 只在标题行高度内当拖动区
//         ev->result = HTCAPTION;
//     } else {
//         ev->result = HTCLIENT;
//     }
//     ev->didHandle = true;
//     // 否则不设 didHandle，保持 HTCLIENT（进度条/文件框等正常交互）
// }

void DownloadProgressWnd::OnNcHitTest(WindowBase::NcHitTestEvent* ev) {
    Point pt = HwndScreenToClient(hwnd, ev->screenPos);
    Rect c = HwndClientRect(hwnd);
    if (!c.Contains(pt)) {
        return; // 交给系统默认
    }
    int b = DpiScale(6); // 边缘热区宽度
    bool left = pt.x < b, right = pt.x >= c.dx - b;
    bool top = pt.y < b, bottom = pt.y >= c.dy - b;

    int capBar_bottom_y = captionRow ? captionRow->lastBounds.Bottom() : 0;
    int ht = HTCLIENT;
    if (top && left)
        ht = HTTOPLEFT;
    else if (top && right)
        ht = HTTOPRIGHT;
    else if (bottom && left)
        ht = HTBOTTOMLEFT;
    else if (bottom && right)
        ht = HTBOTTOMRIGHT;
    else if (left)
        ht = HTLEFT;
    else if (right)
        ht = HTRIGHT;
    else if (top)
        ht = HTTOP;
    else if (bottom)
        ht = HTBOTTOM;
    else if (capCloseBtn && capCloseBtn->lastBounds.Contains(pt))
        ht = HTCLIENT; // X 可点
    else if (btnStopIcon && btnStopIcon->lastBounds.Contains(pt))
        ht = HTCLIENT; // ▩ 可点
    else if (pt.y < capBar_bottom_y)
        ht = HTCAPTION; // 顶部条拖动
    else
        ht = HTCLIENT;

    ev->result = ht;
    ev->didHandle = true;
}

void DownloadProgressWnd::Relayout() {
    if (!hwnd || !layout) {
        return;
    }
    // 按当前客户区宽度重新测量/排布；内容变高时窗口随之长高
    Rect rc = HwndClientRect(hwnd);
    UpdateFileBoxHeight();
    LayoutAndSizeToContent(layout, rc.dx, rc.dy, hwnd);
    DoLayout(HwndClientRect(hwnd).Size());
    HwndInvalidate(hwnd);
}

void DownloadProgressWnd::UpdateFileBoxHeight() {
    if (!linkEdit || !linkEdit->hwnd || !box) {
        return;
    }
    int lines = (int)SendMessageW(linkEdit->hwnd, EM_GETLINECOUNT, 0, 0);
    if (lines <= 0) {
        return;
    }
    // 行高：字体行高；若 PlatformFontLineHeight 不存在，用
    // GetTextMetrics/GetTextExtentPoint32 量出固定值
    int lineDy = PlatformFontLineHeight(linkEdit->GetFont()); // 字段名以 Edit 实际为准
    int inset = DpiScale(6) * 2 + 8 * 2;                      // Padding(6,10) 的上下 + textPadding 上下
    int wantDy = lines * lineDy + inset;
    if (wantDy == fileBoxLastDy) {
        return;
    }
    fileBoxLastDy = wantDy;
    box->minDy = wantDy;
    DoLayout(HwndClientRect(hwnd).Size()); // 用新高度重排一次
}

void DownloadProgressWnd::OnGetMinMaxInfo(WindowBase::GetMinMaxInfoEvent* ev) {
    MINMAXINFO* mmi = ev->mmi; // ← 若字段不叫 mmi，改成 WinGui.h 里的实际名字
    mmi->ptMinTrackSize.x = DpiScale(kDlDlgDx);
    mmi->ptMinTrackSize.y = DpiScale(kDlDlgMinDy);
}

void DownloadProgressWnd::OnAnimTick() {
    if (stopped.load() || !circle) {
        return;
    }
    circle->phase = ++animPhase;
    HwndInvalidate(hwnd); // 只重绘即可，无需 DoLayout
}

void DownloadProgressWnd::OnStopClicked(VirtMouseEvent*) {
    if (stopped.load()) {
        return;
    }
    stopped = true;
    gDlPolling = false; // 让轮询线程在下一拍退出
    DashbeamSendStop();
    ReversFileName();
    if (circle) {
        circle->state = DlCircleState::Done;
    }
    btnStopIcon->enabled = false;
    if (titleLabel) {
        titleLabel->SetText(Tr("已停止"));
    }
    RefreshLabels();
    HwndInvalidate(hwnd);
}

void DownloadProgressWnd::OnProgressData(const DownloadProgressData& d) {
    if (stopped.load()) {
        return;
    }
    data = d;
    bool done = (d.total > 0 && d.bytes >= d.total);
    if (circle) {
        circle->state = (d.bytes >= d.total) ? DlCircleState::Done : DlCircleState::Transferring;
    }
    if (done && titleLabel) {
        titleLabel->SetText(Tr("完成"));
        // HwndSetText(hwnd, Tr("完成"));
    }
    RefreshLabels();
    HwndInvalidate(hwnd);
}

void DownloadProgressWnd::RefreshLabels() {
    i64 perc = data.total > 0 ? data.bytes * 100 / data.total : 0;
    // progress->SetCurrent((int)perc);
    bar->frac = (float)perc / 100.f;
    percLabel->SetText(fmt("%d%%", (int)perc));

    float kbps = (float)data.speed / 1024.f;
    float mbps = (float)data.speed / (1024.f * 1024.f);
    speedLabel->SetText(fmt("%.1f %sB/s", (double)mbps > 1.0 ? mbps : kbps, mbps > 1.0 ? StrL("M") : StrL("K")));

    float doneMb = (float)data.bytes / (1024.f * 1024.f);
    float totalMb = (float)data.total / (1024.f * 1024.f);
    sizeLabel->SetText(fmt("%.1f MB / %.1f MB", (double)doneMb, (double)totalMb));

    bool done = data.total > 0 && data.bytes >= data.total;
    if (stopped.load() || done || data.speed <= 0) {
        etaLabel->SetText(StrL("--"));
    } else {
        i64 secs = (i64)((data.total - data.bytes) / data.speed);
        etaLabel->SetText(fmt(Tr("约 %d 秒").s, secs));
    }
    DoLayout(HwndClientRect(hwnd).Size());
    HwndInvalidate(hwnd);
}

void DownloadProgressWnd::RefreshSubtitle() {
    if (!subtitleLabel) {
        return;
    }
    // 例：已连接 2 个 · 已完成 1 个
    TempStr s = str::FormatTemp("已连接 %d 个 · 已完成 %d 个", activeConns, connectedPeers);
    subtitleLabel->SetText(Str(s)); // 运行时变量用 Str(...)，不要 StrL
    DoLayout(HwndClientRect(hwnd).Size());
    HwndInvalidate(hwnd);
}

static void OnCloseClicked(VirtMouseEvent*);
bool DownloadProgressWnd::Create(MainWindow* mainWin) {
    win = mainWin;
    Color colTxt = ThemeWindowTextColor();
    Color colBg = ThemeWindowControlBackgroundColor();
    Color colMuted = ThemeWindowTextDisabledColor();
    // Color colWinBg = MkRgb(0xED, 0xED, 0xED);
    Color colWinBg = MkRgb(0xE0, 0xE0, 0xE0);
    PlatformFont* fontText = DlgUiFont(kToolDlgTextFontPx);
    PlatformFont* fontMeta = DlgUiFont(kToolDlgMetaFontPx);
    SetFont(fontText);
    bool isRtl = IsUIRtl();
    {
        CreateCustomArgs args;
        args.owner = win ? win->hwndFrame : nullptr;
        args.title = Tr("正在分享中");
        args.visible = false;
        // args.style = WS_POPUPWINDOW | WS_CAPTION | WS_THICKFRAME;
        // 方案 B 的 WM_NCCALCSIZE ：只有三边
        // args.style = WS_POPUP | WS_THICKFRAME;
        // onWndProc = MkMethod1<DownloadProgressWnd, WindowBase::WndProcEvent*, &DownloadProgressWnd::OnWndProc>(this);

        // 完全无四边框
        args.style = WS_POPUP;
        args.font = fontText;
        args.isRtl = isRtl;
        args.bgColor = colWinBg;
        args.icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
        CreateCustom(args);
    }
    if (!hwnd) {
        return false;
    }
    SetColors(colTxt, colWinBg);

    // 标题栏底色（COLORREF，0x00BBGGRR）。Color 本仓库就是 COLORREF 布局
    COLORREF capBg = (COLORREF)colWinBg;
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &capBg, sizeof(capBg));
    // 可选：标题文字色
    COLORREF capTxt = (COLORREF)colTxt;
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &capTxt, sizeof(capTxt));

    onGetMinMaxInfo =
        MkMethod1<DownloadProgressWnd, WindowBase::GetMinMaxInfoEvent*, &DownloadProgressWnd::OnGetMinMaxInfo>(this);
    onSize = MkMethod1<DownloadProgressWnd, WindowBase::SizeEvent*, &DownloadProgressWnd::OnSize>(this);

    auto* vbox = new VBox();
    vbox->alignCross = CrossAxisAlign::Stretch;

    HBox* capBar = new HBox();
    capBar->alignCross = CrossAxisAlign::CrossCenter;
    capBar->minDy = DpiScale(16);
    capTitle = NewVirtText({.s = Tr("正在分享中"), .font = fontText, .textColor = colTxt});
    capBar->AddChild(capTitle, 1); // flex=1 占满中间，空白处可拖动
    capCloseBtn = new VirtCloseButton();
    ApplyCloseButtonDpi(capCloseBtn, DpiGet()); // 现成的 DPI 尺寸设置
    capCloseBtn->onClick = MkFunc1Void<VirtMouseEvent*>(OnCloseClicked);
    // 或直接关：MkFunc1Void<VirtMouseEvent*>([](VirtMouseEvent*){ if (gDownloadWnd) gDownloadWnd->ScheduleDelete(); })
    capBar->AddChild(capCloseBtn);
    vbox->AddChild(new Padding(capBar, DpiScaledInsets(8, kToolDlgPadDx)));

    auto* panelBox = new VBox();
    panelBox->alignCross = CrossAxisAlign::Stretch;
    // top row: title left, "停止" right

    HBox* head = NewDlgRow();
    head->rtl = isRtl;
    head->alignCross = CrossAxisAlign::CrossCenter; // 圆点与文字基线对齐
    auto* dot = new StatusDotCtrl();
    // #00BC7D
    // #43D979
    // MkRgb(0x22, 0xC5, 0x5E); // 绿色；用项目实际的 MkColor/ColorFromRGB
    Color deepGreen = MkRgb(0x00, 0xbc, 0x7D);
    Color green = MkRgb(0x43, 0xD9, 0x79);
    dot->col = deepGreen;
    head->AddChild(dot);
    head->AddChild(new Spacer(DpiScale(6), 0)); // 点和文字之间留点间距
    // head->AddChild(new Padding(dot, DpiScaledInsets(0, 0, 0, 6)));
    titleLabel = NewDlgCaption(Tr("正在上传"), fontText, deepGreen);
    titleLabel->padding = {};
    head->AddChild(titleLabel);
    head->AddChild(new Spacer(DpiScale(6), 0)); // 点和文字之间留点间距
    subtitleLabel = NewVirtText({.s = StrL(""), .font = fontMeta, .textColor = green, .isRtl = isRtl});
    // head->AddChild(subtitleLabel);
    head->AddChild(new Spacer(0, 0), 1);

    auto* btnIcon = new IconButtonCtrl();
    int iconSz = DpiScale(20);
    // fg 传文字色，svg 里 fill="currentColor" 会跟随主题
    btnIcon->pix = GetCachedPixmapForSvg(Str(gDlStopSvg), iconSz, iconSz, colTxt, kColorTransparent);
    btnIcon->onClick = MkMethod1<DownloadProgressWnd, VirtMouseEvent*, &DownloadProgressWnd::OnStopClicked>(this);
    btnStopIcon = btnIcon; // struct 里 btnStop 换成 IconButtonCtrl* btnStopIcon
    head->AddChild(btnIcon);
    captionRow = head;
    panelBox->AddChild(new Padding(head, DpiScaledInsets(8, kToolDlgPadDx)));
    panelBox->AddChild(new Padding(subtitleLabel, DpiScaledInsets(0, kToolDlgPadDx)));

    // concentric circles, centered
    circle = new CircleCtrl();
    circle->accent = green;
    circle->bg = colBg;
    auto* circleRow = new Align(circle);
    circleRow->HAlign = AlignCenter;
    panelBox->AddChild(new Padding(circleRow, DpiScaledInsets(10, kToolDlgPadDx, 4, kToolDlgPadDx)));

    // "keep the app open" hint
    auto* hint = NewDlgLabel(Tr("在分享文件时请保持应用打开"), fontText, colMuted, kToolDlgLineDy);
    auto* hintRow = new Align(hint);
    hintRow->HAlign = AlignCenter;
    panelBox->AddChild(new Padding(hintRow, DpiScaledInsets(0, kToolDlgPadDx)));

    auto* hintTicket = NewDlgLabel(Tr("一次性分享票据"), fontText, colMuted, kToolDlgLineDy);
    auto* hintRowTicket = new Align(hintTicket);
    // hintRow->HAlign = AlignCenter;
    panelBox->AddChild(new Padding(hintRowTicket, DpiScaledInsets(0, kToolDlgPadDx)));

    // 内层：无边框 => 直角；只读但可选中；多行自动换行
    Color editBg = AccentColor(colBg, 8); // 与外层区分的背景色
    Edit::CreateArgs eargs;
    eargs.parent = hwnd;
    eargs.font = fontMeta;
    eargs.text = Str(kDlLinkText);
    eargs.isMultiLine = true;
    eargs.withBorder = false; // 直角
    eargs.noScrollBars = true;
    // eargs.idealSizeLines = 5;
    eargs.idealWidthChars = 40;
    eargs.maxWidthChars = 60;
    eargs.isRtl = isRtl;
    eargs.textPadding = 8; // 文本与边缘留白
    linkEdit = new Edit();
    linkEdit->SetColors(colTxt, editBg); // 内层文本框背景
    linkEdit->Create(eargs);
    SendMessageW(linkEdit->hwnd, EM_SETREADONLY, TRUE, 0);

    // 外层圆角容器
    box = new RoundedBoxCtrl();
    box->fill = AccentColor(colBg, 16); // 外层圆角背景（更深/更浅以区分）
    box->border = ThemeWindowTextDisabledColor();
    box->radius = 8;
    box->minDy = 5 * (PlatformFontLineHeight(fontMeta) / DpiScale(1)) / 1 + 20; // 见下方说明

    // 叠放：圆角框铺满，文本框内缩露出圆角
    auto* overlay = new Overlay();
    overlay->AddChild(box, CrossAxisAlign::Stretch, CrossAxisAlign::Stretch);
    overlay->AddChild(new Padding(linkEdit, DpiScaledInsets(6, 10)), CrossAxisAlign::Stretch, CrossAxisAlign::Stretch);

    // panelBox->AddChild(new Padding(overlay, DpiScaledInsets(kToolDlgFilesPadDy, kToolDlgPadDx)));
    panelBox->AddChild(new Padding(overlay, DpiScaledInsets(0, kToolDlgPadDx)));

    HBox* hintBox = new HBox();

    copyIcon = new IconButtonCtrl();
    // fg 传文字色，svg 里 fill="currentColor" 会跟随主题
    copyIcon->baseSize = 24;
    copyIcon->pix = GetCachedPixmapForSvg(Str(gDlgCopy), iconSz, iconSz, colTxt, kColorTransparent);
    copyIcon->onClick = MkMethod1<DownloadProgressWnd, VirtMouseEvent*, &DownloadProgressWnd::OnCopyClicked>(this);
    onTimer = MkMethod1<DownloadProgressWnd, WindowBase::TimerEvent*, &DownloadProgressWnd::OnTimer>(this);
    hintBox->AddChild(copyIcon);

    auto* hintCopy = NewVirtText(
        {.s = Tr("将此票据发送给想要接收您文件的人"), .font = fontMeta, .textColor = colMuted, .isRtl = isRtl});
    // auto* hintRow = new Align(hint);
    // hintRow->HAlign = AlignCenter;
    hintBox->AddChild(hintCopy);
    panelBox->AddChild(new Padding(hintBox, DpiScaledInsets(2, kToolDlgPadDx)));
    head->AddChild(new Spacer(DpiScale(6), 0)); // 点和文字之间留点间距

    // "传输进度" left, percent right
    HBox* progHead = NewDlgRow();
    progHead->rtl = isRtl;
    progHead->AddChild(NewDlgLabel(Tr("传输进度"), fontMeta, colMuted, kToolDlgLineDy));
    progHead->AddChild(new Spacer(0, 0), 1);
    percLabel = NewVirtText({.s = StrL("0%"), .font = fontMeta, .textColor = colMuted, .isRtl = isRtl});
    progHead->AddChild(percLabel);
    panelBox->AddChild(new Padding(progHead, DpiScaledInsets(10, kToolDlgPadDx, 0, kToolDlgPadDx)));

    // native progress bar
    // progress = new Progress();
    // progress->idealDx = DpiScale(kDlDlgDx - 2 * kToolDlgPadDx);
    // progress->idealDy = DpiScale(6);
    // Progress::CreateArgs pargs;
    // pargs.parent = hwnd;
    // pargs.initialMax = 100;
    // pargs.isRtl = isRtl;
    // progress->Create(pargs);
    // progress->SetColors(colTxt, colBg);
    // vbox->AddChild(new Padding(progress, DpiScaledInsets(0, kToolDlgPadDx)));
    bar = new ProgressBarCtrl();
    Color lightBlueB = MkRgb(0x3B, 0x82, 0xF6);
    Color lightBlue = BlendLighter(lightBlueB, 0.35f); // 或用第二个主题色；按喜好定
    Color lightGreen = MkRgb(0x34, 0xD2, 0x9A);
    bar->c1 = lightBlue;
    bar->c2 = lightGreen;
    bar->track = ThemeWindowControlBackgroundColor();
    bar->border = ThemeWindowTextDisabledColor();
    panelBox->AddChild(new Padding(bar, DpiScaledInsets(0, kToolDlgPadDx)));
    // struct 里 progress 字段改为 ProgressBarCtrl* bar;

    // speed left, "x/y" size right (under the percent)
    HBox* statsRow = NewDlgRow();
    statsRow->rtl = isRtl;
    speedLabel = NewVirtText({.s = StrL(""), .font = fontMeta, .textColor = colMuted, .isRtl = isRtl});
    sizeLabel = NewVirtText(
        {.s = StrL(""), .font = fontMeta, .textColor = colMuted, .align = VirtTextAlign::Right, .isRtl = isRtl});
    statsRow->AddChild(speedLabel);
    statsRow->AddChild(new Spacer(0, 0), 1);
    statsRow->AddChild(sizeLabel);
    panelBox->AddChild(new Padding(statsRow, DpiScaledInsets(0, kToolDlgPadDx)));

    // eta
    etaLabel = NewVirtText({.s = StrL(""), .font = fontMeta, .textColor = colMuted, .isRtl = isRtl});
    panelBox->AddChild(new Padding(etaLabel, DpiScaledInsets(0, kToolDlgPadDx)));

    // 灰色圆角背景垫在内容后面
    auto* panelBg = new PanelBgCtrl();
    panelBg->fill = MkRgb(0xff, 0xff, 0xff);
    // MkGray(0xd2); // 比窗口灰(0xe6)略深，才看得出面板
    panelBg->radius = 10;

    auto* panel = new Overlay();
    panel->AddChild(panelBg, CrossAxisAlign::Stretch, CrossAxisAlign::Stretch);
    panel->AddChild(panelBox, CrossAxisAlign::Stretch, CrossAxisAlign::Stretch);

    // 面板整体再留点外边距，让圆角和窗口边有间隙
    // vbox->AddChild(new Padding(panel, DpiScaledInsets(kToolDlgPadDx, kToolDlgPadDx)));
    // 四参数版，top 单独设成你想要的间距（顺序以项目 DpiScaledInsets 定义为准，top 在首位）
    int gapTop = DpiScale(0); // 想更近就调小，想更远就调大
    vbox->AddChild(new Padding(panel, DpiScaledInsets(gapTop, kToolDlgPadDx, kToolDlgPadDx, kToolDlgPadDx)));

    layout = vbox;

    LayoutAndSizeToContent(layout, DpiScale(kDlDlgDx), 0, hwnd);
    DoLayout(HwndClientRect(hwnd).Size());
    HwndCenterDialog(hwnd, win ? win->hwndFrame : nullptr);
    // UpdateTheme();
    UpdateFileBoxHeight();
    SetIsVisible(true);
    RefreshLabels();
    RefreshSubtitle();
    return true;
}

// ---------------------------------------------------------------------------
// simulated source: a for loop on a worker thread, samples posted to the ui
// thread. A real source calls OnProgressData the same way.

// struct DlSampleMsg {
//     DownloadProgressWnd* wnd = nullptr;
//     DownloadProgressData data{};
//     bool isSample = false; // false = 纯动画帧
// };

// runs on the ui thread: deliver one sample
// static void DlDeliverSample(DlSampleMsg* msg) {
//     DownloadProgressWnd* wnd = msg->wnd;
//     if (gDownloadWnd == wnd && !wnd->stopped.load()) {
//         if (msg->isSample) {
//             wnd->OnProgressData(msg->data);
//         } else {
//             wnd->OnAnimTick();
//         }
//     }
//     delete msg;
// }

// runs on a background thread via RunAsync
// static void DlSimulateAsync(DownloadProgressWnd* wnd) {
//     const int framesPerSample = 30; // 30 * 33ms = 1s
//     int n = dimof(gDlSamples);
//     for (int i = 0; i < n; i++) {
//         for (int f = 0; f < framesPerSample; f++) {
//             if (wnd->stopped.load()) {
//                 return;
//             }
//             ::Sleep(33); // one chunk per second
//             bool last = (f == framesPerSample - 1);
//             auto* msg = new DlSampleMsg{wnd, gDlSamples[i], last};
//             auto fn = MkFunc0<DlSampleMsg>(DlDeliverSample, msg);
//             uitask::Post(fn, nullptr);
//         }
//     }
// }

// ---------------------------------------------------------------------------
// open / close

// 从 {"handle","ticket","hash",...} 里取 "ticket":"..." 的值；没有返回 {}
static TempStr ExtractTicket(Str payload) {
    int pos = str::IndexOfAfter(payload, StrL("\"ticket\":\""));
    if (pos < 0) {
        return {};
    }
    // pos += 10; // strlen("\"ticket\":\"")，与原版 pos += 10 一致
    int end = str::IndexOfChar(Str(payload.s + pos, payload.len - pos), '\"');
    if (end < 0) {
        return {};
    }
    logf("(SendMe)ExtractTicket: %s\n", Str(payload.s + pos, end));
    return str::DupTemp(Str(payload.s + pos, end));
}

static void CleanUI(bool failed = false) {
    if (!gDownloadWnd) return;
    if (gDownloadWnd->titleLabel) {
        gDownloadWnd->titleLabel->SetText(failed ? Tr("") : Tr("失败"));
    }
    if (gDownloadWnd->circle) {
        gDownloadWnd->circle->state = DlCircleState::Done;
    }
    gDownloadWnd->data = {};
    gDownloadWnd->RefreshLabels();
    HwndInvalidate(gDownloadWnd->hwnd);
    return;
}

// runs on a background thread via RunAsync
static void DlPollPeersAsync(DownloadProgressWnd* wnd) {
    while (gDlPolling.load()) {
        if (wnd->stopped.load()) {
            break;
        }
        i64 completed = DashbeamSendCompletedPeers(); // DashbeamSend.cpp 的薄封装
        auto* msg = new DlPeersMsg{wnd, completed};
        uitask::Post(MkFunc0<DlPeersMsg>(DlDeliverPeers, msg), nullptr);
        ::Sleep(500); // 轮询间隔，观感/开销折中
    }
}

// called on the UI thread by DashbeamSend.cpp for every dll event
void DownloadProgressNotifyEvent(Str event, Str payload) {
    // if (!gDownloadWnd) {
    //     return;
    // }
    const char* p = payload.s ? payload.s : "";
    if (str::Eq(event, StrL("share-ready"))) {
        // payload: {"handle","ticket","hash","size","entry_type"}
        TempStr ticket = ExtractTicket(payload);
        if (len(ticket) > 0) {
            // 以下 4 行对应原版 printf，用 logf 复刻
            logf("(SendMe)\n=== 分享已就绪 ===\n");
            logf("(SendMe)ticket: %s\n", ticket);
            logf("(SendMe)(完整回执: %s)\n\n", Str(p));
            logf("(SendMe)等待接收端连接...\n");

            // 把 ticket 写入文件框（linkEdit 必须是成员，不能是 Create 里的局部变量）
            if (gDownloadWnd && gDownloadWnd->linkEdit) {
                gDownloadWnd->linkEdit->SetText(ticket);
                // gDownloadWnd->DoLayout(HwndClientRect(gDownloadWnd->hwnd).Size());
                // HwndInvalidate(gDownloadWnd->hwnd);
                gDownloadWnd->Relayout();
            }
        } else {
            logf("(SendMe)[share-ready] %s\n", Str(p));
        }
    } else if (str::Eq(event, StrL("share-error"))) {
        logf("(SendMe)[share-error] 启动分享失败: %s\n", Str(p));
    } else if (str::Eq(event, StrL("share-peer-connected"))) {
        // ++gDownloadWnd->connectedPeers;
        logf("(SendMe)[peer] 新对端连接 (累计 %d): %s\n", gDownloadWnd->connectedPeers, Str(p));
    } else if (str::Eq(event, StrL("transfer-started"))) {
        logf("(SendMe)[transfer] 传输开始\n");
    } else if (str::Eq(event, StrL("transfer-progress"))) {
        // payload = "bytes:total:speed*1000"
        logf("(SendMe)[transfer] 传输进度: %s\n", Str(p));
        // 解析并驱动进度界面（见第 3 节）
        // i64 bytes = 0, total = 0, speedX1000 = 0;
        // int a = str::Find(payload, StrL(":"));
        // int b = a >= 0 ? str::Find(payload, StrL(":"), a + 1) : -1;
        // if (a > 0 && b > a) {
        //     bytes = atoll(Str(payload.s, a).s);
        //     total = atoll(Str(payload.s + a + 1, b - a - 1).s);
        //     speedX1000 = atoll(Str(payload.s + b + 1, payload.len - b - 1).s);
        //     if (gDownloadWnd) {
        //         gDownloadWnd->OnProgressData({bytes, total, speedX1000 / 1000});
        //     }
        // }
        // "bytes:total:speed*1000"
        StrVec parts;
        Split(&parts, payload, StrL(":"));
        if (len(parts) >= 3) {
            DownloadProgressData d;
            d.bytes = (i64)ParseInt64(parts.At(0));
            d.total = (i64)ParseInt64(parts.At(1));
            d.speed = (i64)ParseInt64(parts.At(2)) / 1000; // payload 里是 speed*1000
            if (gDownloadWnd) {
                gDownloadWnd->OnProgressData(d);
            }
        }
    } else if (str::Eq(event, StrL("transfer-completed"))) {
        // payload: {"durationMs","bytes","totalBytes"}
        logf("(SendMe)[transfer] 传输完成: %s\n", Str(p));
    } else if (str::Eq(event, StrL("transfer-failed"))) {
        logf("(SendMe)[transfer] 传输失败: %s\n", Str(p));
        CleanUI();
    } else if (str::Eq(event, StrL("active-connection-count"))) {
        logf("(SendMe)[transfer] 活动连接数: %s\n", Str(p));
        gDownloadWnd->activeConns = (int)ParseInt(Str(p));
        gDownloadWnd->RefreshSubtitle();
    }
}

static void OnCloseClicked(VirtMouseEvent*) {
    if (gDownloadWnd) {
        DashbeamSendStop();
        ReversFileName();
        gDownloadWnd->ScheduleDelete();
    }
}

static void OnClose(WindowBase::CloseEvent* /*ev*/) {
    if (gDownloadWnd) {
        DashbeamSendStop();
        ReversFileName();
        gDownloadWnd->ScheduleDelete();
    }
}

static void OnDestroy(WindowBase::DestroyEvent* /*ev*/) {
    if (gDownloadWnd) {
        DashbeamSendStop();
        ReversFileName();
        gDownloadWnd->ScheduleDelete();
    }
}

bool IsDownloadProgressDialogVisible() {
    return gDownloadWnd != nullptr;
}

void CloseDownloadProgressDialog() {
    ReversFileName();
    if (gDownloadWnd) {
        gDownloadWnd->ScheduleDelete();
    }
}

static void OnDlWndProc(WindowBase::WndProcEvent* ev) {
    if (ev->msg == WM_NCLBUTTONDBLCLK) {
        ev->result = 0;
        ev->didHandle = true;
    }
}

void ShowDownloadProgressDialog(MainWindow* win, StrVec& paths) {
    if (gDownloadWnd) {
        HwndSetFocus(gDownloadWnd->hwnd);
        return;
    }
    if (len(paths) == 0) {
        MsgBox(win ? win->hwndFrame : nullptr, Tr("No open document to share"), Str(kAppName),
               MB_OK | MB_ICONINFORMATION);
        return;
    }
    auto* wnd = new DownloadProgressWnd();
    wnd->closeOnEsc = true;
    wnd->onBeforeDelete = MkFunc0Void(ClearDownloadWnd);
    wnd->onClose = MkFunc1Void<WindowBase::CloseEvent*>(OnClose);
    wnd->onDestroy = MkFunc1Void<WindowBase::DestroyEvent*>(OnDestroy);
    wnd->onNcHitTest =
        MkMethod1<DownloadProgressWnd, WindowBase::NcHitTestEvent*, &DownloadProgressWnd::OnNcHitTest>(wnd);
    wnd->onWndProc = MkFunc1Void<WindowBase::WndProcEvent*>(OnDlWndProc); // 吞双击，见下
    wnd->SetFont(GetAppFont());
    if (!wnd->Create(win)) {
        delete wnd;
        return;
    }

    StrVec fileList;
    SliceFirst(paths, fileList);
    Str basePath = paths.At(0);
    auto* guard = new TempRenameGuard(paths, basePath);
    if (!guard->Enter()) { // 改名失败就别往下走
        delete guard;
        // 可弹框提示
        return;
    }
    wnd->renameGuard = guard;
    const StrVec& pdfPaths = guard->GetPdfAbsPaths();

    gDownloadWnd = wnd;
    // 真实分享：dll 在自己线程回调，经 DownloadProgressNotifyEvent 更新界面
    if (!DashbeamSendStart(pdfPaths, StrL(".pdf"))) { // 按需去掉 strip_suffix
        // 启动失败：让窗口立刻显示"完成/失败"或直接关
        wnd->ScheduleDelete();
    }
    // 启动已完成数轮询
    gDlPolling = true;
    RunAsync(MkFunc0<DownloadProgressWnd>(DlPollPeersAsync, wnd), StrL("DlPollPeersAsync"));
    // modeless: no RunModalWindow, the main window stays enabled
    // auto fn = MkFunc0<DownloadProgressWnd>(DlSimulateAsync, wnd);
    // RunAsync(fn, StrL("DlSimulateAsync"));
}
