项目中有两个主要的目录遍历实现：

## 1. `src/utils/DirIter.cpp` — 通用目录迭代器（推荐使用）

提供 C++ range-for 风格的 `DirIter` 类和异步遍历函数： [1](#12-0) 

**同步遍历用法**（参考 `DirTraverseThread`）：
```cpp
DirIter di("C:\\some\\path");
di.includeFiles = true;
di.includeDirs = false;
di.recurse = true;
for (DirIterEntry* de : di) {
    // de->filePath, de->name, de->fd
}
``` [2](#12-1) 

**异步遍历**：`StartDirTraverseAsync(StrQueue* queue, const char* dir, bool recurse)` — 在后台线程遍历，结果写入 `StrQueue`。

还有一个 `DirTraverse` 函数（被 `MakeLzSA.cpp` 调用），接受 lambda 回调：
```cpp
DirTraverse(dir, true, [](const char* path) -> bool { ... });
``` [3](#12-2) 

---

## 2. `src/common/dir_scan.cpp` — 异步目录扫描（用于文件浏览器 UI）

提供 `ReadDirectory` 和 `DirScanThread`，支持后台线程递归扫描、早退（`shouldExit`）和回调通知： [4](#12-3) [5](#12-4) 

---

## 总结

| 文件 | 适用场景 |
|---|---|
| `src/utils/DirIter.cpp` | 一般用途，同步/异步遍历文件，range-for 风格 |
| `src/common/dir_scan.cpp` | UI 文件浏览器场景，后台线程递归扫描，支持取消 |

对于你的 Home 页面功能（遍历用户选择的目录），使用 `DirIter` 即可，头文件是 `src/utils/DirIter.h`。

### Citations

**File:** src/utils/DirIter.h (L14-43)
```text
struct DirIter {
    const char* dir = nullptr;
    bool includeFiles = true;
    bool includeDirs = false;
    bool recurse = false;

    struct iterator {
        const DirIter* di;
        bool didFinish = false;

        StrVec dirsToVisit;
        char* currDir = nullptr;
        WCHAR* pattern = nullptr;
        WIN32_FIND_DATAW fd{};
        HANDLE h = nullptr;
        DirIterEntry data;

        iterator(const DirIter*, bool);
        ~iterator();

        DirIterEntry* operator*();
        iterator& operator++();   // ++it
        iterator operator++(int); // it++
        iterator& operator+(int); // it += n
        friend bool operator==(const iterator& a, const iterator& b);
        friend bool operator!=(const iterator& a, const iterator& b);
    };
    iterator begin() const;
    iterator end() const;
};
```

**File:** src/utils/DirIter.cpp (L180-196)
```cpp
static void DirTraverseThread(DirTraverseThreadData* td) {
    DirIter di(td->dir);
    di.includeFiles = true;
    di.includeDirs = false;
    di.recurse = td->recurse;
    for (DirIterEntry* de : di) {
        td->queue->Append(de->filePath);
    }
    td->queue->MarkFinished();
    delete td;
}

void StartDirTraverseAsync(StrQueue* queue, const char* dir, bool recurse) {
    auto td = new DirTraverseThreadData{queue, str::Dup(dir), recurse};
    auto fn = MkFunc0(DirTraverseThread, td);
    RunAsync(fn, "DirTraverseThread");
}
```

**File:** src/tools/MakeLzSA.cpp (L186-201)
```cpp
bool CreateArchiveFromDir(const char* archivePath, const char* dir) {
    StrVec files;
    int n = str::Len(dir);
    bool ok = DirTraverse(dir, true, [&files, n](const char* path) -> bool {
        const char* archiveName = path + n;
        if ('\\' == *archiveName) archiveName++;
        if ('/' == *archiveName) archiveName++;
        TempStr s = str::JoinTemp(path, ":", archiveName);
        files.Append(s);
        return true;
    });
    if (!ok) {
        return false;
    }
    return CreateArchive(archivePath, files, 0);
}
```

**File:** src/common/dir_scan.cpp (L36-36)
```cpp
void ReadDirectory(Arena* arena, DirEntries* dv, AtomicBool* shouldExit) {
```

**File:** src/common/dir_scan.cpp (L258-314)
```cpp
// Background thread function to read directories
DWORD WINAPI DirScanThread(LPVOID param) {
    DirScanCtx* ctx = (DirScanCtx*)param;
    auto* tempAlloc = GetTempArena();

    while (true) {
        WaitForSingleObject(ctx->hSemaphore, INFINITE);

        if (AtomicBoolGet(&ctx->shouldExit)) {
            break;
        }

        EnterCriticalSection(&ctx->cs);
        DirEntriesNode* node = ctx->dirsToVisit;
        if (!node) {
            bool allDone = (ctx->inFlightCount == 0);
            LeaveCriticalSection(&ctx->cs);
            if (allDone) {
                SetEvent(ctx->hQueueEmptyEvent);
            }
            continue;
        }

        ctx->dirsToVisit = node->next;
        ctx->inFlightCount++;
        DirEntries* dv = node->dv;
        bool nonRecursive = node->nonRecursive;
        LeaveCriticalSection(&ctx->cs);

        ReadDirectory(ctx->a, dv, &ctx->shouldExit);

        if (AtomicBoolGet(&ctx->shouldExit)) {
            break;
        }

        if (!nonRecursive) {
            for (int i = 0; i < dv->len; i++) {
                if (AtomicBoolGet(&ctx->shouldExit)) {
                    break;
                }
                DirEntry* e = &dv->els[i];
                if (e->dv == kStillScanningDir && !StrEq(e->name, StrL(".."))) {
                    Str subPath = PathJoinTemp(dv->fullDir, e->name);
                    DirEntries* subDv = AllocDirEntries(ctx->a, subPath);
                    e->dv = subDv;
                    QueueDirScan(ctx, subDv);
                }
            }
        }

        if (AtomicBoolGet(&ctx->shouldExit)) {
            break;
        }

        if (ctx->onScannedDir) {
            ctx->onScannedDir(dv, ctx->userData);
        }
```

