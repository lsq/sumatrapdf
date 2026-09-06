[About页面链接](https://deepwiki.com/search/static-void-openhomepagemainwi_e1225500-eb35-400d-b95f-e659aa16a589)

# DeepWiki Q&A with Code Context for Repository: sumatrapdfreader/sumatrapdf
## Q1
static void OpenHomePage(MainWindow* win) {
    // 1. 查找已有的 Home 标签页
    int nTabs = win->TabCount();
    for (int i = 0; i < nTabs; i++) {
        WindowTab* tab = win->GetTab(i);
        if (tab->IsAboutTab()) {
            logf("找到about page!\n");
            win->tabsCtrl->SetSelected(i);
            LoadModelIntoTab(tab);
            return;
        }
    }

    // 2. 没有 Home 标签页，新建一个（镜像 AddTabToWindow 中的逻辑）
    logf("没有找到home page!\n");
    WindowTab* homeTab = new WindowTab(win);
    homeTab->type = WindowTab::Type::About;
    homeTab->canvasRc = win->canvasRc;

    TabInfo* newTabInfo = new TabInfo();
    newTabInfo->text = StrL("Home");
    newTabInfo->tooltip.s = nullptr;
    newTabInfo->isPinned = true;
    newTabInfo->canClose = true;
    newTabInfo->userData = (UINT_PTR)homeTab;

    int insertedIdx = win->tabsCtrl->InsertTab(0, newTabInfo);
    win->tabsCtrl->SetSelected(insertedIdx);
    win->currentTabTemp = homeTab;
    LoadModelIntoTab(homeTab);
    UpdateTabWidth(win);
}
上面代码出现问题：
1. 如果存在系统默认的about页面，执行查找正确
2.如果关闭系统默认的about，新建也正确，再次关闭程序崩溃
请分析原因是什么
### Answer
## Root cause analysis

The custom `OpenHomePage()` bypasses the engine's real tab-creation function `AddTabToWindow()` sumatrapdf:709-736  and hand-rolls the "create a Home tab" logic instead. That hand-rolled path misses bookkeeping that the real close-path (`CloseTab`/`RemoveTab`) depends on, which is why the crash only shows up the *second* time the tab is closed rather than immediately.

### 1. Closing the last tab in a window destroys the whole `MainWindow`, not just the tab

`CloseTab()` treats "only one tab left" as "close the entire window", independent of whether that lone tab is a document or an About/Home tab: sumatrapdf:5416-5421 

So the very first time the default About tab is closed (it's the only tab), the branch taken is `CloseWindow(win, quitIfLast, false)`, not `RemoveTab()`. `CloseWindow` tears down the frame/`tabsCtrl`/associated state of that `MainWindow*`. Any code (like `OpenHomePage`) that continues to hold and reuse that same `MainWindow* win` pointer afterward is operating on a window whose internals (`win->tabsCtrl`, HWNDs, etc.) may already be destroyed or in the process of being destroyed.

### 2. The engine already special-cases a lone About tab, and it recurses into `CloseTab` again

Even in the normal "remove a tab" branch, if the tab that remains after removal is the sole About tab, the code recursively calls `CloseTab()` on it (closing the window if it's the only remaining tab): sumatrapdf:5443-5456 

This confirms that, in the real engine, a "Home-tab-only" window is never meant to be left standing — it's always folded into a `CloseWindow` call. Your `OpenHomePage()` function, by manually re-inserting a `Home` tab via `win->tabsCtrl->InsertTab(0, ...)` after the window's real Home tab was already consumed by this teardown path, is fighting against this invariant: it recreates a tab on a `MainWindow` that the framework considers "should not exist with only a Home tab left."

### 3. `TabInfo`/`WindowTab` are only cleaned up correctly through `RemoveTab()` + `delete tab`

The normal close path does two things in lockstep: `RemoveTab(tab)` (which removes the `TabInfo` from `tabsCtrl` and calls `delete tab` on the `TabInfo`) sumatrapdf:1050-1072 , and then `CloseTab()` separately `delete`s the `WindowTab*` object itself sumatrapdf:5422-5437 . Because `WindowTab`'s destructor nulls out `win->ctrl`/`win->currentTabTemp` if they still point at it sumatrapdf:58-73 , the engine clearly relies on exactly-once, well-ordered destruction of both objects. Your `OpenHomePage()` creates its `homeTab`/`newTabInfo` outside of `AddTabToWindow()`, so it never goes through the same well-defined path — if the window hosting it gets torn down via `CloseWindow` (as in point 1) instead of via `RemoveTab`, there is no code that has been shown to guarantee that this manually-created `WindowTab` gets `delete`d exactly once. That's a classic setup for either a leak (first close "succeeds" silently) or a use-after-free/double-free (second close crashes), which matches your symptom exactly: first close works, subsequent close crashes.

### 4. Missing `win->currentTabTemp` sync and missing `win->tabsCtrl` null-check

In the "found existing tab" branch, `LoadModelIntoTab(tab)` is called without first doing `win->currentTabTemp = tab;`, unlike every real call site that creates/selects a tab (e.g. `AddTabToWindow` callers always set `win->currentTabTemp` right before/after selecting) sumatrapdf:3562-3564 . This can leave `win->currentTabTemp` stale/dangling relative to what `LoadModelIntoTab` expects, another discrepancy from the invariant the engine maintains elsewhere. Additionally, unlike `AddTabToWindow`, `OpenHomePage` never checks `if (!win->tabsCtrl) return;` before calling `win->tabsCtrl->InsertTab(...)` sumatrapdf:709-716 , so if it's ever invoked while the window's tab control is mid-teardown, it dereferences a control that's going/gone away.

## Summary

The crash is caused by `OpenHomePage()` reimplementing "create a Home tab" outside of the sanctioned `AddTabToWindow()`/`RemoveTab()`/`CloseTab()` lifecycle. The real engine treats "window whose only tab is About" as a condition that gets collapsed into `CloseWindow()` (destroying the whole `MainWindow`), and all tab creation/removal assumes pairing through those specific functions to keep `TabInfo`, `WindowTab`, and `win->currentTabTemp`/`win->ctrl` consistent. By manually constructing the tab and inserting it directly into `tabsCtrl`, and by continuing to operate on a `MainWindow*` that may already be mid-destruction, the second close either double-frees the manually-created `WindowTab`/`TabInfo` or touches memory belonging to an already-destroyed `MainWindow`.

I could not verify the exact implementation of `CloseWindow()`, `MainWindow::Tabs()`/`GetTab()`, or `MainWindow`'s destructor within the available searches (I ran out of tool iterations before fetching them), so I can't state with 100% certainty which specific line performs the fatal dereference/double-free — but the structural mismatch between `OpenHomePage`'s hand-rolled tab creation and the invariants enforced by `CloseTab`/`RemoveTab`/`WindowTab::~WindowTab` is clearly established by the code shown above. If you need the precise faulting instruction, a full run with a debugger/crash dump (or a Devin session with repo + terminal access) against `CloseWindow`, `MainWindow::~MainWindow`, and `MainWindow::Tabs()` would confirm it exactly. The fix direction should be to have `OpenHomePage()` call the real `AddTabToWindow()` (which already contains the "create Home tab" logic you copied) instead of duplicating it, so the tab's lifecycle stays consistent with `CloseTab`/`RemoveTab`.
### Citations
**File:** src/Tabs.cpp (L709-736)
```cpp
WindowTab* AddTabToWindow(MainWindow* win, WindowTab* tab, bool deferUpdate) {
    ReportIf(!win);
    if (!win) {
        return nullptr;
    }
    if (!win->tabsCtrl) {
        return nullptr;
    }

    auto* tabs = win->tabsCtrl;
    int idx = win->TabCount();
    bool useTabs = SettingsUseTabs();
    bool noHomeTab = gGlobalPrefs->noHomeTab;
    bool createHomeTab = useTabs && !noHomeTab && (idx == 0);
    if (createHomeTab) {
        WindowTab* homeTab = new WindowTab(win);
        homeTab->type = WindowTab::Type::About;
        homeTab->canvasRc = win->canvasRc;
        TabInfo* newTab = new TabInfo();
        newTab->text = str::Dup(StrL("Home"));
        newTab->tooltip = nullptr;
        newTab->isPinned = true;
        newTab->canClose = true;
        newTab->userData = (UINT_PTR)homeTab;
        int insertedIdx = tabs->InsertTab(idx, newTab, !deferUpdate);
        ReportIf(insertedIdx != 0);
        idx++;
    }
```
**File:** src/SumatraPDF.cpp (L3562-3564)
```cpp
        AddTabToWindow(win, tab);
        // like the successful path does: the new tab is the current one
        win->currentTabTemp = tab;
```
**File:** src/SumatraPDF.cpp (L5416-5421)
```cpp
    int tabCount = win->TabCount();
    if (tabCount == 1 || (tabCount == 0 && quitIfLast)) {
        if (CanCloseWindow(win)) {
            CloseWindow(win, quitIfLast, false);
            return;
        }
```
**File:** src/SumatraPDF.cpp (L5422-5437)
```cpp
    } else {
        ReportIf(gPluginMode && !gWindows.Contains(win));
        RemoveTab(tab);
        // RemoveTab -> LoadModelIntoTab can pump messages, potentially destroying win
        // and its cbHandler. Since tab was already removed from win's tab list,
        // ~MainWindow won't delete it, so we must delete it here.
        // Null out cb to prevent dangling pointer access in ~DisplayModel.
        if (!IsMainWindowValid(win)) {
            if (tab->ctrl) {
                tab->ctrl->cb = nullptr;
            }
            delete tab;
            return;
        }
        delete tab;
    }
```
**File:** src/SumatraPDF.cpp (L5443-5456)
```cpp
    tabCount = win->TabCount();
    WindowTab* lastTab = (tabCount == 1) ? win->GetTab(0) : nullptr;
    if (lastTab && lastTab->type == WindowTab::Type::About) {
        // showing only home page tab so remove it
        // if there are other windows, close this one
        if (len(gWindows) > 1) {
            CloseWindow(win, false, false);
        } else {
            tab = win->GetTab(0);
            // re-use quitIfLast logic
            CloseTab(tab, quitIfLast);
            return;
        }
    }
```
**File:** src/gui/win/TabsCtrl.cpp (L1050-1072)
```cpp
UINT_PTR TabsCtrl::RemoveTab(int idx) {
    ReportIf(idx < 0);
    ReportIf(idx >= TabCount());
    int selectedTab = selectedIdx;
    TabInfo* tab = tabs[idx];
    UINT_PTR userData = tab->userData;
    tabs.RemoveAt(idx);
    delete tab;
    RebuildTabCtrls();
    if (TabCount() > 0 && selectedTab >= 0) {
        if (idx < selectedTab) {
            selectedTab--;
        } else if (idx == selectedTab) {
            selectedTab = 0;
        }
        SetSelected(selectedTab);
    } else {
        selectedIdx = -1;
    }
    LayoutTabs();
    TabsCtrlUpdateAfterChangingTabsCount(this);
    return userData;
}
```
**File:** src/WindowTab.cpp (L58-73)
```cpp
WindowTab::~WindowTab() {
    logf("~WindowTab: 0x%p, dm: 0x%p\n", this, AsFixed());
    // whatever a close path forgot, nothing may be left pointing at a tab that
    // is going away (the read-aloud playback bar holds one)
    ReadAloudForgetTab(this);
    // Drop MainWindow pointers into this tab / its controller before we free
    // them: DestroyWindow during WebView teardown can re-enter the canvas
    // WndProc, which reads win->ctrl / CurrentTab().
    if (win) {
        if (win->ctrl == ctrl) {
            win->ctrl = nullptr;
        }
        if (win->currentTabTemp == this) {
            win->currentTabTemp = nullptr;
        }
    }
```
## Q2
请修改代码使用AddTabToWindow
### Answer
This is a code implementation task, but since I'm operating in ask-only mode I can't write and hand off the actual patch — I'll explain conceptually what the fix looks like instead.

## Conceptual fix

`AddTabToWindow()` already contains exactly the "create a Home tab" logic that `OpenHomePage()` duplicated by hand (`createHomeTab` block) sumatrapdf:709-736 . Its signature is:

```cpp
WindowTab* AddTabToWindow(MainWindow* win, WindowTab* tab, bool deferUpdate);
```

It expects a `WindowTab*` you've already created (with `type` etc. set) but have *not* yet inserted into `win->tabsCtrl`; it does the `TabInfo` creation, `InsertTab`, and (if `createHomeTab` triggers) also auto-inserts a preceding Home tab — but that auto-Home-tab logic only fires when `idx == win->TabCount() == 0`, i.e. when the window currently has zero tabs.

For `OpenHomePage()`'s "not found" branch, the intent is different: you want to *directly insert a single About/Home tab* into a window that may already have other tabs open (so `idx` won't be `0` and the `createHomeTab` branch won't trigger — that's fine, since you don't want the "auto second Home tab" behavior, you just want `AddTabToWindow` to insert the one `WindowTab` you give it).

Conceptually, `OpenHomePage()`'s "create" branch should become:

```cpp
WindowTab* homeTab = new WindowTab(win);
homeTab->type = WindowTab::Type::About;
// let AddTabToWindow do canvasRc, TabInfo creation, InsertTab, SetSelected, UpdateTabWidth
AddTabToWindow(win, homeTab, false /* deferUpdate */);
win->currentTabTemp = homeTab;
LoadModelIntoTab(homeTab);
```

Note this deliberately does **not** call `win->tabsCtrl->InsertTab()` manually, does not build `TabInfo` manually, and does not call `UpdateTabWidth()` manually — `AddTabToWindow(..., deferUpdate=false)` already does `tabs->SetSelected(insertedIdx)` and `UpdateTabWidth(win)` internally sumatrapdf:749-754 . Duplicating those calls is not just redundant but was part of the original bug: your hand-built `TabInfo`/`WindowTab` pair never flowed through the single function the rest of the codebase (`Favorites.cpp`, `SumatraStartup.cpp`, `SumatraPDF.cpp`) uses to create tabs, so it wasn't guaranteed to be wired up (selection state, tab width, tooltip formatting, etc.) the same way, and the close path (`CloseTab`/`RemoveTab`) assumes tabs were created that way.

This does **not** by itself fix the deeper issue from the previous analysis — that closing the sole tab in a window collapses into `CloseWindow()`, destroying `win` itself sumatrapdf:5416-5421 . If `OpenHomePage()` is invoked with a `win` pointer that might already be mid-teardown (e.g., called from a path that runs during/after `CloseWindow`), switching to `AddTabToWindow()` won't help, because `AddTabToWindow()` itself guards only against `win == nullptr` / `win->tabsCtrl == nullptr` sumatrapdf:710-716  — it doesn't know whether `win` is a dangling/half-destroyed object. You still need to confirm at the call site of `OpenHomePage()` that `win` is a live, fully-initialized `MainWindow` (e.g. checked via `IsMainWindowValid(win)`, the same helper used in the close path sumatrapdf:5422-5437 ) before calling it.

## What I could not verify

I don't have visibility into the exact call site(s) that invoke `OpenHomePage()`, nor the full `IsMainWindowValid()` implementation, so I can't confirm whether the `win` pointer reaching `OpenHomePage()` after the "close default About tab" scenario is guaranteed valid. A Devin session with terminal/build access could apply this change and verify against the actual repro (close default About tab → reopen Home → close again) to confirm the crash is resolved, since ask-only mode here doesn't let me edit files or run the build.
### Citations
**File:** src/Tabs.cpp (L709-736)
```cpp
WindowTab* AddTabToWindow(MainWindow* win, WindowTab* tab, bool deferUpdate) {
    ReportIf(!win);
    if (!win) {
        return nullptr;
    }
    if (!win->tabsCtrl) {
        return nullptr;
    }

    auto* tabs = win->tabsCtrl;
    int idx = win->TabCount();
    bool useTabs = SettingsUseTabs();
    bool noHomeTab = gGlobalPrefs->noHomeTab;
    bool createHomeTab = useTabs && !noHomeTab && (idx == 0);
    if (createHomeTab) {
        WindowTab* homeTab = new WindowTab(win);
        homeTab->type = WindowTab::Type::About;
        homeTab->canvasRc = win->canvasRc;
        TabInfo* newTab = new TabInfo();
        newTab->text = str::Dup(StrL("Home"));
        newTab->tooltip = nullptr;
        newTab->isPinned = true;
        newTab->canClose = true;
        newTab->userData = (UINT_PTR)homeTab;
        int insertedIdx = tabs->InsertTab(idx, newTab, !deferUpdate);
        ReportIf(insertedIdx != 0);
        idx++;
    }
```
**File:** src/Tabs.cpp (L749-754)
```cpp
    int insertedIdx = tabs->InsertTab(idx, newTab, !deferUpdate);
    ReportIf(insertedIdx == -1);
    if (!deferUpdate) {
        tabs->SetSelected(insertedIdx);
        UpdateTabWidth(win);
    }
```
**File:** src/SumatraPDF.cpp (L5416-5421)
```cpp
    int tabCount = win->TabCount();
    if (tabCount == 1 || (tabCount == 0 && quitIfLast)) {
        if (CanCloseWindow(win)) {
            CloseWindow(win, quitIfLast, false);
            return;
        }
```
**File:** src/SumatraPDF.cpp (L5422-5437)
```cpp
    } else {
        ReportIf(gPluginMode && !gWindows.Contains(win));
        RemoveTab(tab);
        // RemoveTab -> LoadModelIntoTab can pump messages, potentially destroying win
        // and its cbHandler. Since tab was already removed from win's tab list,
        // ~MainWindow won't delete it, so we must delete it here.
        // Null out cb to prevent dangling pointer access in ~DisplayModel.
        if (!IsMainWindowValid(win)) {
            if (tab->ctrl) {
                tab->ctrl->cb = nullptr;
            }
            delete tab;
            return;
        }
        delete tab;
    }
```
## Q3
上面逻辑可不可以不创建，而是重新打开HomeTab
### Answer
可以,而且这比手动创建新 tab 更符合代码库既有设计。SumatraPDF 本身并没有单独维护一个"关闭的 Home tab"的历史记录/可恢复对象,所谓"重新打开 Home tab"实际上就是官方文档里说的 `Ctrl+K` → `Show Start Page`,其底层逻辑也是"如果没有 Home tab 就新建一个",而"新建"这一步骤应始终统一走 `AddTabToWindow()`,而不是重新发明。

## 结论:两种写法本质相同,但通过 `AddTabToWindow` 更安全

无论叫"创建"还是"重新打开",在代码层面都只有两种可能:

1. **找到已存在的 About tab 并选中它**——这部分 `OpenHomePage()` 原逻辑(遍历 `win->TabCount()`,用 `tab->IsAboutTab()` 判断)是合理的,可以保留。
2. **不存在时新建一个**——这一步没有"恢复历史 tab"的概念,`WindowTab` 对象在 `RemoveTab`+`delete tab` 之后已经被销毁 sumatrapdf:5422-5437 ,不可能"重新打开"同一个对象,只能创建一个新的、类型为 `WindowTab::Type::About` 的 tab。所以"重新打开"在实现上仍然是"新建"，只是产品语义上称为"重新打开"。

因此正确做法依然是让"新建"分支调用 `AddTabToWindow(win, homeTab, false)`,而不是像原代码那样手写 `TabInfo`/`InsertTab`/`SetSelected`/`UpdateTabWidth`。

## 需要注意的边界情况

`AddTabToWindow()` 内部有一个"自动加 Home tab"的特殊逻辑:当 `idx == win->TabCount() == 0`(即窗口当前没有任何 tab)且 `SettingsUseTabs()` 为真、`gGlobalPrefs->noHomeTab` 为假时,它会自动在你传入的 tab **之前**再插入一个 Home tab sumatrapdf:718-736 。

这意味着如果 `OpenHomePage()` 在窗口 tab 数为 0 时调用 `AddTabToWindow(win, homeTab, ...)`,并且 `homeTab->type` 本身就是 `About`,则会出现"自动插入的 Home tab" + "你手动传入的 About tab"两个 Home tab 同时存在的问题。所以需要按窗口当前状态区分:

- 如果 `win->TabCount() == 0`:直接依赖 `AddTabToWindow` 的 `createHomeTab` 分支自动创建 Home tab 即可,不需要自己再 `new WindowTab` 传进去(或者传一个非 About 类型的占位——但更简单的做法是此时不调用 `OpenHomePage` 的新建逻辑,而是走应用启动/新建窗口时已有的路径)。
- 如果 `win->TabCount() > 0`(有其他文档 tab,只是 Home tab 被关掉了):此时 `idx != 0`,`createHomeTab` 分支不会触发,你可以放心地 `new WindowTab(win)` 设置 `type = WindowTab::Type::About` 后传给 `AddTabToWindow(win, homeTab, false)`,它只会插入你这一个 tab。这正是原问题描述的"关闭默认 About 页面后新建"的场景。

## 仍然存在、未被这次改动解决的问题

之前分析过的"整窗口被 `CloseWindow` 销毁"的场景 sumatrapdf:5416-5421  与是否调用 `AddTabToWindow` 无关——如果 `OpenHomePage()` 拿到的 `win` 指针本身已经在关闭流程中失效,调用 `AddTabToWindow` 前应该先用 `IsMainWindowValid(win)`(同 `CloseTab` 中的用法 sumatrapdf:5407-5409 )校验一次。

## 未能验证的部分

我没能在检索范围内定位到"`Ctrl+K` → `Show Start Page`"命令具体对应的源码函数名(可能是 `CmdShowStartPage` 一类,但在 `Commands.cpp`/`CommandPaletteCollect.cpp` 的搜索中未能确认其确切实现),因此无法直接给出"官方的重新打开 Home tab"函数供你复用对照。如果需要精确定位该命令的处理函数并据此重构 `OpenHomePage()`,建议启动一个具备完整代码库访问权限的 Devin 会话，全文搜索 `Cmd` 前缀命令表和 `CommandPaletteCollect.cpp`/`Commands.cpp` 中与"Home"/"About"相关的命令分发逻辑加以确认。
### Citations
**File:** src/SumatraPDF.cpp (L5407-5409)
```cpp
    if (!IsMainWindowValid(win)) {
        return;
    }
```
**File:** src/SumatraPDF.cpp (L5416-5421)
```cpp
    int tabCount = win->TabCount();
    if (tabCount == 1 || (tabCount == 0 && quitIfLast)) {
        if (CanCloseWindow(win)) {
            CloseWindow(win, quitIfLast, false);
            return;
        }
```
**File:** src/SumatraPDF.cpp (L5422-5437)
```cpp
    } else {
        ReportIf(gPluginMode && !gWindows.Contains(win));
        RemoveTab(tab);
        // RemoveTab -> LoadModelIntoTab can pump messages, potentially destroying win
        // and its cbHandler. Since tab was already removed from win's tab list,
        // ~MainWindow won't delete it, so we must delete it here.
        // Null out cb to prevent dangling pointer access in ~DisplayModel.
        if (!IsMainWindowValid(win)) {
            if (tab->ctrl) {
                tab->ctrl->cb = nullptr;
            }
            delete tab;
            return;
        }
        delete tab;
    }
```
**File:** src/Tabs.cpp (L718-736)
```cpp
    auto* tabs = win->tabsCtrl;
    int idx = win->TabCount();
    bool useTabs = SettingsUseTabs();
    bool noHomeTab = gGlobalPrefs->noHomeTab;
    bool createHomeTab = useTabs && !noHomeTab && (idx == 0);
    if (createHomeTab) {
        WindowTab* homeTab = new WindowTab(win);
        homeTab->type = WindowTab::Type::About;
        homeTab->canvasRc = win->canvasRc;
        TabInfo* newTab = new TabInfo();
        newTab->text = str::Dup(StrL("Home"));
        newTab->tooltip = nullptr;
        newTab->isPinned = true;
        newTab->canClose = true;
        newTab->userData = (UINT_PTR)homeTab;
        int insertedIdx = tabs->InsertTab(idx, newTab, !deferUpdate);
        ReportIf(insertedIdx != 0);
        idx++;
    }
```
## Q4
AddTabToWindow怎么调整位置在第一位置
### Answer
`AddTabToWindow()` 本身并不提供"插入到第一个位置"的参数——它内部固定用 `int idx = win->TabCount();` 计算插入位置 sumatrapdf:718-719 ，即永远是"追加到最后一个 tab 之后"。唯一的例外是函数开头那段"自动创建 Home tab"的特殊分支，它是硬编码调用 `tabs->InsertTab(idx, newTab, ...)`（此时 `idx` 恰好是 `0`，因为只在 `win->TabCount() == 0` 时触发） sumatrapdf:722-735 ，而不是走后面统一的"追加"逻辑。

## 想让 Home tab 固定排在第一位，有两种思路

**思路一：直接调用底层的 `tabs->InsertTab(0, newTab, ...)`，绕过 `AddTabToWindow` 的自动追加逻辑**

`AddTabToWindow` 内部的 `createHomeTab` 分支实际上就是这么做的——它不经过函数末尾统一的 `int insertedIdx = tabs->InsertTab(idx, newTab, !deferUpdate);`（这里 `idx` 已经是 `win->TabCount()`，即末尾），而是单独、提前用 `tabs->InsertTab(idx /* =0 时 */, newTab, !deferUpdate)` 把 Home tab 插到最前面 sumatrapdf:733-734 。

如果你想让 `OpenHomePage()` 的"新建 Home tab"分支也始终排在第一位（而不是追加到末尾），最贴近现有设计的做法是：不调用 `AddTabToWindow()`，而是模仿这段 `createHomeTab` 分支的写法，自己构造 `TabInfo`，然后调用 `win->tabsCtrl->InsertTab(0, newTab, true)`，再手动调用 `tabs->SetSelected(0)` 和 `UpdateTabWidth(win)`（对照 `AddTabToWindow` 末尾在 `!deferUpdate` 时做的事情 sumatrapdf:749-754 ）。这本质上就是把之前那段被批评为"手写、绕开 `AddTabToWindow`"的旧代码，改成与 `AddTabToWindow` 内部这段特权分支完全一致的写法——只是不再重复发明，而是复制这段官方已有的、专门为"Home tab 插入到 0 号位置"设计的逻辑。

**思路二：修改 `AddTabToWindow()` 本身，增加一个可选的插入位置参数**

例如把签名改成 `WindowTab* AddTabToWindow(MainWindow* win, WindowTab* tab, bool deferUpdate, int insertAtIdx = -1)`，当 `insertAtIdx >= 0` 时用它代替 `int idx = win->TabCount();`。这样所有调用方（`SumatraPDF.cpp`、`SumatraStartup.cpp`、`Favorites.cpp` 等现有调用点）保持默认行为不变，只有 `OpenHomePage()` 传入 `0` 来强制插到第一位。这种做法的好处是统一走一个函数、不重复造轮子；代价是要改动一个被多处调用的公共函数签名，需要确认所有调用点都兼容。

## 未能验证的部分

由于工具调用次数已用尽，我没能读取 `TabsCtrl::InsertTab()`（在 `src/gui/win/TabsCtrl.cpp`）和 `TabsCtrl.h` 中的具体签名与实现细节，因此无法百分之百确认 `InsertTab(idx, ...)` 在 `idx` 之外是否还有其他隐含约束（比如是否要求 `idx` 必须 ≤ 当前 tab 数、插入后是否会自动调整 pinned 状态排序等）。如果要实际改动签名或新增参数，建议开一个具备完整代码库和构建环境的 Devin 会话去确认 `TabsCtrl::InsertTab` 的完整行为并验证改动。
### Citations
**File:** src/Tabs.cpp (L718-719)
```cpp
    auto* tabs = win->tabsCtrl;
    int idx = win->TabCount();
```
**File:** src/Tabs.cpp (L722-735)
```cpp
    bool createHomeTab = useTabs && !noHomeTab && (idx == 0);
    if (createHomeTab) {
        WindowTab* homeTab = new WindowTab(win);
        homeTab->type = WindowTab::Type::About;
        homeTab->canvasRc = win->canvasRc;
        TabInfo* newTab = new TabInfo();
        newTab->text = str::Dup(StrL("Home"));
        newTab->tooltip = nullptr;
        newTab->isPinned = true;
        newTab->canClose = true;
        newTab->userData = (UINT_PTR)homeTab;
        int insertedIdx = tabs->InsertTab(idx, newTab, !deferUpdate);
        ReportIf(insertedIdx != 0);
        idx++;
```
**File:** src/Tabs.cpp (L749-754)
```cpp
    int insertedIdx = tabs->InsertTab(idx, newTab, !deferUpdate);
    ReportIf(insertedIdx == -1);
    if (!deferUpdate) {
        tabs->SetSelected(insertedIdx);
        UpdateTabWidth(win);
    }
```
## Q5
函数内部str::Build变量是不是不能使用ToStr作为返回值
### Answer
`str::Builder` 里的 `ToStr()` 返回的是**指向 `Builder` 内部缓冲区的视图（view）**，不拥有内存所有权，所以如果 `Builder` 是函数内的局部变量，用 `ToStr(builder)` 作为函数返回值是**不安全的**，本质上等同于返回一个悬挂指针。

## 原因

`ToStr(const str::Builder& b)` 的实现只是把 `Builder` 内部的指针和长度包一层 `Str`，并没有拷贝数据：

```cpp
Str ToStr(const str::Builder& b) {
    return Str(b.els, (int)b.len);
}
``` sumatrapdf:2697-2699 

注释里也明确说明了这一点：`ToStr()` 只是"返回 `{ptr,len}` 视图"，而 `ToStrTemp()`/`ToCStr()` 才是真正拥有/持久化数据的版本：

```cpp
// ToStr() returns a {ptr,len} view (may contain embedded NULs).
// ToCStr() returns the NUL-terminated buffer, ...
``` sumatrapdf:2692-2696 

而 `str::Builder` 是一个 RAII 对象，其内部缓冲 `els` 要么指向外部传入的 `buf`（栈/临时区），要么是 `Builder` 自己 `new`/`realloc` 出来的堆内存，析构函数 `~Builder()` 会释放它 sumatrapdf:233-253 。如果 `Builder` 是函数内的局部变量：

```cpp
Str MyFunc() {
    str::Builder b;
    b.Append("foo");
    return ToStr(b); // 危险！
}
```

函数返回时 `b` 立即析构，`b.els` 指向的内存被释放（或栈内存失效），而你返回的 `Str` 里的 `.s` 指针仍然指向这块已经失效的内存——调用方一旦使用这个 `Str` 就是 use-after-free/读取野指针。

## 正确做法

代码库里针对这种"要把 `Builder` 的内容带出函数"的场景，提供了两种安全的替代方案：

1. **`ToStrTemp(b)`**：在临时内存区（temp arena）里复制一份数据再返回，生命周期由 arena 管理，可以安全地跨函数边界使用：
```cpp
NO_INLINE TempStr ToStrTemp(const str::Builder& b) {
    return str::DupTemp(ToStr(b));
}
``` sumatrapdf:2701-2705 

2. **`b.TakeStr()`**：把 `Builder` 内部缓冲的所有权转移出来（必要时先复制脱离外部 scratch buf），返回一个独立、调用方需要自行管理生命周期（或用 arena 管理）的 `Str`：
```cpp
Str str::Builder::TakeStr() {
    ...
    Reset();
    return Str(res, n);
}
``` sumatrapdf:1756-1774 

## 结论

- 如果 `Builder` 是函数局部变量，**不要**直接 `return ToStr(builder);`，因为它只是一个不拥有数据的视图，`Builder` 析构后指针失效。
- 应该用 `return ToStrTemp(builder);`（依赖 temp arena 生命周期，SumatraPDF 里大量代码这么用）或者 `return builder.TakeStr();`（转移所有权，调用方负责后续释放/管理）。
- 只有当 `Builder` 本身的生命周期覆盖了返回值的使用范围时（例如 `Builder` 是调用者传入的引用/指针，或是类的成员变量，函数只是临时用一下），直接用 `ToStr()` 才是安全的。 sumatrapdf:351-356
### Citations
**File:** src/base/Str.cpp (L1756-1774)
```cpp
Str str::Builder::TakeStr() {
    int n = len;
    char* res = els;
    if (!els || n == 0) {
        Reset();
        return Str{};
    }
    if (buf.s && els == buf.s) {
        // data is in the external buffer, so we have to duplicate it
        res = (char*)MemDup(this->a, els, (size_t)n + kPadding);
        els = buf.s;
    } else {
        // we're returning the heap allocation; rebind to external if any
        els = buf.s;
    }

    Reset();
    return Str(res, n);
}
```
**File:** src/base/Str.cpp (L2692-2696)
```cpp
// handles embedded 0 in the string
// str::Builder/wstr::Builder always keep their data NUL-terminated.
// ToStr() returns a {ptr,len} view (may contain embedded NULs).
// ToCStr() returns the NUL-terminated buffer, for passing to C/win32 code we
// don't control that expects a zero-terminated char*/WCHAR*.
```
**File:** src/base/Str.cpp (L2697-2699)
```cpp
Str ToStr(const str::Builder& b) {
    return Str(b.els, (int)b.len);
}
```
**File:** src/base/Str.cpp (L2701-2705)
```cpp
// NO_INLINE: this is called in many places; keeping it out of line trims code size
// owning temp-arena copy of the builder's content (unlike ToStr()'s view)
NO_INLINE TempStr ToStrTemp(const str::Builder& b) {
    return str::DupTemp(ToStr(b));
}
```
**File:** src/base/Str.h (L233-253)
```text
struct Builder {
    // arena is not owned by Builder and must outlive it
    Arena* a = nullptr;
    char* els = nullptr;
    int len = 0;
    int cap = 0;
    // Optional external scratch (not owned; default empty = allocate on first use).
    // When set, used while needed+NUL fits in buf.len; growth copies to heap/arena.
    Str buf;

    int nReallocs = 0;

    // externalBuf: optional scratch (not owned), e.g. stack or temp-arena memory.
    // Set .a after construction if heap allocs should use an arena.
    explicit Builder(Str externalBuf = {});
    explicit Builder(int capHint);
    // the implicit memberwise copy would alias els and double-free it
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;

    ~Builder();
```
**File:** src/base/Str.h (L351-356)
```text
Str ToStr(const str::Builder&);
char* ToCStr(const str::Builder&);
WStr ToWStr(const wstr::Builder&);
WCHAR* ToWCStr(const wstr::Builder&);

TempStr ToStrTemp(const str::Builder&);
```
## Q6
struct JsonStructBuilder {
    JsonStruct *jsStruct;
    const StructInfo* rootInfo;
    u8* rootBase;
    Str data;
    Vec<ArraySlot> slots;

    JsonStructBuilder(JsonStruct* strct, Str jsonData) : jsStruct(strct), data(jsonData) {}

    ~JsonStructBuilder() {
        for (int i = 0; i < len(slots); i++) {
            str::Free(slots[i].prefix);
        }
    }

    // 核心：根据路径找到 (base, StructInfo, fieldIdx)，写入值
    bool Visit() {
        // 从根开始递归解析路径
        json::Parse(data, MkFunc1(+[](JsonStruct* args, json::Value* v) {,VisitPath( args, v);}, jsStruct);
        return true;
    }

    void VisitPath(JsonStruct* jt, json::Value* v) {
 这段代码MkFunc1为什么会报错“reference to non-static member function must be called”
### Answer
This is exactly the pattern the codebase already uses for calling a member function through `json::Parse`: it uses `MkMethod1`, not `MkFunc1`, for member functions:

```cpp
static bool ParseVerify(Str json, JsonVerifier* v) {
    return json::Parse(json, MkMethod1<JsonVerifier, json::Value*, &JsonVerifier::OnValue>(v));
}
``` sumatrapdf:37-39 

## Why `MkFunc1` + `VisitPath` fails

`json::Parse` expects a `json::VisitFn`, which is just `Func1<Value*>` — `using VisitFn = Func1<Value*>;` — and its signature is `bool Parse(Str data, const VisitFn& onValue);` sumatrapdf:33-35 

`MkFunc1` is meant to wrap a **free/static function pointer** plus a raw user-data pointer:

```cpp
template <typename T1, typename T2>
Func1<T2> MkFunc1(void (*fn)(T1*, T2), T1* d) {
    auto res = Func1<T2>{};
    using fptr = void (*)(void*, T2);
    res.fn = (fptr)fn;
    res.SetData((void*)d, false);
    return res;
}
``` sumatrapdf:786-793 

Its first argument must be a plain `void(*)(T1*, T2)` — i.e. something with **no implicit `this`**. That's exactly why your code writes `+[](JsonStruct* args, json::Value* v) { ... }`: the unary `+` forces the capture-less lambda to decay into a raw function pointer of that type.

But inside that lambda body you call `VisitPath(args, v)`. `VisitPath` is declared as:

```cpp
void VisitPath(JsonStruct* jt, json::Value* v) {
```

— a **non-static member function of `JsonStructBuilder`**. Because the lambda is capture-less (required for the `+` conversion to a plain function pointer), it has **no `this`** available. When the compiler performs unqualified name lookup for the bare identifier `VisitPath` inside that lambda, it still finds `JsonStructBuilder::VisitPath` (since the lambda is textually nested inside a `JsonStructBuilder` member function, `Visit()`), but a non-static member function can't be referenced/invoked without binding it to an object first. That's precisely the compiler's complaint: **"reference to non-static member function must be called"** — it's telling you that `VisitPath` names a member function, and you can't just call it like a free function (`VisitPath(args, v)`); it needs an object, e.g. `someObj->VisitPath(...)` or `someObj.VisitPath(...)`.

Additional structural mismatch: even if you fix the qualification, `args` in the lambda is typed `JsonStruct*` (matching the `jsStruct` you pass as `d` to `MkFunc1`), not `JsonStructBuilder*`, and `VisitPath(JsonStruct* jt, json::Value* v)` takes 2 explicit params in addition to the implicit `this` — so there's no `JsonStructBuilder*` instance in scope inside the lambda at all to call `VisitPath` on.

## How the codebase actually solves this

For calling a member function as the visitor callback, use `MkMethod1`, which builds a trampoline that supplies `this` for you:

```cpp
template <typename T, typename TArg, void (T::*Method)(TArg)>
static void MethodTrampoline1(void* obj, TArg arg) {
    (static_cast<T*>(obj)->*Method)(arg);
}

template <typename T, typename TArg, void (T::*Method)(TArg)>
Func1<TArg> MkMethod1(T* obj) {
    auto res = Func1<TArg>{};
    using fptr = void (*)(void*, TArg);
    res.fn = (fptr)&MethodTrampoline1<T, TArg, Method>;
    res.SetData((void*)obj, false);
    return res;
}
``` sumatrapdf:772-784 

Concretely, your `Visit()` should look like:

```cpp
bool Visit() {
    return json::Parse(data, MkMethod1<JsonStructBuilder, json::Value*, &JsonStructBuilder::VisitPath>(this));
}
```

and `VisitPath` should be a single-argument member function matching `void VisitPath(json::Value* v)` (matching `VisitFn = Func1<Value*>`, which calls `fn(void*, Value*)` — see `JsonVerifier::OnValue(json::Value* v)` used the same way sumatrapdf:27-34 ), not `(JsonStruct*, json::Value*)` as currently declared — `Func1<Value*>` only carries one real argument (`Value*`); the object (`this`) is threaded through `userData`, not as a `JsonStruct*` parameter.

`MkFunc1` (as opposed to `MkMethod1`) is only appropriate when the callback truly is a free/static function taking `(T1*, T2)`, e.g. `CountOnValue(CountState* st, json::Value* v)` used via `MkFunc1(CountOnValue, &countState)` — a pattern also present in the test file. It is not the right tool when the callback body needs to call a non-static member function of the enclosing class.
### Citations
**File:** src/base/tests/JsonParser_ut.cpp (L27-34)
```cpp
    void OnValue(json::Value* v) {
        utassert(idx < dataLen);
        const ExpectedValue& d = data[idx];
        utassert(v->type == d.type);
        utassert(str::Eq(json::PathFormatTemp(v->path), d.path));
        utassert(str::Eq(v->value, d.value));
        idx++;
    }
```
**File:** src/base/tests/JsonParser_ut.cpp (L37-39)
```cpp
static bool ParseVerify(Str json, JsonVerifier* v) {
    return json::Parse(json, MkMethod1<JsonVerifier, json::Value*, &JsonVerifier::OnValue>(v));
}
```
**File:** src/base/JsonParser.h (L33-35)
```text
using VisitFn = Func1<Value*>;

bool Parse(Str data, const VisitFn& onValue);
```
**File:** src/base/Base.h (L772-784)
```text
template <typename T, typename TArg, void (T::*Method)(TArg)>
static void MethodTrampoline1(void* obj, TArg arg) {
    (static_cast<T*>(obj)->*Method)(arg);
}

template <typename T, typename TArg, void (T::*Method)(TArg)>
Func1<TArg> MkMethod1(T* obj) {
    auto res = Func1<TArg>{};
    using fptr = void (*)(void*, TArg);
    res.fn = (fptr)&MethodTrampoline1<T, TArg, Method>;
    res.SetData((void*)obj, false);
    return res;
}
```
**File:** src/base/Base.h (L786-793)
```text
template <typename T1, typename T2>
Func1<T2> MkFunc1(void (*fn)(T1*, T2), T1* d) {
    auto res = Func1<T2>{};
    using fptr = void (*)(void*, T2);
    res.fn = (fptr)fn;
    res.SetData((void*)d, false);
    return res;
}
```
## Q7
函数内部的printf语句，怎么根据是不是有类似debug的全局变量来打印信息
### Answer
代码库里有两种常见做法，可以根据场景选择：

## 方法一：直接用全局 build 标志 `gIsDebugBuild` 做条件判断

`SumatraConfig.h`/`SumatraConfig.cpp` 里定义了几个全局 build 标志：

```cpp
extern bool gIsDebugBuild;
extern bool gIsAsanBuild;
extern bool gIsPreReleaseBuild;
``` sumatrapdf:8-10 

在函数内部直接判断后再调用 `logf`，比如 `SetCurrentLangByCode()` 里的写法：

```cpp
if (idx == 0 && !gIsDebugBuild) {
    // perf: in release builds we skip parsing translations for english
    // in debug we want to execute this code to catch errors
    return;
}
``` sumatrapdf:193-197 

或者像 `DeleteStaleFilesAsync()` 那样，先整体判断标志，后面的 `logf` 只在满足条件时才会执行到：

```cpp
if (!(gIsPreReleaseBuild || gIsDebugBuild)) {
    return;
}
...
logf("DeleteStaleFilesAsync: dataDir: '%s'\n", dataDir);
``` sumatrapdf:1707-1714 

这种写法最简单：直接 `if (gIsDebugBuild) { logf(...); }`。

## 方法二：为某个模块单独定义一个局部的调试开关变量 + 包装宏

如果你不想用全局的 build 标志，而是想针对某个模块/功能单独开关（比如"只在调试这个 cache 逻辑的时候才打印"），`RenderCache.cpp` 里的做法更贴合你说的"类似 debug 的全局变量"：

```cpp
// RenderCache's verbose per-operation logging (FreePage / Paint / DropCacheEntry
// / ...) is noisy, so it's disabled by default. Set gLogRenderCache = true to
// re-enable it when debugging the cache.
static bool gLogRenderCache = false;
#define rcLogf(...)                \
    do {                           \
        if (gLogRenderCache) {     \
            log(fmt(__VA_ARGS__)); \
        }                          \
    } while (0)
``` sumatrapdf:74-83 

调用方式就跟平时用 `logf`/`printf` 一样，只是换成 `rcLogf(...)`；打开调试时只需临时把 `gLogRenderCache` 改成 `true`（注释里甚至给出了在构造函数里临时启用的位置提示）：

```cpp
RenderCache::RenderCache() : maxTileSize(...) {
    // enable when debugging RenderCache logic
    // gEnableDbgLog = true;
    ...
``` sumatrapdf:90-92 

## 底层的 `log`/`logf`

两种做法最终都落到统一的日志基础设施：`logf()` 是一个模板函数，内部用 `str::FormatTemp` 格式化后调用 `log()`：

```cpp
template <typename... TArgs>
void logf(const char* s, const TArgs&... args) {
    ::log(str::FormatTemp(s, args...));
}
``` sumatrapdf:837-840 

这些日志最终会写入日志文件/控制台，也可以被 Logview 工具（通过命名管道 `\\.\pipe\LOCAL\ArsLexis-Logger`）实时查看，这在 `docs/md/Logview.md` 里有说明 sumatrapdf:13-15 。

## 建议

- 如果你想要的是"debug build 才打印"（编译期区分 Debug/Release），直接用现成的 `gIsDebugBuild`，写法一最简单，不需要新增变量。
- 如果你想要的是"运行时手动开关某个功能的详细日志"（不区分 build 类型，方便临时调试某个具体模块），推荐照抄 `RenderCache.cpp` 的 `gLogRenderCache` + `rcLogf` 宏模式：在你的 `.cpp` 顶部加一个 `static bool gLogXxx = false;`，配一个 `#define xxxLogf(...) do { if (gLogXxx) { log(fmt(__VA_ARGS__)); } } while (0)`，然后把函数内的 `printf`/`logf` 换成 `xxxLogf`。这样默认不打印（避免刷屏日志），调试时改一行把 `gLogXxx` 设为 `true` 即可。
### Citations
**File:** src/SumatraConfig.h (L8-10)
```text
extern bool gIsDebugBuild;
extern bool gIsAsanBuild;
extern bool gIsPreReleaseBuild;
```
**File:** src/Translations.cpp (L193-197)
```cpp
    if (idx == 0 && !gIsDebugBuild) {
        // perf: in release builds we skip parsing translations for english
        // in debug we want to execute this code to catch errors
        return;
    }
```
**File:** src/SumatraStartup.cpp (L1707-1714)
```cpp
    if (!(gIsPreReleaseBuild || gIsDebugBuild)) {
        return;
    }
    TempStr dataDir = GetSumatraDataDirTemp();
    if (!dataDir) {
        return;
    }
    logf("DeleteStaleFilesAsync: dataDir: '%s'\n", dataDir);
```
**File:** src/RenderCache.cpp (L74-83)
```cpp
// RenderCache's verbose per-operation logging (FreePage / Paint / DropCacheEntry
// / ...) is noisy, so it's disabled by default. Set gLogRenderCache = true to
// re-enable it when debugging the cache.
static bool gLogRenderCache = false;
#define rcLogf(...)                \
    do {                           \
        if (gLogRenderCache) {     \
            log(fmt(__VA_ARGS__)); \
        }                          \
    } while (0)
```
**File:** src/RenderCache.cpp (L90-92)
```cpp
RenderCache::RenderCache() : maxTileSize({GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)}) {
    // enable when debugging RenderCache logic
    // gEnableDbgLog = true;
```
**File:** src/base/Base.h (L837-840)
```text
template <typename... TArgs>
void logf(const char* s, const TArgs&... args) {
    ::log(str::FormatTemp(s, args...));
}
```
**File:** docs/md/Logview.md (L13-15)
```markdown
Logview is a generic logging tool that opens a named pipe `\\.\pipe\LOCAL\ArsLexis-Logger` that any application can open and write to.

SumatraPDF uses it for logging (`log()`, `logf()` functions in `Log.h` and `Log.cpp`).
```
## Q8
用已有数据结构怎么实现 real hash map
### Answer
`dict::MapStrToInt`(`src/base/Dict.cpp`/`Dict.h`)本身就是用现有基础数据结构拼出来的一个"real hash map"，只是它故意把类型限定成 `Str -> int`，避免模板化带来的代码膨胀。理解它的构造方式，就能照着思路做一个泛型版本。

## 现有的构造 "real hash map" 所依赖的基础组件

1. **`Arena`**（`src/base/Base.h` 中声明的 `ArenaNew()`/`ArenaDelete()`/`AllocArray<T>(a, n)`）——批量分配、统一释放，避免对每个 `HashTableEntry` 单独 `malloc`/`free`。`MapStrToInt` 构造时就是 `a = ArenaNew(); h = NewHashTable(initialSize, a);` sumatrapdf:208-212 ，析构时 `DeleteHashTable(h); ArenaDelete(a);` sumatrapdf:214-217 。

2. **裸的桶数组 + 链式冲突解决**：`HashTable` 结构就是 `HashTableEntry** entries` 数组（大小为 2 的幂）加一个 `freeList`（复用被删除的 entry 节省分配） sumatrapdf:70-87 ，`NewHashTable()` 用 `RoundToPowerOf2` 保证桶数是 2 的幂，用 `AllocArray<HashTableEntry*>(size)` 分配桶数组（注意：桶数组本身**不**用 arena 分配，而是普通堆分配，因为它体积大，resize 时要整体释放重建） sumatrapdf:89-99 。

3. **策略化的哈希/比较——`HasherComparator` 接口**：把"怎么算哈希""怎么判等"抽成一个虚基类接口 `HasherComparator`（`Hash(uintptr_t)`/`Equal(uintptr_t,uintptr_t)`），针对不同 key 类型各写一个实现（`StrKeyHasherComparator` 用 `MurmurHash2`+`str::Eq`，`WStrKeyHasherComparator` 用 `wstr::Eq`） sumatrapdf:35-65 。这样底层的 `HashTable`/`GetOrCreateEntry`/`RemoveEntry`/`HashTableResize` 全部对 key 的具体类型无感知，只操作 `uintptr_t`，从而**不需要模板**就能支持多种 key 类型（作者在文件头注释里明确说明了这个设计取舍：避免模板化带来的"code bloat and awful syntax" sumatrapdf:4-16 ）。

4. **核心三个操作**都建立在这个通用 `HashTable` 之上：
   - `GetOrCreateEntry()`：按 `hash % nEntries` 定位桶，链表内线性比较找到已存在的 entry；若不存在且允许创建（`a != nullptr`），从 `freeList` 或 arena 分配新 entry 并挂到链表头 sumatrapdf:144-174 。
   - `RemoveEntry()`：找到 entry 后从链表中摘除，放回 `freeList` 供复用（而不是真正释放内存） sumatrapdf:176-206 。
   - `HashTableResize()` + `HashTableResizeIfNeeded()`：负载因子超过 150%（借用了 redis 同款策略，用链式冲突可以容忍超过 100% 的负载）就整体 rehash 到下一个 2 的幂大小 sumatrapdf:106-141 。

5. **类型安全的外层包装** `MapStrToInt`：真正的"map"只是在这个通用无类型 `HashTable` 外面包一层，把 `Str`/`int` 编解码成 `uintptr_t`（`(uintptr_t)CStrTemp(key)`、`(intptr_t)val`），并在插入新 key 时用 `str::Dup(a, key)` 把 key 复制一份"驻留"进 arena，保证 entry 里存的指针在 map 生命周期内始终有效 sumatrapdf:232-255 。`Get`/`Remove` 同理，只是不创建新 entry sumatrapdf:257-281 。

## 如果要用现有数据结构自己实现一个新的（泛型）hash map

按照上面同样的模式，你有两条可行路径：

**路径 A：直接复用 `dict::HashTable` 这套底层机制，只加一个新的 `HasherComparator` 子类**
如果 key 类型可以自然编码成 `uintptr_t`（指针、interned 字符串、整数等），最省事的做法就是：写一个新的 `XxxKeyHasherComparator : public HasherComparator`，再仿照 `MapStrToInt` 写一个 `MapXxxToYyy` 包装类（构造 `Arena`+`HashTable`，`Insert`/`Get`/`Remove` 分别调用 `GetOrCreateEntry`/`RemoveEntry`）。这是**改动最小、风格最一致**的方案，不用改动 `Dict.cpp` 底层任何一行。

**路径 B：用 `Vec<T>` 自己写一个"开放地址（open addressing）"或"桶+链表"的模板化 hash map**
如果你想要一个真正模板化、能存任意 `K/V` 组合的 map，可以直接用 `Vec<T>`（`src/base/Vec.h`）当桶数组的底层存储，例如 `Vec<Entry<K,V>>` 作为开放地址表（类似 `hb_hashmap_t` 在 `ext/harfbuzz/src/hb-map.hh` 里的做法：单一扁平数组 + `is_used`/`is_real` 标记位表示"空/删除/占用"三态，探测方式是线性探测 `i = (i + ++step) & mask`） sumatrapdf:248-272 。`Vec<T>` 已经提供了 `Reset()`/`Clear()`/`FreeEls()`/`VecReserve()`/`VecInsertSpace()` 这些内存管理原语 sumatrapdf:27-59 ，可以直接拿来管理你自己的桶数组，不需要重新写内存分配/扩容逻辑。

两条路径的取舍：
- 路径 A 与代码库既有风格（`dict` 命名空间，非模板化，故意避免模板膨胀）完全一致，适合往 `src/base/Dict.h`/`Dict.cpp` 里新增一种 map 类型。
- 路径 B 更像 `hb_hashmap_t` 的现代模板化开放地址实现，性能通常优于链式（缓存局部性更好），但需要自己处理 tombstone（墓碑）删除标记、rehash 时的搬移逻辑，工作量比路径 A 大。

如果你能说明具体要支持的 key/value 类型，我可以进一步给出更贴合你需求的具体设计（比如是否要模板化、是否要支持自定义 hash 函数等）。
### Citations
**File:** src/base/Dict.cpp (L4-16)
```cpp
/* This is a dictionary with intentionally unorthodox design.
It's called dictionary, not a hashtable, to emphasize it's
about the API, not implementation technique.
Currently it's imlemented on top of a hash table, but it could
just as well be a tree of some kind.

Usually those things are done as a templated hash table class,
but I want to avoid code bloat and awful syntax.

The classes are based on a generic, untyped uintptr => uintptr
hash table. The actual dict class is a wrapper that provides
a type-safe API and handles policy decisions like allocations
(if they are necessary).
```
**File:** src/base/Dict.cpp (L35-65)
```cpp
class HasherComparator {
  public:
    virtual size_t Hash(uintptr_t key) = 0;
    virtual bool Equal(uintptr_t k1, uintptr_t k2) = 0;
    virtual ~HasherComparator() = default;
};

static Str KeyAsStr(uintptr_t key) {
    // interned hash table keys are NUL-terminated
    return Str((char*)key);
}

static WStr KeyAsWStr(uintptr_t key) {
    return WStr((wchar_t*)key);
}

class StrKeyHasherComparator : public HasherComparator {
    size_t Hash(uintptr_t key) override {
        Str s = KeyAsStr(key);
        return MurmurHash2(s);
    }
    bool Equal(uintptr_t k1, uintptr_t k2) override { return str::Eq(KeyAsStr(k1), KeyAsStr(k2)); }
};

class WStrKeyHasherComparator : public HasherComparator {
    size_t Hash(uintptr_t key) override {
        WStr s = KeyAsWStr(key);
        return MurmurHash2(s);
    }
    bool Equal(uintptr_t k1, uintptr_t k2) override { return wstr::Eq(KeyAsWStr(k1), KeyAsWStr(k2)); }
};
```
**File:** src/base/Dict.cpp (L70-87)
```cpp
struct HashTableEntry {
    uintptr_t key;
    uintptr_t val;
    struct HashTableEntry* next;
};

// not a class so that it can be allocated with an allocator
struct HashTable {
    HashTableEntry** entries;
    HashTableEntry* freeList;

    int nEntries;
    int nUsed; // total number of inserted entries

    // for debugging
    int nResizes;
    int nCollisions;
};
```
**File:** src/base/Dict.cpp (L89-99)
```cpp
static HashTable* NewHashTable(int size, Arena* a) {
    ReportIf(!a); // we'll leak otherwise
    HashTable* h = AllocArray<HashTable>(a, 1);
    // number of hash table entries should be power of 2
    size = RoundToPowerOf2(size);
    // entries are not allocated with the arena since those are large blocks
    // and we don't want to waste their memory after
    h->entries = AllocArray<HashTableEntry*>(size);
    h->nEntries = size;
    return h;
}
```
**File:** src/base/Dict.cpp (L106-141)
```cpp
static void HashTableResize(HashTable* h, HasherComparator* hc) {
    int newSize = RoundToPowerOf2(h->nEntries + 1);
    ReportIf(newSize <= h->nEntries);
    HashTableEntry** newEntries = AllocArray<HashTableEntry*>(newSize);
    HashTableEntry *e, *next;
    size_t hash;
    int pos;
    for (int i = 0; i < h->nEntries; i++) {
        e = h->entries[i];
        while (e) {
            next = e->next;
            hash = hc->Hash(e->key);
            pos = (int)(hash % (size_t)newSize);
            e->next = newEntries[pos];
            newEntries[pos] = e;
            e = next;
        }
    }
    free((void*)h->entries);
    h->entries = newEntries;
    h->nEntries = newSize;
    h->nResizes += 1;

    ReportIf(h->nUsed >= (h->nEntries * 3) / 2);
}

// micro optimization: this is called often, so we want this check inlined. Resizing logic
// is called rarely, so doesn't need to be inlined
static inline void HashTableResizeIfNeeded(HashTable* h, HasherComparator* hc) {
    // per http://stackoverflow.com/questions/1603712/when-should-i-do-rehashing-of-entire-hash-table/1604428#1604428
    // when using collision chaining, load factor can be 150%
    if (h->nUsed < (h->nEntries * 3) / 2) {
        return;
    }
    HashTableResize(h, hc);
}
```
**File:** src/base/Dict.cpp (L144-174)
```cpp
static HashTableEntry* GetOrCreateEntry(HashTable* h, HasherComparator* hc, uintptr_t key, Arena* a, bool& newEntry) {
    bool shouldCreate = (a != nullptr);
    size_t hash = hc->Hash(key);
    int pos = (int)(hash % (size_t)h->nEntries);
    HashTableEntry* e = h->entries[pos];
    newEntry = false;
    while (e) {
        if (hc->Equal(key, e->key)) {
            return e;
        }
        e = e->next;
    }
    if (!shouldCreate) {
        return nullptr;
    }

    if (h->freeList) {
        e = h->freeList;
        h->freeList = h->freeList->next;
    } else {
        e = AllocArray<HashTableEntry>(a, 1);
    }
    e->next = h->entries[pos];
    h->entries[pos] = e;
    h->nUsed++;
    if (e->next != nullptr) {
        h->nCollisions++;
    }
    newEntry = true;
    return e;
}
```
**File:** src/base/Dict.cpp (L176-206)
```cpp
static bool RemoveEntry(HashTable* h, HasherComparator* hc, uintptr_t key, uintptr_t* removedValOut) {
    size_t hash = hc->Hash(key);
    int pos = (int)(hash % (size_t)h->nEntries);
    HashTableEntry* e = h->entries[pos];
    while (e) {
        if (hc->Equal(key, e->key)) {
            break;
        }
        e = e->next;
    }
    if (!e) {
        return false;
    }

    // remove the the entry from the list
    HashTableEntry* e2 = h->entries[pos];
    if (e2 == e) {
        h->entries[pos] = e->next;
    } else {
        while (e2->next != e) {
            e2 = e2->next;
        }
        e2->next = e->next;
    }
    e->next = h->freeList;
    h->freeList = e;
    *removedValOut = e->val;
    ReportIf(0 == h->nUsed);
    h->nUsed -= 1;
    return true;
}
```
**File:** src/base/Dict.cpp (L208-212)
```cpp
MapStrToInt::MapStrToInt(int initialSize) {
    // arena-allocate HashTableEntry entries and copies of string keys
    a = ArenaNew();
    h = NewHashTable(initialSize, a);
}
```
**File:** src/base/Dict.cpp (L214-217)
```cpp
MapStrToInt::~MapStrToInt() {
    DeleteHashTable(h);
    ArenaDelete(a);
}
```
**File:** src/base/Dict.cpp (L232-255)
```cpp
bool MapStrToInt::Insert(Str key, int val, int* existingValOut, Str* existingKeyOut) {
    if (len(key) == 0) {
        return false;
    }
    bool newEntry;
    HashTableEntry* e = GetOrCreateEntry(h, &gStrKeyHasherComparator, (uintptr_t)CStrTemp(key), a, newEntry);
    if (!newEntry) {
        if (existingValOut) {
            *existingValOut = (int)e->val;
        }
        if (existingKeyOut) {
            *existingKeyOut = KeyAsStr(e->key);
        }
        return false;
    }
    e->key = (intptr_t)str::Dup(a, key).s;
    e->val = (intptr_t)val;
    if (existingKeyOut) {
        *existingKeyOut = KeyAsStr(e->key);
    }

    HashTableResizeIfNeeded(h, &gStrKeyHasherComparator);
    return true;
}
```
**File:** src/base/Dict.cpp (L257-281)
```cpp
bool MapStrToInt::Remove(Str key, int* removedValOut) const {
    if (len(key) == 0) {
        return false;
    }
    uintptr_t removedVal;
    bool removed = RemoveEntry(h, &gStrKeyHasherComparator, (uintptr_t)CStrTemp(key), &removedVal);
    if (removed && removedValOut) {
        *removedValOut = (int)removedVal;
    }
    return removed;
}

bool MapStrToInt::Get(Str key, int* valOut) const {
    if (len(key) == 0) {
        return false;
    }
    StrKeyHasherComparator hc;
    bool newEntry;
    HashTableEntry* e = GetOrCreateEntry(h, &hc, (uintptr_t)CStrTemp(key), nullptr, newEntry);
    if (!e) {
        return false;
    }
    *valOut = (int)e->val;
    return true;
}
```
**File:** ext/harfbuzz/src/hb-map.hh (L248-272)
```text
  bool set_with_hash (KK&& key, uint32_t hash, VV&& value, bool overwrite = true)
  {
    if (unlikely (!successful)) return false;
    if (unlikely ((occupancy + occupancy / 2) >= mask && !alloc ())) return false;

    hash &= 0x3FFFFFFF; // We only store lower 30bit of hash
    unsigned int tombstone = (unsigned int) -1;
    unsigned int i = hash % prime;
    unsigned length = 0;
    unsigned step = 0;
    while (items[i].is_used ())
    {
      if ((std::is_integral<K>::value || items[i].hash == hash) &&
	  items[i] == key)
      {
        if (!overwrite)
	  return false;
        else
	  break;
      }
      if (!items[i].is_real () && tombstone == (unsigned) -1)
        tombstone = i;
      i = (i + ++step) & mask;
      length++;
    }
```
**File:** src/base/Vec.h (L27-59)
```text
template <typename T>
struct Vec {
    int len = 0;
    int cap = 0;
    T* els = nullptr;

    // We always pad the elements with a single 0 value. This makes
    // Vec<char> and Vec<WCHAR> a C-compatible string. Although it's
    // not useful for other types, the code is simpler if we always do it
    // (rather than have it an optional behavior).

    void FreeEls() {
        if (els) {
            Free(nullptr, (void*)els);
            els = nullptr;
        }
    }

    // resets to initial state, freeing memory
    void Reset() {
        FreeEls();
        len = 0;
        cap = 0;
    }

    // use to empty but don't free els
    // for efficient reuse
    void Clear() {
        len = 0;
        if (els && cap > 0) {
            memset((void*)els, 0, (size_t)cap * sizeof(T));
        }
    }
```
## Q9
MapStrToInt怎么迭代keys
### Answer
`MapStrToInt` 目前**没有**公开的迭代接口——其 `Dict.h` 里声明的公开方法只有 `Count()`/`Insert()`/`Remove()`/`Get()`，没有 `Keys()`/`ForEach()`/迭代器之类的东西:

```cpp
class MapStrToInt {
  public:
    Arena* a = nullptr;
    HashTable* h = nullptr;

    explicit MapStrToInt(int initialSize = kHashTableInitialSize);
    ~MapStrToInt();

    int Count() const;
    bool Insert(Str key, int val, int* existingValOut = nullptr, Str* existingKeyOut = nullptr);
    bool Remove(Str key, int* removedValOut) const;
    bool Get(Str key, int* valOut) const;
};
``` sumatrapdf:19-33 

## 为什么不能从外部直接遍历

`MapStrToInt` 的成员 `h` 是 `HashTable*`，但 `HashTable` 在 `Dict.h` 里只是前置声明 `struct HashTable;` sumatrapdf:6-6 ，其真正的字段定义（`entries`、`nEntries`、`freeList` 等）只在 `Dict.cpp` 内部可见：

```cpp
struct HashTable {
    HashTableEntry** entries;
    HashTableEntry* freeList;
    int nEntries;
    int nUsed;
    int nResizes;
    int nCollisions;
};
``` sumatrapdf:77-87 

所以即使 `MapStrToInt::h` 是 public 成员，外部代码也拿不到 `HashTable` 的完整类型定义，无法直接访问 `h->entries` 数组去遍历——这是一个 opaque pointer（不完整类型）。测试文件 `Dict_ut.cpp` 里为了验证 `Insert`/`Remove`/`Get` 的正确性，也是自己另外维护一个 `StrVec toRemove` 来记录插入过的 key，而不是从 `MapStrToInt` 本身遍历出 key 列表 sumatrapdf:59-76 。

## 结论：要迭代 keys，需要给 `MapStrToInt`/`HashTable` 新增一个遍历方法

现有数据结构本身不支持迭代，如果需要这个能力，得在 `Dict.cpp` 内部新增一个函数（因为只有 `.cpp` 里能看到 `HashTable`/`HashTableEntry` 的完整定义），类似：

```cpp
// 在 Dict.cpp 中新增
static void HashTableForEach(HashTable* h, const std::function<void(HashTableEntry*)>& fn) {
    for (int i = 0; i < h->nEntries; i++) {
        for (HashTableEntry* e = h->entries[i]; e; e = e->next) {
            fn(e);
        }
    }
}
```

再在 `MapStrToInt` 上加一个包装方法（在 `Dict.h` 声明、`Dict.cpp` 实现），比如：

```cpp
// Dict.h
void MapStrToInt::ForEach(const std::function<void(Str key, int val)>& fn) const;

// Dict.cpp
void MapStrToInt::ForEach(const std::function<void(Str key, int val)>& fn) const {
    HashTableForEach(h, [&](HashTableEntry* e) {
        fn(KeyAsStr(e->key), (int)e->val);
    });
}
```

这里复用了已有的 `KeyAsStr()` 辅助函数（把 `uintptr_t` key 解释成 NUL-结尾字符串），它已经在 `Insert`/`Get` 等函数里用于同样的解码 sumatrapdf:41-43 。

需要注意几点约束：
- 遍历顺序是桶数组下标顺序 + 链表顺序，不保证插入顺序，也不保证稳定（`HashTableResize` 会重新分桶，顺序在 resize 前后会变化）。
- 遍历期间不要调用 `Insert`（可能触发 resize，导致 `entries` 数组被整体释放重建，正在遍历的指针失效）；`Remove` 当前 entry 本身（放回 `freeList`）通常安全，但要小心不要移动/重排链表指针后继续用旧的 `next`。
- 如果你只是想临时调试查看内容，也可以不新增正式 API，而是在 `Dict.cpp` 内部临时写一个遍历函数验证后再决定是否要作为正式接口保留。

如果你需要的话，我可以给出更完整的 `ForEach`/迭代器设计（比如是否要支持 early-break、是否要做成真正的 C++ 迭代器以配合 `for (auto& [k,v] : map)` 语法），但目前代码库里没有现成实现，需要新增。
### Citations
**File:** src/base/Dict.h (L6-6)
```text
struct HashTable;
```
**File:** src/base/Dict.h (L19-33)
```text
class MapStrToInt {
  public:
    Arena* a = nullptr;
    HashTable* h = nullptr;

    explicit MapStrToInt(int initialSize = kHashTableInitialSize);
    ~MapStrToInt();

    int Count() const;

    bool Insert(Str key, int val, int* existingValOut = nullptr, Str* existingKeyOut = nullptr);

    bool Remove(Str key, int* removedValOut) const;
    bool Get(Str key, int* valOut) const;
};
```
**File:** src/base/Dict.cpp (L41-43)
```cpp

static Str KeyAsStr(uintptr_t key) {
    // interned hash table keys are NUL-terminated
```
**File:** src/base/Dict.cpp (L77-87)
```cpp
struct HashTable {
    HashTableEntry** entries;
    HashTableEntry* freeList;

    int nEntries;
    int nUsed; // total number of inserted entries

    // for debugging
    int nResizes;
    int nCollisions;
};
```
**File:** src/base/tests/Dict_ut.cpp (L59-76)
```cpp
    srand((unsigned int)time(nullptr));
    StrVec toRemove;
    for (int i = 0; i < 1024; i++) {
        Str k = GenRandomString();
        ok = d.Insert(k, i, nullptr);
        // no guarantee that the string is unique, so Insert() doesn't always succeeds
        if (!ok) continue;
        toRemove.Append(str::Dup(k));
        utassert(len(toRemove) == d.Count());
        ok = d.Get(k, &val);
        ReportIf(!ok);
        ReportIf(i != val);
    }
    for (int i = 0; i < len(toRemove); i++) {
        Str k = toRemove[i];
        ok = d.Remove(k, nullptr);
        utassert(ok);
    }
```
## Q10
给出更完整的 ForEach/迭代器设计，支持 early-break、做成真正的 C++ 迭代器以配合 for (auto& [k,v] : map) 语法
### Answer
代码里其实早就留了这个 TODO——注释里明确写着 `// TODO:\n// - add iterator for keys/values` sumatrapdf:23-24 ，说明作者本来就打算加但还没做。下面给出一个可以直接落地、支持 `for (auto [k, v] : map)` 和 early-break 的设计。

## 设计要点

1. **迭代器必须能跳过空桶**——底层是 `entries[nEntries]` 数组 + 每个桶一条链表 sumatrapdf:77-87 ，大部分桶可能是空指针，所以 `operator++` 逻辑是"先沿当前链表走 `next`，链表走完了就跳到下一个非空桶"。
2. **要支持 range-for `for (auto& [k, v] : map)`**，需要给 `MapStrToInt` 添加 `begin()`/`end()`，并让 `operator*` 返回一个包含 `key`/`val` 的小结构体（用结构化绑定）。
3. **要支持 early-break**（`break;`/`return;` 提前退出循环），这一点标准的 `for (auto& x : container)` 天然就支持——只要迭代器满足基本的前向迭代器要求（`operator!=`、`operator++`、`operator*`），不需要额外设计；不需要像"visitor + callback"模式那样额外传 `bool` 返回值来表示"停止"。这也是为什么应该做成真正的迭代器，而不是 `ForEach(std::function<...>)` 回调式接口——回调式接口如果要支持 early-break，还要人为约定"回调返回 false 就停止"，多一层复杂度。
4. **`HashTableEntry`/`HashTable` 是定义在 `Dict.cpp` 内部的不完整类型** sumatrapdf:6-6 ，迭代器如果要暴露给 `Dict.h`（这样外部代码才能写 `for (auto& kv : map)`），迭代器类本身也必须放进 `Dict.h`，但它内部持有的 `HashTableEntry*` 只能作为不透明指针使用（`struct HashTableEntry;` 前置声明），实际解引用逻辑（比如 `KeyAsStr`）还是要通过在 `Dict.cpp` 里实现的非 inline 成员函数完成。

## 具体设计

**`Dict.h` 新增内容：**

```cpp
namespace dict {

struct HashTable;
struct HashTableEntry; // opaque, defined in Dict.cpp

// value returned by dereferencing MapStrToInt::Iterator
struct StrIntEntry {
    Str key;
    int val;
};

class MapStrToInt {
  public:
    Arena* a = nullptr;
    HashTable* h = nullptr;

    explicit MapStrToInt(int initialSize = kHashTableInitialSize);
    ~MapStrToInt();

    int Count() const;
    bool Insert(Str key, int val, int* existingValOut = nullptr, Str* existingKeyOut = nullptr);
    bool Remove(Str key, int* removedValOut) const;
    bool Get(Str key, int* valOut) const;

    // --- iteration support ---
    class Iterator {
      public:
        Iterator() = default; // sentinel / end()
        Iterator(HashTable* h, int bucketIdx, HashTableEntry* e) : h(h), bucketIdx(bucketIdx), e(e) {}

        StrIntEntry operator*() const;      // implemented in Dict.cpp (needs HashTableEntry layout)
        Iterator& operator++();             // implemented in Dict.cpp
        bool operator!=(const Iterator& other) const { return e != other.e; }
        bool operator==(const Iterator& other) const { return e == other.e; }

      private:
        HashTable* h = nullptr;
        int bucketIdx = -1;
        HashTableEntry* e = nullptr;
    };

    Iterator begin() const;
    Iterator end() const { return Iterator(); }
};

} // namespace dict
```

**`Dict.cpp` 新增实现**（复用已有的 `KeyAsStr` 辅助函数 sumatrapdf:42-45 ）：

```cpp
// finds the first non-empty bucket starting at (and including) idx
static HashTableEntry* FirstEntryFrom(HashTable* h, int& bucketIdx) {
    for (; bucketIdx < h->nEntries; bucketIdx++) {
        if (h->entries[bucketIdx]) {
            return h->entries[bucketIdx];
        }
    }
    return nullptr;
}

MapStrToInt::Iterator MapStrToInt::begin() const {
    int idx = 0;
    HashTableEntry* e = FirstEntryFrom(h, idx);
    return Iterator(h, idx, e);
}

StrIntEntry MapStrToInt::Iterator::operator*() const {
    return StrIntEntry{KeyAsStr(e->key), (int)e->val};
}

MapStrToInt::Iterator& MapStrToInt::Iterator::operator++() {
    if (e->next) {
        e = e->next;
        return *this;
    }
    // current bucket's chain exhausted, advance to next non-empty bucket
    bucketIdx++;
    e = FirstEntryFrom(h, bucketIdx);
    return *this;
}
```

## 用法

```cpp
dict::MapStrToInt m;
m.Insert("foo", 1);
m.Insert("bar", 2);

for (auto [key, val] : m) {
    logf("%s = %d\n", key.CStr(), val);
    if (str::Eq(key, "bar")) {
        break; // early-break 天然可用，无需额外设计
    }
}
```

（这里假定 `Str` 有类似 `CStr()`/隐式转换到 `const char*` 的方法用于打印；具体用哪个转换函数取决于 `Str` 的现有 API，可以按需替换成 `str::Dup`/已有的格式化函数。）

## 需要注意的限制/权衡

- **不保证顺序**：遍历顺序是桶下标顺序 + 链表插入顺序（新 entry 插入链表头），不是插入 map 的顺序，且 `HashTableResize()` 会整体 rehash 到新的桶数组 sumatrapdf:106-130 ，同一个 key 在 resize 前后落在哪个桶会变，因此**遍历期间绝对不能调用 `Insert`**（可能触发 resize，`h->entries` 指针被 `free` 重建，迭代器里缓存的 `bucketIdx`/`e` 立刻悬空）。
- **遍历期间调用 `Remove` 当前正在访问的 entry 是不安全的**：`RemoveEntry()` 会把该 entry 挂回 `freeList` 并修改链表指针 sumatrapdf:176-206 ，而 `operator++` 依赖 `e->next` 才能找到下一个元素——如果 `e` 已经被 `Remove`，其 `next` 已经被改写指向 `freeList` 而不是原链表中的下一个元素，会导致遍历错乱或跳过元素。如果要支持"遍历中删除"，需要在 `operator++` 之前先保存 `next` 指针，或者给 `Iterator` 提供一个 `EraseAndAdvance()`方法，内部先记录 `next`、再调用 `RemoveEntry`、再继续。当前设计**不支持**遍历中删除，只支持只读遍历 + early-break。
- **`operator*` 返回值是拷贝（`StrIntEntry{Str, int}`），不是引用**：因为底层存储是 `uintptr_t key/val`，没有天然的 `std::pair<Str,int>&` 可引用；`key` 是对 arena 内存的一个 `{ptr,len}` 视图（沿用 `KeyAsStr` 的语义 sumatrapdf:42-45 ），生命周期跟随 `MapStrToInt` 本身（因为底层字符串是 arena-分配、`map` 存活期间不会被释放），所以在 `map` 存活期间使用是安全的，但不能把 `key` 存下来跨越 `map` 销毁后继续用。
- **可以用同样的模式给 `MapStrToWStrToInt` 之类的其他 map 类型加迭代器**——如果代码库以后加了别的键类型的 map，重复上面模式即可，因为迭代逻辑本身跟 key 类型无关（都只是遍历 `HashTableEntry` 链表），只有 `operator*` 里解码 key 的方式需要换成对应的 `KeyAsWStr` 等。

## 是否要做成模板化的通用迭代器

如果以后要给多个 `MapXxxToYyy` 都加迭代器，会发现 `Iterator::operator++`/`FirstEntryFrom` 这套逻辑其实是通用的（只依赖 `HashTable`/`HashTableEntry` 布局），可以抽成一个内部共享的 `HashTableIteratorBase`，各个 `MapXxxToYyy::Iterator` 只需要重写 `operator*` 里"如何解码 key/val"这一部分。但鉴于 `Dict.cpp` 文件头本身就声明了"故意不用模板，避免 code bloat"的设计取向 sumatrapdf:10-11 ，建议先按每个具体 map 类型手写一份非模板的 `Iterator`（如上），除非确实要支持很多种 key/value 组合，再考虑做共享基类。
### Citations
**File:** src/base/Dict.cpp (L10-11)
```cpp
Usually those things are done as a templated hash table class,
but I want to avoid code bloat and awful syntax.
```
**File:** src/base/Dict.cpp (L23-24)
```cpp
TODO:
- add iterator for keys/values
```
**File:** src/base/Dict.cpp (L42-45)
```cpp
static Str KeyAsStr(uintptr_t key) {
    // interned hash table keys are NUL-terminated
    return Str((char*)key);
}
```
**File:** src/base/Dict.cpp (L77-87)
```cpp
struct HashTable {
    HashTableEntry** entries;
    HashTableEntry* freeList;

    int nEntries;
    int nUsed; // total number of inserted entries

    // for debugging
    int nResizes;
    int nCollisions;
};
```
**File:** src/base/Dict.cpp (L106-130)
```cpp
static void HashTableResize(HashTable* h, HasherComparator* hc) {
    int newSize = RoundToPowerOf2(h->nEntries + 1);
    ReportIf(newSize <= h->nEntries);
    HashTableEntry** newEntries = AllocArray<HashTableEntry*>(newSize);
    HashTableEntry *e, *next;
    size_t hash;
    int pos;
    for (int i = 0; i < h->nEntries; i++) {
        e = h->entries[i];
        while (e) {
            next = e->next;
            hash = hc->Hash(e->key);
            pos = (int)(hash % (size_t)newSize);
            e->next = newEntries[pos];
            newEntries[pos] = e;
            e = next;
        }
    }
    free((void*)h->entries);
    h->entries = newEntries;
    h->nEntries = newSize;
    h->nResizes += 1;

    ReportIf(h->nUsed >= (h->nEntries * 3) / 2);
}
```
**File:** src/base/Dict.cpp (L176-206)
```cpp
static bool RemoveEntry(HashTable* h, HasherComparator* hc, uintptr_t key, uintptr_t* removedValOut) {
    size_t hash = hc->Hash(key);
    int pos = (int)(hash % (size_t)h->nEntries);
    HashTableEntry* e = h->entries[pos];
    while (e) {
        if (hc->Equal(key, e->key)) {
            break;
        }
        e = e->next;
    }
    if (!e) {
        return false;
    }

    // remove the the entry from the list
    HashTableEntry* e2 = h->entries[pos];
    if (e2 == e) {
        h->entries[pos] = e->next;
    } else {
        while (e2->next != e) {
            e2 = e2->next;
        }
        e2->next = e->next;
    }
    e->next = h->freeList;
    h->freeList = e;
    *removedValOut = e->val;
    ReportIf(0 == h->nUsed);
    h->nUsed -= 1;
    return true;
}
```
**File:** src/base/Dict.h (L6-6)
```text
struct HashTable;
```
