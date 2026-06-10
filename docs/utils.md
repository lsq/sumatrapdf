以下是 `src/utils` 目录下各文件的功能说明，按功能分类整理：

---

## 基础设施

### `BaseUtil.h` / `BaseUtil.cpp`
整个项目的基础头文件，几乎所有文件都包含它。提供：
- 平台/编译器检测宏（`OS_WIN`、`COMPILER_MSVC` 等）
- 基础类型别名（`i8`/`u8`/`i64`/`u64` 等）
- `ReportIf` / `CrashMe` — 断言与崩溃上报机制
- `AllocArray` / `AllocStruct` — 零初始化内存分配
- `Func0` / `Func1` — 轻量级函数指针封装（绑定函数+参数）
- `MkFunc0` / `MkFunc1` / `MkMethod0` — 创建 `Func0`/`Func1` 的工厂函数
- `ListDelete` / `ListInsertFront` / `ListRemove` 等 — 侵入式链表操作模板
- `defer { }` — RAII 作用域退出宏
- `Kind` — 轻量级运行时类型标识（用字符串地址作为类型 ID） [1](#13-0) 

---

## 字符串处理

### `StrUtil.h` / `StrUtil.cpp`
核心字符串工具库，`namespace str`：
- `Eq` / `EqI` / `EqN` — 字符串比较（区分/不区分大小写）
- `StartsWith` / `EndsWith` / `Contains` — 前缀/后缀/包含检测
- `Dup` / `Join` / `Format` / `FmtV` — 字符串复制、拼接、格式化
- `ReplaceWithCopy` — 安全替换字符串指针
- `ToLower` / `ToUpper` / `TrimWSInPlace` — 大小写转换、空白裁剪
- `FindChar` / `Find` / `FindI` — 字符/子串查找
- `MemToHex` / `HexToMem` — 十六进制编解码
- `Parse` — 类 scanf 解析
- `FormatSizeShortTemp` / `FormatFileSizeTemp` — 文件大小格式化（"3.48 MB"）
- `StrBuilder` / `WStrBuilder` — 可增长字符串构建器
- `ByteSlice` / `StrSpan` — 非拥有字节/字符串视图
- `seqstrings` 命名空间 — 操作 `\0` 分隔的字符串序列（用于设置系统）
- `url` 命名空间 — URL 解码、路径提取 [2](#13-1) 

### `StrVec.h` / `StrVec.cpp`
字符串向量，内存紧凑（分页存储）：
- `StrVec` — 字符串集合，支持 `Append` / `At` / `Find` / `Remove` / `Sort`
- `StrVecWithData<T>` — 每个字符串附带一个 `T` 类型的附加数据
- `Split` / `Join` / `JoinTemp` — 字符串分割与拼接
- `AppendIfNotExists` — 去重追加 [3](#13-2) 

### `StrconvUtil.h` / `StrconvUtil.cpp`
字符编码转换，`namespace strconv`：
- `Utf8ToWStr` / `WStrToUtf8` — UTF-8 ↔ UTF-16 转换
- `StrCPToWStr` / `WStrToCodePage` — 任意代码页转换
- `AnsiToUtf8` / `Utf8ToAnsi` — ANSI ↔ UTF-8
- `UnknownToUtf8Temp` — 自动检测编码并转为 UTF-8 [4](#13-3) 

### `StrFormat.h` / `StrFormat.cpp`
类型安全的格式化系统，`namespace fmt`：
- `fmt::Format` — 支持 `%d`/`%s` 和 `{0}`/`{1}` 两种风格的类型安全格式化
- `fmt::FormatTemp` — 返回临时分配字符串的版本 [5](#13-4) 

### `StrQueue.h` / `StrQueue.cpp`
线程安全的字符串队列，用于生产者-消费者场景（如异步目录遍历）：
- `Append` / `PopFront` / `MarkFinished` / `IsFinished` [6](#13-5) 

---

## 内存管理

### `TempAllocator.h` / `TempAllocator.cpp`
线程局部临时分配器，分配的内存在 `ResetTempAllocator()` 时批量释放：
- `GetTempAllocator` / `ResetTempAllocator` / `DestroyTempAllocator`
- `str::DupTemp` / `str::JoinTemp` / `str::FormatTemp` — 分配到临时区的字符串操作
- `ToUtf8Temp` / `ToWStrTemp` — 编码转换到临时区
- `AllocArrayTemp<T>` — 临时数组分配 [7](#13-6) 

### `Vec.h`
通用动态数组模板，仅支持 POD 类型和指针：
- 内置 16 元素栈缓冲区，避免小数组堆分配
- `Append` / `InsertAt` / `RemoveAt` / `RemoveAtFast` / `Pop` / `Find` / `Sort` / `Reverse`
- `StealData` — 转移所有权
- `FreeMembers` — 释放所有指针元素 [8](#13-7) 

### `Scoped.h`
RAII 资源管理模板：
- `ScopedMem<T>` — `malloc` 内存自动释放
- `AutoDelete<T>` — `delete` 对象自动释放
- `AutoFree` / `AutoFreeStr` — `char*` 自动释放
- `AutoFreeWStr` — `WCHAR*` 自动释放
- `AutoRun<T>` — 作用域退出时调用指定函数 [9](#13-8) 

### `ScopedWin.h`
Windows 资源 RAII 封装：
- `ScopedCritSec` — 临界区自动加锁/解锁
- `AutoCloseHandle` — `HANDLE` 自动关闭
- `ScopedComPtr<T>` / `ScopedComQIPtr<T>` — COM 接口引用计数管理
- `AutoDeleteDC` / `AutoReleaseDC` / `ScopedGetDC` — HDC 自动释放
- `ScopedGdiObj<T>` / `AutoDeletePen` / `AutoDeleteBrush` — GDI 对象自动删除
- `ScopedSelectObject` / `ScopedSelectFont` / `ScopedSelectPen` — GDI 对象选入/恢复
- `ScopedCom` / `ScopedOle` / `ScopedGdiPlus` — COM/OLE/GDI+ 初始化/反初始化 [10](#13-9) 

---

## 几何与绘图

### `GeomUtil.h` / `GeomUtil.cpp`
几何基础类型：
- `Point` / `PointF` — 整数/浮点坐标点
- `Size` / `SizeF` — 整数/浮点尺寸
- `Rect` / `RectF` — 整数/浮点矩形，提供 `Contains` / `Intersect` / `Union` / `Inflate` / `Offset`
- 各类型与 `RECT`、`POINT`、`SIZE`、`Gdiplus::Rect` 之间的转换函数 [11](#13-10) 

### `GdiPlusUtil.h` / `GdiPlusUtil.cpp`
GDI+ 辅助函数：
- `MeasureTextAccurate` / `MeasureTextStandard` / `MeasureTextQuick` — 文本尺寸测量
- `GetBaseTransform` — 计算页面变换矩阵（缩放+旋转）
- `BitmapFromDataWin` — 从内存数据创建 `Gdiplus::Bitmap`
- `ImageSizeFromData` / `ImageSizeFromHeader` — 从数据读取图像尺寸
- `LoadRenderedBitmapWin` — 从文件加载为 `RenderedBitmap` [12](#13-11) 

### `ColorUtil.h` / `ColorUtil.cpp`
颜色处理：
- `MkColor` / `UnpackColor` — COLORREF 打包/解包
- `ParseColor` / `SerializeColorTemp` — 颜色字符串解析（`#rrggbb`）与序列化
- `AdjustLightness` / `GetLightness` / `IsLightColor` — 亮度调整与判断
- `MkPdfColor` / `UnpackPdfColor` — PDF 颜色（`aarrggbb`）处理
- `GdiRgbFromCOLORREF` / `Unblend` — GDI+ 颜色转换 [13](#13-12) 

### `Dpi.h` / `Dpi.cpp`
DPI 缩放：
- `DpiGetForHwnd` / `DpiGet` — 获取窗口 DPI
- `DpiScale(HWND, int)` / `DpiScale(HDC, int)` — 将逻辑像素缩放为物理像素 [14](#13-13) 

### `WinUtil.h` / `WinUtil.cpp`
Windows UI 综合工具库（最大的工具文件），主要功能：
- **窗口操作**：`ClientRect` / `WindowRect` / `MoveWindow` / `HwndSetText` / `HwndGetTextTemp` / `HwndScheduleRepaint` / `HwndSendCommand` / `HwndToForeground`
- **绘图**：`DrawRect` / `FillRect` / `DrawLine` / `DrawCenteredText` / `HdcDrawText` / `HdcMeasureText` / `PaintCheckerboard`
- **字体**：`CreateSimpleFont` / `GetDefaultGuiFont` / `GetUserGuiFont` / `DeleteCreatedFonts`
- **注册表**：`ReadRegStr2Temp` / `WriteRegStr` / `ReadRegDWORD` / `WriteRegDWORD` / `DeleteRegKey`
- **进程/文件**：`LaunchProcessWithCmdLine` / `LaunchFileShell` / `LaunchBrowser` / `ResolveLnkTemp` / `CreateShortcut`
- **位图**：`RenderedBitmap` / `CreateMemoryBitmap` / `SerializeBitmap` / `GetBitmapPixels`
- **双缓冲**：`DoubleBuffer` — 防闪烁绘图
- **菜单**：`MenuSetChecked` / `MenuSetEnabled` / `MenuSetText`
- **剪贴板**：`CopyTextToClipboard` / `CopyImageToClipboard`
- **键盘**：`IsKeyPressed` / `IsShiftPressed` / `IsCtrlPressed`
- **屏幕**：`ShiftRectToWorkArea` / `GetFullscreenRect` / `GetVirtualScreenRect`
- **DDE**：`DDEExecute`
- **CPU 特性**：`CpuID` / `LatestSupportedSIMD`
- **时间**：`TimeNow` / `TimeDiffMs` [15](#13-14) 

### `windrawlib.h` / `windrawlib.cpp`
Direct2D / WIC / DirectWrite 的底层封装，提供 `WD_HCANVAS`、`WD_HFONT`、`WD_HIMAGE` 等句柄类型，以及 `wdInitialize` / `wdTerminate`。 [16](#13-15) 

---

## 文件系统

### `FileUtil.h` / `FileUtil.cpp`
文件和路径操作，分三个命名空间：

**`namespace path`**：路径字符串操作
- `GetExtTemp` / `GetBaseNameTemp` / `GetDirTemp` — 提取扩展名/文件名/目录
- `Join` / `JoinTemp` — 路径拼接
- `NormalizeTemp` / `IsSame` — 路径规范化与比较
- `IsOnFixedDrive` / `IsOnNetworkDrive` / `IsCloudPlaceholder` — 驱动器类型检测
- `IsAbsolute` / `IsDirectory` / `Match` — 路径属性判断

**`namespace file`**：文件 I/O
- `ReadFile` / `WriteFile` / `ReadN` — 文件读写
- `GetSize` — 获取文件大小
- `Delete` / `DeleteFileToTrash` / `Copy` / `Rename` — 文件操作
- `GetModificationTime` / `SetModificationTime` — 修改时间
- `GetZoneIdentifier` / `DeleteZoneIdentifier` — Mark of the Web 操作

**`namespace dir`**：目录操作
- `Create` / `CreateAll` / `RemoveAll` / `HasWriteAccess` [17](#13-16) 

### `DirIter.h` / `DirIter.cpp`
目录遍历：
- `DirIter` — range-for 风格的目录迭代器，支持递归、文件/目录过滤
- `StartDirTraverseAsync` — 异步后台遍历，结果写入 `StrQueue`
- `DirTraverse` — 同步遍历，接受 lambda 回调 [18](#13-17) 

### `FileWatcher.h` / `FileWatcher.cpp`
文件变更监听：
- `FileWatcherSubscribe` — 订阅文件变更，变更时调用回调
- `FileWatcherUnsubscribe` — 取消订阅
- `WatchedFileSetIgnore` — 临时忽略变更通知 [19](#13-18) 

---

## 压缩与归档

### `Archive.h` / `Archive.cpp`
多格式归档读取（ZIP/RAR/7Z/TAR），通过 libarchive 和 unrar.dll：
- `MultiFormatArchive::Open` — 打开归档文件
- `GetFileInfos` / `GetFileDataByName` / `GetFileDataById` — 枚举和提取文件
- `GetFileDataPartById` — 部分提取（用于大文件）
- `OpenArchiveFromFile` / `OpenArchiveFromStream` — 工厂函数 [20](#13-19) 

### `ZipUtil.h` / `ZipUtil.cpp`
ZIP 创建与 gzip 解压：
- `ZipCreator` — 创建 ZIP 文件，支持 `AddFile` / `AddDir` / `Finish`
- `OpenDirAsZipStream` — 将目录作为 ZIP 流打开
- `Ungzip` — 解压 gzip 数据 [21](#13-20) 

### `LzmaSimpleArchive.h` / `LzmaSimpleArchive.cpp`
LZMA 简单归档（用于更新包），`namespace lzma`：
- `ParseSimpleArchive` — 解析归档头
- `GetFileDataByName` / `GetFileDataByIdx` — 按名称/索引提取文件
- `ExtractFiles` — 批量解压到目录 [22](#13-21) 

---

## 解析器

### `HtmlPullParser.h` / `HtmlPullParser.cpp`
流式 HTML 拉取解析器：
- `HtmlPullParser::Next()` — 逐 token 解析，返回 `HtmlToken`（开始标签/结束标签/文本/错误）
- `HtmlToken::GetAttrByName` / `NextAttr` — 属性访问
- `ResolveHtmlEntities` — HTML 实体解码
- `HtmlEntityNameToRune` — 实体名称转 Unicode 码点 [23](#13-22) 

### `TrivialHtmlParser.h` / `TrivialHtmlParser.cpp`
DOM 风格 HTML 解析器（用于 CHM/EPUB 等）：
- `HtmlParser::Parse` / `ParseInPlace` — 解析 HTML 为树结构
- `FindElementByName` / `FindElementByNameNS` — 按标签名查找元素
- `HtmlElement::GetAttribute` / `GetChildByTag` — 属性和子元素访问 [24](#13-23) 

### `HtmlParserLookup.h` / `HtmlParserLookup.cpp`
自动生成的 HTML/CSS 查找表：
- `FindHtmlTag` — 标签名字符串 → `HtmlTag` 枚举
- `IsTagSelfClosing` / `IsInlineTag` — 标签属性查询
- `FindCssProp` — CSS 属性名 → `CssProp` 枚举
- `FindHtmlEntityRune` — HTML 实体名 → Unicode 码点 [25](#13-24) 

### `HtmlPrettyPrint.h` / `HtmlPrettyPrint.cpp`
- `PrettyPrintHtml` — 格式化输出 HTML（用于调试）

### `CssParser.h` / `CssParser.cpp`
CSS 拉取解析器：
- `CssPullParser::NextRule` / `NextSelector` / `NextProperty` — 逐规则/选择器/属性解析 [26](#13-25) 

### `JsonParser.h` / `JsonParser.cpp`
JSON 推送解析器，`namespace json`：
- `json::Parse` — 解析 JSON，对每个原始值调用 `ValueVisitor::Visit(path, value, type)` [27](#13-26) 

### `SquareTreeParser.h` / `SquareTreeParser.cpp`
SumatraPDF 设置文件格式（`key = value` / `key [ ... ]`）的解析器：
- `ParseSquareTree` — 解析为 `SquareTreeNode` 树
- `SquareTreeNode::GetValue` / `GetChild` — 值/子节点访问
- `SerializeSquareTreeNode` — 序列化回字符串 [28](#13-27) 

### `SettingsUtil.h` / `SettingsUtil.cpp`
设置序列化/反序列化框架：
- `SerializeStruct` — 将 C++ 结构体序列化为设置文件格式
- `DeserializeStruct` — 从设置文件反序列化到结构体
- `FreeStruct` — 释放结构体中的字符串字段
- `FieldInfo` / `StructInfo` — 描述结构体字段布局的元数据 [29](#13-28) 

---

## 二进制数据读写

### `ByteReader.h` / `ByteReader.cpp`
从内存缓冲区按偏移读取各种整数类型：
- `Byte` / `WordLE` / `WordBE` / `DWordLE` / `DWordBE` / `QWordLE` / `QWordBE`
- `Unpack` — 按格式字符串批量解包结构体 [30](#13-29) 

### `ByteWriter.h` / `ByteWriter.cpp`
向 `StrBuilder` 写入各种整数类型：
- `Write8` / `Write16` / `Write32` / `Write64`
- `ByteWriterLE` — 小端字节序版本 [31](#13-30) 

### `ByteOrderDecoder.h` / `ByteOrderDecoder.cpp`
流式字节序解码器，支持大端/小端：
- `UInt8` / `UInt16` / `UInt32` / `UInt64` / `Int16` / `Int32` / `Int64`
- `Skip` / `Unskip` / `Offset` / `IsOk` [32](#13-31) 

### `BitReader.h` / `BitReader.cpp`
位级读取器：
- `Peek(n)` — 查看接下来 n 位
- `Eat(n)` — 消耗 n 位
- `BitsLeft` — 剩余位数 [33](#13-32) 

### `BitManip.h`
位操作模板，`namespace bit` / `namespace bitmask`：
- `bit::Set` / `bit::Clear` / `bit::IsSet` / `bit::FromBit`
- `bitmask::IsSet` / `bitmask::IsClear` [34](#13-33) 

---

## 图像格式

### `AvifReader.h` / `AvifReader.cpp`
AVIF 图像解码：
- `AvifSizeFromData` — 读取图像尺寸
- `AvifImageFromData` — 解码为 `Gdiplus::Bitmap`

### `WebpReader.h` / `WebpReader.cpp`
WebP 图像解码，`namespace webp`：
- `HasSignature` / `SizeFromData` / `ImageFromData`

### `TgaReader.h` / `TgaReader.cpp`
TGA 图像读写，`namespace tga`：
- `HasSignature` / `ImageFromData` — 解码
- `SerializeBitmap` — 将 `HBITMAP` 序列化为 TGA 格式

---

## 线程与异步

### `ThreadUtil.h` / `ThreadUtil.cpp`
线程工具：
- `Mutex` — `CRITICAL_SECTION` 的 RAII 封装
- `RunAsync` — 在新线程上运行 `Func0`
- `StartThread` — 启动线程并返回 `HANDLE`
- `SetThreadName` — 设置线程名称（调试用） [35](#13-34) 

### `UITask.h` / `UITask.cpp`
UI 线程任务队列，`namespace uitask`：
- `Post` — 从任意线程投递任务到 UI 线程执行
- `PostOptimized` — 同类型任务去重投递
- `DrainQueue` — 处理所有待执行任务
- `IsMainUIThread` — 判断是否在 UI 线程 [36](#13-35) 

---

## 网络

### `HttpUtil.h` / `HttpUtil.cpp`
HTTP 客户端（基于 WinINet）：
- `HttpGet` — 同步 GET 请求
- `HttpPost` — 同步 POST 请求
- `HttpGetToFile` — 下载文件到磁盘，支持进度回调 [37](#13-36) 

---

## 加密与哈希

### `CryptoUtil.h` / `CryptoUtil.cpp`
- `CalcMD5Digest` / `CalcSHA1Digest` / `CalcSHA2Digest` — 哈希计算
- `VerifySHA1Signature` — 验证 SHA1 签名（用于更新包验证）
- `ExtractP7m` — 从 PKCS#7 包装中提取内容 [38](#13-37) 

---

## 调试与诊断

### `Log.h` / `Log.cpp`
日志系统：
- `log` / `logf` — 普通日志（受 `gReducedLogging` 控制）
- `logv` / `logvf` — 详细日志
- `loga` / `logfa` — 始终输出的日志
- `StartLogToFile` / `WriteCurrentLogToFile` — 日志写入文件
- 支持输出到控制台、调试器、管道 [39](#13-38) 

### `DbgHelpDyn.h` / `DbgHelpDyn.cpp`
调试符号与崩溃信息，`namespace dbghelp`：
- `Initialize` — 初始化符号引擎
- `GetCurrentThreadCallstack` / `GetAllThreadsCallstacks` — 获取调用栈
- `WriteMiniDump` — 写入 minidump 文件
- `GetExceptionInfo` — 格式化异常信息 [40](#13-39) 

### `UtAssert.h` / `UtAssert.cpp`
单元测试断言：
- `utassert(expr)` — 非交互式断言，记录失败次数
- `utassert_print_results` — 打印测试结果 [41](#13-40) 

---

## 其他工具

### `GuessFileType.h` / `GuessFileType.cpp`
文件类型检测：
- `GuessFileTypeFromContent` — 通过文件内容（magic bytes）检测类型
- `GuessFileTypeFromName` — 通过文件扩展名检测类型
- `GuessFileType` — 综合检测
- 定义了所有支持格式的 `Kind` 常量（`kindFilePDF`、`kindFileCbz` 等） [42](#13-41) 

### `CmdLineArgsIter.h` / `CmdLineArgsIter.cpp`
命令行参数解析：
- `ParseCmdLine` — 将命令行字符串解析为 `StrVec`
- `CmdLineArgsIter` — 迭代器，提供 `NextArg` / `EatParam` / `AdditionalParam` [43](#13-42) 

### `Dict.h` / `Dict.cpp`
哈希表，`namespace dict`：
- `MapStrToInt` — `char*` → `int` 的哈希映射，支持 `Insert` / `Get` / `Remove` [44](#13-43) 

### `Timer.h`
高精度计时器（header-only）：
- `TimeGet` — 获取当前时间戳（`QueryPerformanceCounter`）
- `TimeSinceInMs` — 计算自某时刻起经过的毫秒数 [45](#13-44) 

### `FrameTimeoutCalculator.h`
动画帧率控制（header-only）：
- `GetTimeoutInMilliseconds` — 返回距下一帧还需等待的毫秒数
- `Step` — 推进到下一帧时间点 [46](#13-45) 

### `WinDynCalls.h` / `WinDynCalls.cpp`
动态加载 Windows API 的集中管理：
- 为 `ntdll`、`kernel32`、`user32`、`uxtheme`、`dwmapi`、`dbghelp` 等 DLL 中的函数定义 `Dyn*` 函数指针
- `InitDynCalls` — 统一加载所有动态函数
- `theme::` / `dwm::` / `touch::` 命名空间提供便捷封装 [47](#13-46)

### Citations

**File:** src/utils/BaseUtil.h (L1-50)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#ifndef BaseUtil_h
#define BaseUtil_h

/* OS_DARWIN - Any Darwin-based OS, including Mac OS X and iPhone OS */
#ifdef __APPLE__
#define OS_DARWIN 1
#else
#define OS_DARWIN 0
#endif

/* OS_LINUX - Linux */
#ifdef __linux__
#define OS_LINUX 1
#else
#define OS_LINUX 0
#endif

#if defined(WIN32) || defined(_WIN32)
#define OS_WIN 1
#else
#define OS_WIN 0
#endif

// https://learn.microsoft.com/en-us/cpp/preprocessor/predefined-macros
#if defined(_M_IX86) || defined(__i386__)
#define IS_INTEL_32 1
#define IS_INTEL_64 0
#define IS_ARM_64 0
#elif defined(_M_X64) || defined(__x86_64__)
#define IS_INTEL_64 1
#define IS_INTEL_32 0
#define IS_ARM_64 0
#elif defined(_M_ARM64)
#define IS_INTEL_64 0
#define IS_INTEL_32 0
#define IS_ARM_64 1
#else
#error "unsupported arch"
#endif

/* OS_UNIX - Any Unix-like system */
#if OS_DARWIN || OS_LINUX || defined(unix) || defined(__unix) || defined(__unix__)
#define OS_UNIX 1
#endif

#if defined(_MSC_VER)
#define COMPILER_MSVC 1
```

**File:** src/utils/StrUtil.h (L87-200)
```text
namespace str {

enum class TrimOpt {
    Left,
    Right,
    Both
};

size_t Len(const WCHAR*);
size_t Len(const char* s);

int Leni(const WCHAR*);
int Leni(const char* s);

void Free(const char*);
void Free(const u8*);
void Free(const StrSpan& s);

void Free(const WCHAR* s);

void FreePtr(const char** s);
void FreePtr(char** s);
void FreePtr(const WCHAR** s);
void FreePtr(WCHAR** s);

char* Dup(Arena*, const char* str, size_t cch = (size_t)-1);
char* Dup(const char* s, size_t cch = (size_t)-1);
char* Dup(const ByteSlice&);

void ReplacePtr(const char** s, const char* snew);
void ReplacePtr(char** s, const char* snew);

void ReplaceWithCopy(const char** s, const char* snew);
void ReplaceWithCopy(const char** s, const ByteSlice&);
void ReplaceWithCopy(char** s, const char* snew);

char* Join(Arena*, const char*, const char*, const char*);
char* Join(Arena*, const char*, const char*, const char*, const char*, const char*);
char* Join(const char* s1, const char* s2, const char* s3 = nullptr);

bool Eq(const char* s1, const char* s2);
bool Eq(const ByteSlice& sp1, const ByteSlice& sp2);
bool EqI(const char* s1, const char* s2);
bool EqIS(const char* s1, const char* s2);
bool EqN(const char* s1, const char* s2, size_t len);
bool EqNI(const char* s1, const char* s2, size_t len);
bool IsEmpty(const char* s);
bool StartsWith(const char* str, const char* prefix);
bool StartsWith(const u8* str, const char* prefix);

bool StartsWithI(const char* str, const char* prefix);
bool EndsWith(const char* txt, const char* end);
bool EndsWithI(const char* txt, const char* end);
bool EqNIx(const char* s, size_t len, const char* s2);

char* ToLowerInPlace(char*);

char* ToLower(const char*);

char* ToUpperInPlace(char*);

void Utf8Encode(char*& dst, int c);

bool IsDigit(char c);
bool IsWs(char c);
bool IsAlNum(char c);

const char* FindChar(const char* str, char c);
char* FindChar(char* str, char c);
int FindCharIdx(const char* str, char c);
const char* FindCharLast(const char* str, char c);
char* FindCharLast(char* str, char c);
const char* Find(const char* str, const char* find);
const char* FindI(const char* str, const char* find);
int BufFind(const char* buf, int bufSize, const char* toFind);

bool Contains(const char* s, const char* txt);
bool ContainsI(const char* s, const char* txt);

bool BufFmtV(char* buf, size_t bufCchSize, const char* fmt, va_list args);
bool BufFmt(char* buf, size_t bufCchSize, const char* fmt, ...);
char* FmtVWithAllocator(Arena* a, const char* fmt, va_list args);
char* FmtV(const char* fmt, va_list args);
char* Format(const char* fmt, ...);

size_t TrimWSInPlace(char* s, TrimOpt opt);
void TrimWsEnd(char* s, char*& e);

size_t TransCharsInPlace(char* str, const char* oldChars, const char* newChars);

size_t NormalizeWSInPlace(char* str);
size_t NormalizeNewlinesInPlace(char* s, char* e);
size_t NormalizeNewlinesInPlace(char* s);
size_t RemoveCharsInPlace(char* str, const char* toRemove);

int BufSet(char* dst, int dstCchSize, const char* src);
int BufAppend(char* dst, int dstCchSize, const char* s);

char* MemToHex(const u8* buf, size_t len);
bool HexToMem(const char* s, u8* buf, size_t bufLen);

const char* Parse(const char* str, const char* fmt, ...);
const char* Parse(const char* str, size_t len, const char* fmt, ...);

int CmpNatural(const char*, const char*);

TempStr FormatFloatWithThousandSepTemp(double number, LCID locale = LOCALE_USER_DEFAULT, bool stripTrailingZero = true);
TempStr FormatNumWithThousandSepTemp(i64 num, LCID locale = LOCALE_USER_DEFAULT);
TempStr FormatSizeShortTemp(i64 size, const char* sizeUnits[3]);
TempStr FormatFileSizeTemp(i64);
TempStr FormatRomanNumeralTemp(int number);

bool IsEmptyOrWhiteSpace(const char*);
bool Skip(const char*& s, const char* toSkip);
```

**File:** src/utils/StrVec.h (L16-68)
```text
struct StrVec {
    StrVecPage* first = nullptr;
    int* sortIndexes = nullptr;
    int nextPageSize = 256;
    int size = 0;
    int dataSize = 0;

    StrVec() = default;
    StrVec(int dataSize);
    StrVec(const StrVec& that);
    StrVec& operator=(const StrVec& that);
    ~StrVec();

    void Reset(StrVecPage* = nullptr);

    int Size() const;
    bool IsEmpty() const;
    char* At(int i) const;
    StrSpan AtSpan(int i) const;
    void* AtDataRaw(int i) const;
    char* operator[](int) const;

    char* Append(const char*, int sLen = -1);
    char* SetAt(int idx, const char*, int sLen = -1);
    char* InsertAt(int, const char*, int sLen = -1);
    char* RemoveAt(int);
    char* RemoveAtFast(int);
    bool Remove(const char*);

    int Find(const StrSpan&, int startAt = 0) const;
    int FindI(const StrSpan&, int startAt = 0) const;
    bool Contains(const char*, int sLen = -1) const;

    struct iterator {
        const StrVec* v;
        int idx;

        // perf: cache page, idxInPage from prev iteration
        int idxInPage;
        StrVecPage* page;

        iterator(const StrVec* v, int idx);
        char* operator*() const;
        StrSpan Span() const;
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

**File:** src/utils/StrconvUtil.h (L1-28)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

namespace strconv {

WCHAR* Utf8ToWStr(const char* s, size_t cb = (size_t)-1, Arena* a = nullptr);
char* WStrToUtf8(const WCHAR* s, size_t cch = (size_t)-1, Arena* a = nullptr);

char* WStrToCodePage(uint codePage, const WCHAR* s, size_t cch = (size_t)-1, Arena* a = nullptr);
TempStr ToMultiByteTemp(const char* src, uint codePageSrc, uint codePageDest);
WCHAR* StrCPToWStr(const char* src, uint codePage, int cbSrc = -1);
TempWStr StrCPToWStrTemp(const char* src, uint codePage, int cbSrc = -1);
TempStr StrToUtf8Temp(const char* src, uint codePage);

TempStr UnknownToUtf8Temp(const char*, size_t cb = (size_t)-1);

char* WStrToAnsi(const WCHAR*);
char* Utf8ToAnsi(const char*);

TempWStr AnsiToWStrTemp(const char* src, size_t cbLen = (size_t)-1);
char* AnsiToUtf8(const char* src, size_t cbLen = (size_t)-1);
TempStr AnsiToUtf8Temp(const char* src, size_t cbLen);
} // namespace strconv

// shorter names
// TODO: eventually we want to migrate all strconv:: to them
char* ToUtf8(const WCHAR* s, size_t cch = (size_t)-1);
WCHAR* ToWStr(const char* s, size_t cb = (size_t)-1);
```

**File:** src/utils/StrFormat.h (L38-120)
```text
namespace fmt {

enum class Type {
    // concrete types for Arg
    Char,
    Int,
    Float,
    Double,
    Str,
    WStr,

    // for Inst.t
    RawStr, // copy part of format string
    Any,

    None,
};

// argument to a formatting instruction
// at the front are arguments given with i(), s() etc.
// at the end are FormatStr arguments from format string
struct Arg {
    Type t{Type::None};
    union {
        char c;
        i64 i;
        float f;
        double d;
        const char* s;
        const WCHAR* ws;
    } u{};

    Arg() = default;

    Arg(char c) {
        t = Type::Char;
        u.c = c;
    }

    Arg(int arg) {
        t = Type::Int;
        u.i = (i64)arg;
    }

    Arg(size_t arg) {
        t = Type::Int;
        u.i = (i64)arg;
    }

    Arg(i64 arg) {
        t = Type::Int;
        u.i = arg;
    }

    Arg(float f) {
        t = Type::Float;
        u.f = f;
    }

    Arg(double d) {
        t = Type::Double;
        u.d = d;
    }

    Arg(const char* arg) {
        t = Type::Str;
        u.s = arg;
    }

    Arg(const WCHAR* arg) {
        t = Type::WStr;
        u.ws = arg;
    }
};

char* Format(const char* s, const Arg& a1 = Arg(), const Arg& a2 = Arg(), const Arg& a3 = Arg(), const Arg& a4 = Arg(),
             const Arg& a5 = Arg(), const Arg& a6 = Arg());

char* FormatTemp(const char* s, const Arg);
char* FormatTemp(const char* s, const Arg, const Arg);
char* FormatTemp(const char* s, const Arg, const Arg, const Arg);

} // namespace fmt
```

**File:** src/utils/StrQueue.h (L1-24)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// multi-threaded queue of strings
struct StrQueue {
    StrQueue();
    ~StrQueue();

    void Lock();
    void Unlock();
    int Size();
    char* Append(const char*, int len = -1);
    char* PopFront();
    bool IsSentinel(char*);
    void MarkFinished();
    bool IsFinished();
    bool Access(const Func1<StrQueue*>& fn);

    StrVec strings;

    volatile bool isFinished = false;
    CRITICAL_SECTION cs;
    HANDLE hEvent = nullptr;
};
```

**File:** src/utils/TempAllocator.h (L1-36)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// note: include BaseUtil.h instead of including directly

Arena* GetTempAllocator();
void DestroyTempAllocator();
void ResetTempAllocator();

template <typename T>
FORCEINLINE T* AllocArrayTemp(size_t n) {
    if (!mulSafe<size_t>(&n, sizeof(T))) {
        return nullptr;
    }
    auto a = GetTempAllocator();
    return (T*)AllocZero(a, n);
}

namespace str {
TempStr DupTemp(const char* s, size_t cb = (size_t)-1);
TempWStr DupTemp(const WCHAR* s, size_t cch = (size_t)-1);

TempStr JoinTemp(const char* s1, const char* s2, const char* s3 = nullptr);
TempStr JoinTemp(const char* s1, const char* s2, const char* s3, const char* s4);
TempStr JoinTemp(const char* s1, const char* s2, const char* s3, const char* s4, const char* s5);
TempWStr JoinTemp(const WCHAR* s1, const WCHAR* s2, const WCHAR* s3 = nullptr);

TempStr ReplaceTemp(const char* s, const char* toReplace, const char* replaceWith);
TempStr ReplaceNoCaseTemp(const char* s, const char* toReplace, const char* replaceWith);

TempStr FormatTemp(const char* fmt, ...);
} // namespace str

TempStr ToUtf8Temp(const WCHAR* s, size_t cch = (size_t)-1);
TempWStr ToWStrTemp(const char* s, size_t cb = (size_t)-1);
TempWStr ToWStrTemp(const StrBuilder& s);
```

**File:** src/utils/Vec.h (L11-30)
```text
template <typename T>
class Vec {
  public:
    Arena* allocator = nullptr;
    size_t len = 0;
    size_t cap = 0;
    size_t capacityHint = 0;
    T* els = nullptr;
    T buf[16];

    // We always pad the elements with a single 0 value. This makes
    // Vec<char> and Vec<WCHAR> a C-compatible string. Although it's
    // not useful for other types, the code is simpler if we always do it
    // (rather than have it an optional behavior).
    static constexpr size_t kPadding = 1;
    static constexpr size_t kElSize = sizeof(T);

  private:
    NO_INLINE bool EnsureCapSlow(size_t needed) {
        size_t newCap = cap * 2;
```

**File:** src/utils/Scoped.h (L1-50)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// include BaseUtil.h instead of including directly

// auto-free memory for arbitrary malloc()ed memory of type T*
template <typename T>
class ScopedMem {
  public:
    T* ptr = nullptr;

    ScopedMem() = default;
    explicit ScopedMem(T* ptr) : ptr(ptr) {}
    ~ScopedMem() { free(ptr); }
    void Set(T* newPtr) {
        free(ptr);
        ptr = newPtr;
    }
    T* Get() const { return ptr; }
    T* StealData() {
        T* tmp = ptr;
        ptr = nullptr;
        return tmp;
    }
    operator T*() const { // NOLINT
        return ptr;
    }
};

// deletes an object at the end of the scope
template <typename T>
struct AutoDelete {
    T* o = nullptr;
    AutoDelete() = default;
    AutoDelete(T* p) { // NOLINT
        o = p;
    }
    ~AutoDelete() { delete o; }

    AutoDelete& operator=(AutoDelete& other) = delete;
    AutoDelete& operator=(AutoDelete&& other) = delete;
    AutoDelete& operator=(const AutoDelete& other) = delete;
    AutoDelete& operator=(const AutoDelete&& other) = delete;
    operator T*() const { // NOLINT
        return o;
    }
    T* operator->() const { // NOLINT
        return o;
    }
};
```

**File:** src/utils/ScopedWin.h (L1-50)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct ScopedCritSec {
    CRITICAL_SECTION* cs = nullptr;

    explicit ScopedCritSec(CRITICAL_SECTION* cs) : cs(cs) { EnterCriticalSection(cs); }
    ~ScopedCritSec() { LeaveCriticalSection(cs); }
};

class AutoCloseHandle {
    HANDLE handle = nullptr;

  public:
    AutoCloseHandle() = default;

    AutoCloseHandle(HANDLE h) : handle(h) {}

    ~AutoCloseHandle() {
        if (IsValid()) {
            CloseHandle(handle);
        }
    }

    AutoCloseHandle& operator=(HANDLE h) {
        ReportIf(handle != nullptr);
        ReportIf(h == nullptr);
        handle = h;
        return *this;
    }

    operator HANDLE() const { // NOLINT
        return handle;
    }

    bool IsValid() const { return handle != nullptr && handle != INVALID_HANDLE_VALUE; }
};

template <class T>
class ScopedComPtr {
  protected:
    T* ptr = nullptr;

  public:
    ScopedComPtr() = default;

    explicit ScopedComPtr(T* ptr) : ptr(ptr) {}
    ~ScopedComPtr() {
        if (ptr) {
            ptr->Release();
```

**File:** src/utils/GeomUtil.h (L59-97)
```text
struct Rect {
    int x = 0;
    int y = 0;
    int dx = 0;
    int dy = 0;

    Rect() = default;
    Rect(RECT r);           // NOLINT
    Rect(Gdiplus::RectF r); // NOLINT
    Rect(int x, int y, int dx, int dy);
    // TODO: why not working if in .cpp? Confused by Size also being a method?
    Rect(const Point pt, const Size sz) : x(pt.x), y(pt.y), dx(sz.dx), dy(sz.dy) {}
    Rect(Point min, Point max);

    bool EqSize(int otherDx, int otherDy) const;
    int Right() const;
    int Bottom() const;
    static Rect FromXY(int xs, int ys, int xe, int ye);
    static Rect FromXY(const Point TL, const Point BR);
    bool IsZero() const;
    bool IsEmpty() const;
    bool Contains(int x, int y) const;
    bool Contains(const Point pt) const;
    Rect Intersect(const Rect other) const;
    Rect Union(Rect other) const;
    void Offset(int _x, int _y);
    void Inflate(int _x, int _y);
    void SubTB(int t, int b);
    void SubLR(int l, int r);
    Point TL() const;
    Point BR() const;
    Size Size() const;
    void SetSize(const struct Size&);
    void SetPos(const Point&);
    bool Equals(const Rect& other) const;
    bool operator==(const Rect& other) const;
    bool operator!=(const Rect& other) const;
};

```

**File:** src/utils/GdiPlusUtil.h (L1-25)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct RenderedBitmap;

Gdiplus::RectF RectToRectF(Gdiplus::Rect r);

typedef RectF (*TextMeasureAlgorithm)(Gdiplus::Graphics* g, Gdiplus::Font* f, const WCHAR* s, int len);

RectF MeasureTextAccurate(Gdiplus::Graphics* g, Gdiplus::Font* f, const WCHAR* s, int len);
RectF MeasureTextStandard(Gdiplus::Graphics* g, Gdiplus::Font* f, const WCHAR* s, int len);
RectF MeasureTextQuick(Gdiplus::Graphics* g, Gdiplus::Font* f, const WCHAR* s, int len);
RectF MeasureText(Gdiplus::Graphics* g, Gdiplus::Font* f, const WCHAR* s, size_t len = -1,
                  TextMeasureAlgorithm algo = nullptr);
// float     GetSpaceDx(Graphics *g, Font *f, TextMeasureAlgorithm algo=nullptr);
// size_t   StringLenForWidth(Graphics *g, Font *f, const WCHAR *s, size_t len, float dx, TextMeasureAlgorithm
// algo=nullptr);

void GetBaseTransform(Gdiplus::Matrix& m, Gdiplus::RectF pageRect, float zoom, int rotation);

Gdiplus::Bitmap* BitmapFromDataWin(const ByteSlice& bmpData);
Size ImageSizeFromData(const ByteSlice&);
Size ImageSizeFromHeader(const ByteSlice&);
CLSID GetGdiPlusEncoderClsid(const WCHAR* format);
RenderedBitmap* LoadRenderedBitmapWin(const char* path);
```

**File:** src/utils/ColorUtil.h (L1-59)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// note: include BaseUtil.h instead of including directly

// a "unset" state for COLORREF value. technically all colors are valid
// this one is hopefully not used in practice
constexpr COLORREF kColorUnset = ((COLORREF)(0xfeffffff));
// kColorNoChange indicates that we shouldn't change the color
constexpr COLORREF kColorNoChange((COLORREF)(0xfdffffff));

// PdfColor is aarrggbb, where 0xff alpha is opaque and 0x0 alpha is transparent
// this is different than COLORREF, which ggrrbb and no alpha
using PdfColor = uint64_t;

struct ParsedColor {
    bool wasParsed = false;
    bool parsedOk = false;
    COLORREF col = 0;
    PdfColor pdfCol = 0;
};

COLORREF MkGray(u8 x);
COLORREF MkColor(u8 r, u8 g, u8 b, u8 a = 0);
void UnpackColor(COLORREF, u8& r, u8& g, u8& b);
void UnpackColor(COLORREF, u8& r, u8& g, u8& b, u8& a);

bool IsSpecialColor(COLORREF col);

void ParseColor(ParsedColor& parsed, const char* txt);
bool ParseColor(COLORREF* destColor, const char* s);
COLORREF ParseColor(const char* s, COLORREF defCol = 0);
TempStr SerializeColorTemp(COLORREF);

PdfColor MkPdfColor(u8 r, u8 g, u8 b, u8 a = 0xff); // 0xff is opaque
void UnpackPdfColor(PdfColor, u8& r, u8& g, u8& b, u8& a);
void SerializePdfColor(PdfColor c, StrBuilder& out);

COLORREF AdjustLightness(COLORREF c, float factor);
COLORREF AdjustLightness2(COLORREF c, float units);
float GetLightness(COLORREF c);
bool IsLightColor(COLORREF c);

// TODO: use AdjustLightness instead to compensate for the alpha?
Gdiplus::Color Unblend(COLORREF c, u8 alpha);
Gdiplus::Color GdiRgbFromCOLORREF(COLORREF c);
Gdiplus::Color GdiRgbaFromCOLORREF(COLORREF c);

constexpr COLORREF RgbToCOLORREF(COLORREF rgb) {
    return ((rgb & 0x0000FF) << 16) | (rgb & 0x00FF00) | ((rgb & 0xFF0000) >> 16);
}

/* In debug mode, VS 2010 instrumentations complains about GetRValue() etc.
This adds equivalent functions that don't have this problem and ugly
substitutions to make sure we don't use Get*Value() in the future */
u8 GetRed(COLORREF rgb);
u8 GetGreen(COLORREF rgb);
u8 GetBlue(COLORREF rgb);
u8 GetAlpha(COLORREF rgb);
```

**File:** src/utils/Dpi.h (L1-9)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

int DpiGetForHwnd(HWND);
int DpiGet(HWND);
int DpiScale(HWND, int);
void DpiScale(HWND, int&, int&);

int DpiScale(HDC, int x);
```

**File:** src/utils/WinUtil.h (L127-132)
```text
int HdcDrawText(HDC hdc, const char* s, RECT* r, uint format, HFONT font = nullptr);
int HdcDrawText(HDC hdc, const char* s, const Rect& r, uint format, HFONT font = nullptr);
int HdcDrawText(HDC hdc, const char* s, const Point& pos, uint fmt, HFONT font = nullptr);
Size HdcMeasureText(HDC hdc, const char* s, uint format, HFONT font);
Size HdcMeasureText(HDC hdc, const char* s, HFONT font = nullptr);

```

**File:** src/utils/windrawlib.h (L106-121)
```text
int d2d_init(void);
void d2d_fini(void);
void d2d_init_color(D2D1_COLOR_F* c, WD_COLOR color);
void d2d_matrix_mult(D2D1_MATRIX_3X2_F* res, const D2D1_MATRIX_3X2_F* a, const D2D1_MATRIX_3X2_F* b);

d2d_canvas_t* d2d_canvas_alloc(ID2D1RenderTarget* target, WORD type, UINT width, BOOL rtl);
void d2d_reset_transform(d2d_canvas_t* c);
void d2d_reset_clip(d2d_canvas_t* c);
void d2d_apply_transform(d2d_canvas_t* c, const D2D1_MATRIX_3X2_F* matrix);

IWICBitmapSource* wic_convert_bitmap(IWICBitmapSource* bitmap);

void dwrite_default_user_locale(WCHAR buffer[LOCALE_NAME_MAX_LENGTH]);

bool wdInitialize();
void wdTerminate();
```

**File:** src/utils/FileUtil.h (L54-103)
```text
namespace file {

bool Exists(const char* path);

FILE* OpenFILE(const char* path);
HANDLE OpenReadOnly(const char*);
ByteSlice ReadFileWithAllocator(const char* path, Arena*);
ByteSlice ReadFile(const char* path);
int ReadN(const char* path, char* buf, size_t toRead);
bool WriteFile(const char* path, const ByteSlice&);

i64 GetSize(HANDLE h);
i64 GetSize(const char*);
bool Delete(const char* path);
bool DeleteFileToTrash(const char* path);

FILETIME GetModificationTime(const char* path);

bool SetModificationTime(const char* path, FILETIME lastMod);

DWORD GetAttributes(const char* path);
bool SetAttributes(const char* path, DWORD attrs);

bool StartsWithN(const char* path, const char* s, size_t len);
bool StartsWith(const char* path, const char* s);

int GetZoneIdentifier(const char* path);
bool SetZoneIdentifier(const char* path, int zoneId = URLZONE_INTERNET);
bool DeleteZoneIdentifier(const char* path);

// Progress reported by Copy(). bytesTotal == 0 means "total not known".
struct CopyProgress {
    i64 bytesCopied;
    i64 bytesTotal;
};
using CopyProgressCb = Func1<CopyProgress*>;

// Thread-local progress callback honored by long-running copies (e.g.
// caching a network-drive cbx locally). Caller sets it before triggering
// an operation that may copy large files; clears it afterwards.
extern thread_local CopyProgressCb gFileCopyProgressCb;

bool Copy(const char* dst, const char* src, bool dontOverwrite);
bool Copy(const char* dst, const char* src, bool dontOverwrite, const CopyProgressCb& cbProgress);
bool Rename(const char* newPath, const char* oldPath);

bool SetAccessTime(const char* path, FILETIME accessTime);
FILETIME GetAccessTime(const char* path);

} // namespace file
```

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

**File:** src/utils/FileWatcher.h (L1-10)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct WatchedFile;

void FileWatcherInit(void);
WatchedFile* FileWatcherSubscribe(const char* path, const Func0& onFileChangedCb, bool enableManualCheck = false);
void FileWatcherUnsubscribe(WatchedFile* wf);
void FileWatcherWaitForShutdown(void);
void WatchedFileSetIgnore(WatchedFile* wf, bool ignore);
```

**File:** src/utils/Archive.h (L20-85)
```text
class MultiFormatArchive {
  public:
    enum class Format {
        Unknown,
        Zip,
        Rar,
        SevenZip,
        Tar
    };

    struct FileInfo {
        size_t fileId = 0;
        const char* name = nullptr;
        i64 fileTime = 0; // this is typedef'ed as time64_t in unrar.h
        size_t fileSizeUncompressed = 0;
        bool isDir = false;
        // set when eagerLoad extraction failed for this entry (bad data,
        // OOM, etc.). `data` will be nullptr in that case.
        bool failed = false;

        // internal use
        i64 filePos = 0;
        char* data = nullptr;

        FILETIME GetWinFileTime() const;
    };

    MultiFormatArchive();
    ~MultiFormatArchive();

    Format format = Format::Unknown;

    // hintKind is the result of a prior GuessFileTypeFromContent() done
    // by the caller. When non-null we skip the internal 2 KiB sniff and
    // use it to drive rar-first vs. libarchive routing.
    // eagerLoad = true: decompress every entry at open time and close
    //   the archive so no re-open will ever happen.
    // cbProgress fires after each entry is processed (see
    // ArchiveExtractProgress). Pass a default-constructed Func1 to skip
    // notifications.
    bool Open(const char* path, bool eagerLoad, Kind hintKind, const ArchiveExtractProgressCb& cbProgress);
    bool Open(IStream* stream);

    Vec<FileInfo*> const& GetFileInfos();

    size_t GetFileId(const char* fileName);

    // Return the FileInfo record for a given entry, loading its data into
    // fileInfo->data on demand (on a miss, re-opens the archive unless
    // that was disabled by eager-load mode).
    //
    // Ownership: the returned FileInfo* is owned by this archive. By
    // default fileInfo->data is *not* transferred to the caller — a later
    // call for the same entry returns the same cached buffer, and the
    // archive destructor frees it. If the caller wants the buffer to
    // outlive the archive, they should set fileInfo->data = nullptr after
    // saving the pointer; they then become responsible for free()ing it.
    //
    // Returns nullptr for an unknown name / out-of-range fileId. For an
    // entry whose decompression failed check fileInfo->failed — data will
    // be nullptr in that case.
    FileInfo* GetFileDataByName(const char* filename);
    FileInfo* GetFileDataById(size_t fileId);
    ByteSlice GetFileDataPartById(size_t fileId, size_t sizeHint);

    const char* GetComment();
```

**File:** src/utils/ZipUtil.h (L9-33)
```text
class ZipCreator {
    ISequentialStream* stream;
    StrBuilder centraldir;
    size_t bytesWritten;
    size_t fileCount;

    bool WriteData(const void* data, size_t size);
    bool AddFileData(const char* nameUtf8, const void* data, size_t size, u32 dosdate = 0);

  public:
    explicit ZipCreator(const char* zipFilePath);
    explicit ZipCreator(ISequentialStream* stream);
    ~ZipCreator();

    ZipCreator(ZipCreator const&) = delete;
    ZipCreator& operator=(ZipCreator const&) = delete;

    bool AddFile(const char* filePath, const char* nameInZip = nullptr);
    bool AddFileFromDir(const char* filePath, const char* dir);
    bool AddDir(const char* dirPath, bool recursive = false);
    bool Finish();
};

IStream* OpenDirAsZipStream(const char* dirPath, bool recursive = false);

```

**File:** src/utils/LzmaSimpleArchive.h (L1-30)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

namespace lzma {

struct FileInfo {
    // public data
    u32 compressedSize;
    u32 uncompressedSize;
    u32 uncompressedCrc32;
    FILETIME ftModified;
    const char* name;
    const u8* compressedData;
};

// Note: good enough for our purposes, can be expanded when needed
#define MAX_LZMA_ARCHIVE_FILES 128

struct SimpleArchive {
    int filesCount;
    FileInfo files[MAX_LZMA_ARCHIVE_FILES];
};

bool ParseSimpleArchive(const u8* archiveHeader, size_t dataLen, SimpleArchive* archiveOut);
int GetIdxFromName(SimpleArchive* archive, const char* name);
u8* GetFileDataByIdx(SimpleArchive* archive, int idx, Arena* allocator);
u8* GetFileDataByName(SimpleArchive* archive, const char* fileName, Arena* allocator);
// files is an array of char * entries, last element must be nullptr
bool ExtractFiles(const char* archivePath, const char* dstDir, const char** files, Arena* allocator);

```

**File:** src/utils/HtmlPullParser.h (L68-91)
```text
/* A very simple pull html parser. Call Next() to get the next HtmlToken,
which can be one one of 3 tag types or error. If a tag has attributes,
the caller has to parse them out (using HtmlToken::NextAttr()) */
class HtmlPullParser {
    const char* currPos = nullptr;
    const char* end = nullptr;

    const char* start = nullptr;
    size_t len = 0;

    HtmlToken currToken{};

  public:
    HtmlPullParser(const char* s, size_t len) : currPos(s), end(s + len), start(s), len(len) {}
    HtmlPullParser(const char* s, const char* end) : currPos(s), end(end), start(s), len(end - s) {}
    explicit HtmlPullParser(const ByteSlice& d)
        : currPos((char*)d.data()), end((char*)d.data() + d.size()), start((char*)d.data()), len(d.size()) {}

    void SetCurrPosOff(ptrdiff_t off) { currPos = start + off; }
    size_t Len() const { return len; }
    const char* Start() const { return start; }

    HtmlToken* Next();
};
```

**File:** src/utils/TrivialHtmlParser.h (L38-85)
```text
class HtmlParser {
    Arena* allocator = nullptr;

    // text to parse. It can be changed.
    char* html = nullptr;
    // true if s was allocated by ourselves, false if managed
    // by the caller
    bool freeHtml = false;
    // the codepage used for converting text to Unicode
    uint codepage{CP_ACP};

    size_t elementsCount = 0;
    size_t attributesCount = 0;

    HtmlElement* rootElement = nullptr;
    HtmlElement* currElement = nullptr;

    HtmlElement* AllocElement(HtmlTag tag, char* name, HtmlElement* parent);
    HtmlAttr* AllocAttr(char* name, HtmlAttr* next);

    void CloseTag(HtmlToken* tok);
    void StartTag(HtmlToken* tok);
    void AppendAttr(char* name, char* value);

    HtmlElement* FindParent(HtmlToken* tok);
    HtmlElement* ParseError(HtmlParseError err) {
        error = err;
        return nullptr;
    }

    void Reset();

  public:
    HtmlParseError error{ErrParsingNoError}; // parsing error, a static string
    const char* errorContext = nullptr;      // pointer within html showing which part we failed to parse

    HtmlParser();
    ~HtmlParser();

    HtmlElement* Parse(const ByteSlice& d, uint codepage = CP_ACP);
    HtmlElement* ParseInPlace(const ByteSlice& d, uint codepage = CP_ACP);

    size_t ElementsCount() const;
    size_t TotalAttrCount() const;

    HtmlElement* FindElementByName(const char* name, HtmlElement* from = nullptr);
    HtmlElement* FindElementByNameNS(const char* name, const char* ns, HtmlElement* from = nullptr);
};
```

**File:** src/utils/HtmlParserLookup.h (L86-124)
```text
HtmlTag FindHtmlTag(const char* name, size_t len);
bool IsTagSelfClosing(HtmlTag item);
bool IsInlineTag(HtmlTag item);
AlignAttr FindAlignAttr(const char* name, size_t len);
u32 FindHtmlEntityRune(const char* name, size_t len);

enum CssProp {
    Css_Color,
    Css_Display,
    Css_Font,
    Css_Font_Family,
    Css_Font_Size,
    Css_Font_Style,
    Css_Font_Weight,
    Css_List_Style,
    Css_Margin,
    Css_Margin_Bottom,
    Css_Margin_Left,
    Css_Margin_Right,
    Css_Margin_Top,
    Css_Max_Width,
    Css_Opacity,
    Css_Padding,
    Css_Padding_Bottom,
    Css_Padding_Left,
    Css_Padding_Right,
    Css_Padding_Top,
    Css_Page_Break_After,
    Css_Page_Break_Before,
    Css_Text_Align,
    Css_Text_Decoration,
    Css_Text_Indent,
    Css_Text_Underline,
    Css_White_Space,
    Css_Word_Wrap,
    Css_Unknown
};

CssProp FindCssProp(const char* name, size_t len);
```

**File:** src/utils/CssParser.h (L21-47)
```text
class CssPullParser {
    const char* s = nullptr;
    const char* currPos = nullptr;
    const char* end = nullptr;

    bool inProps = false;
    bool inlineStyle = false;

    const char* currSel = nullptr;
    const char* selEnd = nullptr;

    CssSelector sel{};
    CssProperty prop{};

  public:
    CssPullParser(const char* s, size_t len) {
        this->s = s;
        currPos = s;
        end = s + len;
    }

    // call NextRule first for parsing a style element and
    // NextProperty only for parsing a single style attribute
    bool NextRule();
    const CssSelector* NextSelector();
    const CssProperty* NextProperty();
};
```

**File:** src/utils/JsonParser.h (L24-33)
```text
struct ValueVisitor {
    // return false to stop parsing
    virtual bool Visit(const char* path, const char* value, Type type) = 0;
    virtual ~ValueVisitor() = default;
};

// data must be UTF-8 encoded and nullptr-terminated
// returns false on error
bool Parse(const char* data, ValueVisitor* visitor);

```

**File:** src/utils/SquareTreeParser.h (L1-33)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct SquareTreeNode {
    SquareTreeNode() = default;
    ~SquareTreeNode();

    struct DataItem {
        const char* key = nullptr;
        // only one of str or child are set
        const char* str = nullptr;
        SquareTreeNode* child = nullptr;

        DataItem() = default;
        DataItem(const char* k, const char* string) {
            this->key = k;
            this->str = string;
            this->child = nullptr;
        }
        DataItem(const char* k, SquareTreeNode* node) {
            this->key = k;
            this->str = nullptr;
            this->child = node;
        }
    };
    Vec<DataItem> data;

    const char* GetValue(const char* key, size_t* startIdx = nullptr) const;
    SquareTreeNode* GetChild(const char* key, size_t* startIdx = nullptr) const;
};

SquareTreeNode* ParseSquareTree(const char* s);
const char* SerializeSquareTreeNode(SquareTreeNode*);
```

**File:** src/utils/SettingsUtil.h (L45-64)
```text
struct FieldInfo {
    // offset of the field in the struct
    size_t offset = 0;
    SettingType type = SettingType::Struct;
    // default value for primitive types and pointer to StructInfo for complex ones
    intptr_t value = 0;
};

struct StructInfo {
    u16 structSize = 0;
    u16 fieldCount = 0;
    const FieldInfo* fields = nullptr;
    // one string of fieldCount zero-terminated names of all fields
    // in the order of fields
    const char* fieldNames = nullptr;
};

ByteSlice SerializeStruct(const StructInfo* info, const void* strct, const char* prevData = nullptr);
void* DeserializeStruct(const StructInfo* info, const char* data, void* strct = nullptr);
void FreeStruct(const StructInfo* info, void* strct);
```

**File:** src/utils/ByteReader.h (L1-30)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct ByteReader {
    const u8* d = nullptr;
    size_t len = 0;

    // Unpacks a structure from the data according to the given format
    // e.g. the format "32b2w6d" unpacks 32 Bytes, 2 16-bit Words and 6 32-bit Dwords
    bool Unpack(void* strct, size_t size, const char* format, size_t off, bool isBE) const;

    explicit ByteReader(const ByteSlice&);
    ByteReader(const char* data, size_t len);
    ByteReader(const u8* data, size_t len);

    u8 Byte(size_t off) const;
    u16 WordLE(size_t off) const;
    u16 WordBE(size_t off) const;
    u16 Word(size_t off, bool isBE) const;
    u32 DWordLE(size_t off) const;
    u32 DWordBE(size_t off) const;
    u32 DWord(size_t off, bool isBE) const;
    u64 QWordLE(size_t off) const;
    u64 QWordBE(size_t off) const;
    u64 QWord(size_t off, bool isBE) const;
    const u8* Find(size_t off, u8 byte) const;
    bool UnpackLE(void* strct, size_t size, const char* format, size_t off = 0) const;
    bool UnpackBE(void* strct, size_t size, const char* format, size_t off = 0) const;
    bool Unpack(void* strct, size_t size, const char* format, bool isBE, size_t off = 0) const;
};
```

**File:** src/utils/ByteWriter.h (L1-23)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct ByteWriter {
    bool isLE = false;
    StrBuilder d;

    ByteWriter(size_t sizeHint = 0);
    ByteWriter(const ByteWriter& o);
    ByteWriter& operator=(const ByteWriter&) = delete;

    void Write8(u8 b);
    void Write8x2(u8 b1, u8 b2);
    void Write16(u16 val);
    void Write32(u32 val);
    void Write64(u64 val);

    size_t Size() const;
    ByteSlice AsByteSlice() const;
};

struct ByteWriterLE : ByteWriter {
    ByteWriterLE(size_t sizeHint = 0);
```

**File:** src/utils/ByteOrderDecoder.h (L5-43)
```text
class ByteOrderDecoder {
  public:
    enum ByteOrder {
        LittleEndian,
        BigEndian
    };

    ByteOrderDecoder(const char* d, size_t len, ByteOrder order);
    ByteOrderDecoder(const u8* d, size_t len, ByteOrder order);

    void Bytes(char* dst, size_t len);

    u8 UInt8();
    char Char() { return (char)UInt8(); }

    u16 UInt16();
    i16 Int16() { return (i16)UInt16(); }

    u32 UInt32();
    i32 Int32() { return (i32)UInt32(); }

    u64 UInt64();
    i64 Int64() { return (i64)UInt64(); }

    void Skip(size_t len);
    void Unskip(size_t len);

    size_t Offset() const { return curr - data; }

    bool IsOk() const { return ok; };

  protected:
    bool ok;
    ByteOrder byteOrder;
    const u8* data;

    const u8* curr;
    size_t left;
};
```

**File:** src/utils/BitReader.h (L1-18)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

class BitReader {
    u8 GetByte(size_t pos) const;

  public:
    BitReader(u8* data, size_t len);
    ~BitReader();
    u32 Peek(size_t bitsCount);
    size_t BitsLeft() const;
    bool Eat(size_t bitsCount);

    u8* data = nullptr;
    size_t dataLen = 0;
    size_t currBitPos = 0;
    size_t bitsCount = 0;
};
```

**File:** src/utils/BitManip.h (L1-45)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

/* Simple functions to make it easier to set/clear/test for bits in integers */

namespace bit {

template <typename T>
void Set(T& v, int bitNo) {
    T mask = 1 << bitNo;
    v |= mask;
}

template <typename T>
void Set(T& v, int bit1, int bit2) {
    T mask = 1 << bit1;
    v |= mask;
    mask = 1 << bit2;
    v |= mask;
}

template <typename T>
T FromBit(int bitNo) {
    T v = (T)(1 << bitNo);
    return v;
}

template <typename T>
void Clear(T& v, int bitNo) {
    T mask = 1 << bitNo;
    v &= ~mask;
}

template <typename T>
bool IsSet(T v, int bitNo) {
    T mask = (T)1 << bitNo;
    return (v & mask) != 0;
}

template <typename T>
bool IsMaskSet(T v, T mask) {
    return (v & mask) != 0;
}

} // namespace bit
```

**File:** src/utils/ThreadUtil.h (L1-20)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct Mutex {
    CRITICAL_SECTION cs;

    Mutex() { InitializeCriticalSection(&cs); }
    ~Mutex() { DeleteCriticalSection(&cs); }

    void Lock() { EnterCriticalSection(&cs); }
    void Unlock() { LeaveCriticalSection(&cs); }
};

void SetThreadName(const char* threadName, DWORD threadId = 0);

void RunAsync(const Func0&, const char* threadName = nullptr);
HANDLE StartThread(const Func0&, const char* threadName = nullptr);

extern AtomicInt gDangerousThreadCount;
bool AreDangerousThreadsPending();
```

**File:** src/utils/UITask.h (L1-18)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

namespace uitask {

// Call Initialize() at program startup and Destroy() at the end
void Initialize();
void Destroy();

bool IsMainUIThread();

// call only from the same thread as Initialize() and Destroy()
void DrainQueue();

void Post(const Func0& fn, Kind kind = nullptr);
void PostOptimized(const Func0& fn, Kind kind = nullptr);

} // namespace uitask
```

**File:** src/utils/HttpUtil.h (L1-21)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct HttpRsp {
    AutoFreeStr url;
    StrBuilder data;
    DWORD error = (DWORD)-1;
    DWORD httpStatusCode = (DWORD)-1;

    HttpRsp() = default;
};

struct HttpProgress {
    i64 nDownloaded;
};

bool IsHttpRspOk(const HttpRsp*);

bool HttpPost(const char* server, int port, const char* url, StrBuilder* headers, StrBuilder* data);
bool HttpGet(const char* url, HttpRsp* rspOut);
bool HttpGetToFile(const char* url, const char* destFilePath, const Func1<HttpProgress*>& cbProgress);
```

**File:** src/utils/CryptoUtil.h (L1-12)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

void CalcMD5Digest(const void* data, int dataSize, u8 digest[16]);
void CalcSHA1Digest(const void* data, int dataSize, u8 digest[20]);
void CalcSHA2Digest(const void* data, int dataSize, u8 digest[32]);

bool VerifySHA1Signature(const void* data, size_t dataLen, const char* hexSignature, const void* pubkey,
                         size_t pubkeyLen);

// extracts the content (e.g. PDF) from a PKCS#7 / .p7m wrapper using Win32 crypto APIs
ByteSlice ExtractP7m(ByteSlice d);
```

**File:** src/utils/Log.h (L1-29)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

extern Arena* gLogAllocator;
extern StrBuilder* gLogBuf;
extern bool gLogToConsole;
extern bool gLogToDebugger;
extern bool gReducedLogging;
extern bool gLogToPipe;
extern const char* gLogAppName;
extern char* gLogFilePath;
void StartLogToFile(const char* path, bool removeIfExists);
bool WriteCurrentLogToFile(const char* path);

void log(const char* s);
void logf(const char* fmt, ...);

void logvf(const char* fmt, ...);
void logv(const char* s);

void logPipe(const char* fmt, ...);

void logValueSize(const char* name, i64 v);

// log always
void logfa(const char* fmt, ...);
void loga(const char* s);

void DestroyLogging();
```

**File:** src/utils/DbgHelpDyn.h (L1-19)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD */

namespace dbghelp {

bool Initialize(const WCHAR* symPath, bool force);
bool HasSymbols();
void GetAddressInfo(StrBuilder& s, DWORD64 addr, bool compact);
void WriteMiniDump(const WCHAR* crashDumpFilePath, MINIDUMP_EXCEPTION_INFORMATION* mei, bool fullDump);
void GetThreadCallstack(StrBuilder& s, DWORD threadId);
bool GetCurrentThreadCallstack(StrBuilder& s);
void LogCallstack();
void RememberCallstackLogs();
TempStr GetCurrentThreadCallstackTemp();
void FreeCallstackLogs();
ByteSlice GetCallstacks();
void GetAllThreadsCallstacks(StrBuilder& s);
void GetExceptionInfo(StrBuilder& s, EXCEPTION_POINTERS* excPointers);

```

**File:** src/utils/UtAssert.h (L1-12)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

/* This is assert for unit tests that can be used in non-interactive usage.
Instead of showing a UI to the user, like regular assert(), it simply
remembers number of failed asserts. */

void utassert_func(bool ok, const char* exprStr, const char* file, int lineNo);
int utassert_print_results();

#define utassert(_expr) utassert_func(_expr, #_expr, __FILE__, __LINE__)

```

**File:** src/utils/GuessFileType.h (L5-55)
```text
extern Kind kindFilePDF;
extern Kind kindFilePS;
extern Kind kindFileXps;
extern Kind kindFileDjVu;
extern Kind kindFileChm;

extern Kind kindFileZip;
extern Kind kindFileCbz;
extern Kind kindFileCbr;
extern Kind kindFileRar;
extern Kind kindFile7Z;
extern Kind kindFileCb7;
extern Kind kindFileTar;
extern Kind kindFileCbt;

extern Kind kindFilePng;
extern Kind kindFileJpeg;
extern Kind kindFileGif;
extern Kind kindFileTiff;
extern Kind kindFileBmp;
extern Kind kindFileTga;
extern Kind kindFileJxr;
extern Kind kindFileHdp;
extern Kind kindFileWdp;
extern Kind kindFileWebp;
extern Kind kindFileJp2;

extern Kind kindFileFb2;
extern Kind kindFileFb2z;
extern Kind kindFileEpub;
extern Kind kindFileMobi;
extern Kind kindFilePalmDoc;
extern Kind kindFileHTML;
extern Kind kindFileSvg;
extern Kind kindFileHeic;
extern Kind kindFileAvif;
extern Kind kindFileTxt;

extern Kind kindDirectory;

const char* FindEmbeddedPdfFileStreamNo(const char* path);

Kind GuessFileTypeFromContent(const char* path);
Kind GuessFileTypeFromContent(const ByteSlice& d);
Kind GuessFileTypeFromName(const char*);
Kind GuessFileType(const char* path, bool sniff);
const char* GfxFileExtFromData(const ByteSlice&);
const char* GfxFileExtFromKind(Kind);
const char* GetExtForKind(Kind kind);

int KindIndexOf(Kind* kinds, int nKinds, Kind kind);
```

**File:** src/utils/CmdLineArgsIter.h (L10-26)
```text
struct CmdLineArgsIter {
    StrVec args;
    int curr = 0;
    int nArgs = 0;
    const char* currArg = nullptr;

    explicit CmdLineArgsIter(const WCHAR* cmdLine);
    ~CmdLineArgsIter() = default;

    const char* NextArg();
    const char* EatParam();
    void RewindParam();
    const char* AdditionalParam(int n) const;

    char* at(int) const;
    char* ParamsTemp();
};
```

**File:** src/utils/Dict.h (L21-35)
```text
class MapStrToInt {
  public:
    Arena* allocator = nullptr;
    HashTable* h = nullptr;

    explicit MapStrToInt(size_t initialSize = DEFAULT_HASH_TABLE_INITIAL_SIZE);
    ~MapStrToInt();

    size_t Count() const;

    bool Insert(const char* key, int val, int* existingValOut = nullptr, const char** existingKeyOut = nullptr);

    bool Remove(const char* key, int* removedValOut) const;
    bool Get(const char* key, int* valOut) const;
};
```

**File:** src/utils/Timer.h (L1-19)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// Relatively high-precision timer. Can be used e.g. for measuring execution
// time of a piece of code.

inline LARGE_INTEGER TimeGet() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t;
}

inline double TimeSinceInMs(LARGE_INTEGER start) {
    LARGE_INTEGER t = TimeGet();
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    double timeInSecs = (double)(t.QuadPart - start.QuadPart) / (double)freq.QuadPart;
    return timeInSecs * 1000.0;
}
```

**File:** src/utils/FrameTimeoutCalculator.h (L1-43)
```text
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

class FrameTimeoutCalculator {
    LARGE_INTEGER timeStart;
    LARGE_INTEGER timeLast;
    LONGLONG ticksPerFrame;
    LONGLONG ticsPerMs;
    LARGE_INTEGER timeFreq;

  public:
    explicit FrameTimeoutCalculator(int framesPerSecond) {
        QueryPerformanceFrequency(&timeFreq); // number of ticks per second
        ticsPerMs = timeFreq.QuadPart / 1000;
        ticksPerFrame = timeFreq.QuadPart / framesPerSecond;
        QueryPerformanceCounter(&timeStart);
        timeLast = timeStart;
    }

    // in seconds, as a double
    double ElapsedTotal() const {
        LARGE_INTEGER timeCurr;
        QueryPerformanceCounter(&timeCurr);
        LONGLONG elapsedTicks = timeCurr.QuadPart - timeStart.QuadPart;
        double res = (double)elapsedTicks / (double)timeFreq.QuadPart;
        return res;
    }

    DWORD GetTimeoutInMilliseconds() const {
        LARGE_INTEGER timeCurr;
        LONGLONG elapsedTicks;
        QueryPerformanceCounter(&timeCurr);
        elapsedTicks = timeCurr.QuadPart - timeLast.QuadPart;
        if (elapsedTicks > ticksPerFrame) {
            return 0;
        } else {
            LONGLONG timeoutMs = (ticksPerFrame - elapsedTicks) / ticsPerMs;
            return (DWORD)timeoutMs;
        }
    }

    void Step() { timeLast.QuadPart += ticksPerFrame; }
};
```

**File:** src/utils/WinDynCalls.h (L31-50)
```text
#define API_DECLARATION(name) extern Sig_##name Dyn##name;

#define API_DECLARATION2(name)          \
    typedef decltype(name)* Sig_##name; \
    extern Sig_##name Dyn##name;

// ntdll.dll
#define PROCESS_EXECUTE_FLAGS 0x22
#define MEM_EXECUTE_OPTION_DISABLE 0x1
#define MEM_EXECUTE_OPTION_ENABLE 0x2
#define MEM_EXECUTE_OPTION_PERMANENT 0x8
#define MEM_EXECUTE_OPTION_DISABLE_ATL 0x4

/* enable "NX" execution prevention for XP, 2003
 * cf. http://www.uninformed.org/?v=2&a=4 */
typedef HRESULT(WINAPI* Sig_NtSetInformationProcess)(HANDLE ProcessHandle, UINT ProcessInformationClass,
                                                     PVOID ProcessInformation,
                                                     ULONG ProcessInformationLength); // NOLINT

#define NTDLL_API_LIST(V) V(NtSetInformationProcess)
```

