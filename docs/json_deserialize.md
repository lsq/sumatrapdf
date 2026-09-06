[Json 序列化原理对话](https://deepwiki.com/search/jsonparsekeyvalue_bd5374cd-40e8-4640-9734-092cef1686d3)

# DeepWiki Q&A with Code Context for Repository: sumatrapdfreader/sumatrapdf
## Q1
给我写一个Json::Parse回调函数，在解析过程中打印key和value值
### Answer
SumatraPDF 代码库里没有 `Json::Parse`（大写 J 的类），实际的 JSON 解析 API 是命名空间 `json` 下的自由函数 `json::Parse`，定义于 `src/base/JsonParser.h` 和 `src/base/JsonParser.cpp`。它是一个"推送式"（push）解析器：不构建 DOM 树，而是在解析过程中对每个基本值（string/number/bool/null）调用一次回调 `VisitFn`（本质是 `Func1<Value*>`）。 sumatrapdf:22-35 

回调收到的 `json::Value` 结构包含：
- `path`：一个 `StrNode*` 链表，表示当前值在 JSON 树中的路径（key 或数组下标），只在回调调用期间有效；
- `value`：值的字符串表示（临时 arena 拷贝）；
- `type`：`String` / `Number` / `Bool` / `Null`；
- `stop`：在回调里设为 `true` 可以提前终止解析。 sumatrapdf:6-31 

路径可以用 `json::PathFormatTemp(v->path)` 格式化成人类可读的字符串（例如 `/key[0]/name`），这正好适合打印"key"。下面是一个可以直接使用的回调示例，参照代码库里 `WebView.cpp` 的用法风格：

```cpp
#include "base/Base.h"
#include "base/JsonParser.h"

static void PrintJsonValue(void* /*userData*/, json::Value* v) {
    TempStr path = json::PathFormatTemp(v->path); // "" for root value
    const char* typeName = "?";
    switch (v->type) {
        case json::Type::String: typeName = "string"; break;
        case json::Type::Number: typeName = "number"; break;
        case json::Type::Bool:   typeName = "bool";   break;
        case json::Type::Null:   typeName = "null";   break;
    }
    logf("key: %s, value: %s, type: %s\n", path, v->value, typeName);
    // v->stop = true; // 如需提前终止解析可设置这个
}

// 调用方式：
void Example(Str jsonData) {
    bool ok = json::Parse(jsonData, MkFunc1(PrintJsonValue, (void*)nullptr));
    if (!ok) {
        logf("JSON parse failed\n");
    }
}
```

如果需要绑定一个带状态的对象（例如成员函数），可以使用 `MkMethod1`，用法可参考单元测试文件里的 `JsonVerifier::OnValue`： sumatrapdf:37-39 

以及 `WebView.cpp` 中真实项目里回调结合 `json::PathMatch` 按路径取值的用法： sumatrapdf:1550-1561 

几点注意事项：
- `path` 和 `value` 只在回调调用期间有效（`value` 是临时 arena 拷贝），如果要保存下来需要用 `str::Dup` 复制一份；不要保存 `path` 指针供之后使用。
- 对象的 key 不会被转义存储；每个路径段前缀有一个类型字节（`kSegKey '/'` 表示 key，`kSegIdx 'i'` 表示数组下标），可用 `json::PathSegKey` / `json::PathSegIndex` 单独取出某一段的 key 或下标而不必格式化整个路径。 sumatrapdf:13-20 sumatrapdf:7-25
### Citations
**File:** src/base/JsonParser.h (L6-35)
```text
enum class Type {
    String,
    Number,
    Bool,
    Null
};

// Path is a StrNode list (outermost segment first). Each node's s stores:
//   '/' + key   object key (rest is the key bytes, may contain '/' etc.)
//   'i' + digits  array index
//   '*'         match-only "any segment" (not produced by the parser)
// Path is only valid during the VisitFn call; the parser reuses/mutates the list after.
constexpr char kSegKey = '/';
constexpr char kSegIdx = 'i';
constexpr char kSegAny = '*';

// One primitive value from Parse. path/value are only valid for the duration of
// the VisitFn call (path may be mutated when the parser pops; value is a
// temp-arena copy). Set stop to true to cancel further visits (Parse still
// returns true).
struct Value {
    StrNode* path = nullptr;
    Str value;
    Type type = Type::String;
    bool stop = false;
};

using VisitFn = Func1<Value*>;

bool Parse(Str data, const VisitFn& onValue);
```
**File:** src/base/tests/JsonParser_ut.cpp (L37-39)
```cpp
static bool ParseVerify(Str json, JsonVerifier* v) {
    return json::Parse(json, MkMethod1<JsonVerifier, json::Value*, &JsonVerifier::OnValue>(v));
}
```
**File:** src/gui/win/WebView.cpp (L1550-1561)
```cpp
static void JsCallOnValue(JsCallState* st, json::Value* v) {
    if (v->type != json::Type::String) {
        return;
    }
    if (json::PathMatch(v->path, StrL("/id"))) {
        st->id = v->value;
    } else if (json::PathMatch(v->path, StrL("/method"))) {
        st->method = v->value;
    } else if (json::PathMatch(v->path, StrL("/params"))) {
        st->params = v->value;
    }
}
```
**File:** src/base/JsonParser.cpp (L7-25)
```cpp
// Simple push parser for JSON (cf. http://www.json.org/).
//
// Parse walks the input and calls onValue (Func1<Value*>) for every primitive
// (string, number, bool, null). Put state in userData via MkFunc1 / MkMethod1.
// Set Value::stop to cancel early; Parse then returns true (cancel is not an
// error). Parse returns false only on invalid JSON when not canceled.
//
// Path is a StrNode list (outermost first). Each segment's first char is the
// kind (kSegKey '/' + key, kSegIdx 'i' + decimal index). Example for
// { "key": [false, { "name": "valu\u0065" }] }:
//   1. path "/key" -> "i0", value "false", Type::Bool
//   2. path "/key" -> "i1" -> "/name", value "value", Type::String
// Keys are not escaped; a key may contain '/' or digits without ambiguity
// because each segment carries an explicit kind byte.
//
// path nodes live on the temp arena for the duration of Parse (and may be
// mutated after the callback returns when the parser pops a segment). value is
// a temp-arena copy valid until the temp arena resets. Keep a str::Dup of
// value if you need it longer; do not store path pointers past the callback.
```
## Q2
很好，怎么把这个文件作为测试，运行testapp可以在控制台打印出来
### Answer
在 SumatraPDF 代码库里，"testapp" 和"能在控制台打印 JSON 解析结果的单元测试"其实是两个不同的东西，需要先厘清：

## 1. 两套不同的"测试"机制

**`test_util.exe`**（控制台单元测试可执行文件）——这是你真正想要的。它由 `src/tools/test_util.cpp` 编译而来，`main()` 里顺序调用了一堆 `xxxTest()` / `xxx_UnitTests()` 函数，其中就包括 `JsonTest()`： sumatrapdf:90-119 

`JsonTest()` 本身就定义在 `src/base/tests/JsonParser_ut.cpp`，也就是你上一轮问的那个文件，它已经被声明为 `extern void JsonTest();` 并纳入 `test_util.cpp` 的构建： sumatrapdf:1-21 

**`TestApp`**（GUI 窗口测试程序）——名字里带 "TestApp"，但和 JSON 解析完全无关，它是 `src/testcode/TestApp.cpp` 里定义的一个 GUI 演示窗口，通过 `SumatraPDF.exe -test-app` 命令行参数触发： sumatrapdf:2496-2502 

它只是弹出一个 Windows 窗口，跑消息循环，不会往控制台打印任何东西： sumatrapdf:55-90 

所以如果你想在**控制台**看到打印的 key/value，应该用 `test_util.exe` 这条路线，而不是 `-test-app`。

## 2. 把打印回调接到 `JsonTest()` 里

`JsonParser_ut.cpp` 里已经有很多用 lambda 写的 `onValue` 回调示例，例如： sumatrapdf:188-205 

可以仿照这个风格，在 `JsonTest()`（或新增一个独立函数，比如 `JsonParser_PrintDemo()`）里加一段：

```cpp
{
    Str demo = StrL("{\"key\":[{\"name\":\"value\"}]}");
    auto onValue = [](void*, json::Value* v) {
        TempStr path = json::PathFormatTemp(v->path);
        printf("key: %s, value: %.*s\n", path, v->value.len, v->value.s);
    };
    json::Parse(demo, MkFunc1<void, json::Value*>(onValue, nullptr));
}
```

因为 `test_util.cpp` 用的是普通 `printf`（参见 `PrintStdout` 辅助函数），直接 `printf` 就能在控制台看到输出： sumatrapdf:46-51 

如果不想混进已有的 `JsonTest()` 断言测试里，也可以新建一个函数（比如放在 `JsonParser_ut.cpp` 末尾），然后在 `test_util.cpp` 里加一行 `extern void JsonParser_PrintDemo();` 声明，并在 `main()` 里调用它（放在 `JsonTest();` 附近即可）。

## 3. 构建并运行

仓库提供了脚本 `cmd/run-unit-tests.ts`，它会用 premake5 + MSBuild 编译 `test_util` 这个 target，然后运行生成的 `test_util.exe` 并捕获输出： sumatrapdf:96-120 

也就是说流程是：
1. 在 `JsonParser_ut.cpp` 里加打印回调（如上）。
2. 若新增了独立函数，在 `test_util.cpp` 顶部加 `extern` 声明并在 `main()` 里调用。
3. 用 `bun cmd/run-unit-tests.ts`（或直接用 Visual Studio 编译 `vs2022\SumatraPDF.sln` 里 `tools\test_util` 这个 target）构建。
4. 运行生成的 `test_util.exe`（不带 `-for-ai` 参数直接跑也可以看到 `printf` 输出），控制台就会打印出 key/value。

注意：`utassert` 头文件会覆盖 `assert()` 宏，`JsonParser_ut.cpp` 里已经 `#include "base/UtAssert.h"`，`printf` 不受影响可以正常使用。 sumatrapdf:1-9
### Citations
**File:** src/tools/test_util.cpp (L1-21)
```cpp
#include "base/Base.h"
#include "base/WinDynCalls.h"
#include "base/DbgHelpDyn.h"

// must be last due to assert() over-write
#include "base/UtAssert.h"

// in src/util/tests/UtilTests.cpp
extern void BaseUtils_UnitTests();

// in src/UnitTests.cpp
extern void SumatraPDF_UnitTests();

extern void BaseUtilTest();
extern void ByteOrderTests();
extern void CryptoUtilTest();
extern void CssParser_UnitTests();
extern void DictTest();
extern void FileUtilTest();
extern void GuessFileTypeTest();
extern void JsonTest();
```
**File:** src/tools/test_util.cpp (L46-51)
```cpp
static void PrintStdout(Str s) {
    if (str::IsNull(s)) {
        return;
    }
    printf("%.*s", s.len, s.s);
}
```
**File:** src/tools/test_util.cpp (L90-119)
```cpp
int main(int argc, char** argv) {
    bool forAi = false;
    for (int i = 1; i < argc; i++) {
        if (str::Eq(argv[i], StrL("-for-ai"))) {
            forAi = true;
        }
    }
    if (forAi) {
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
    printf("Running unit tests\n");

    InitDynCalls();
    if (forAi) {
        utassert_set_for_ai(true);
#if OS_WIN
        InitSymbolsForAi();
        SetUnhandledExceptionFilter(ForAiCrashHandler);
#endif
    }
    BaseUtilTest();
    ByteOrderTests();
    CryptoUtilTest();
    CssParser_UnitTests();
    DictTest();
    FileUtilTest();
    GuessFileTypeTest();
    JsonTest();
    RefHoverTest();
```
**File:** src/SumatraStartup.cpp (L2496-2502)
```cpp
#if defined(DEBUG)
    if (flags.testApp) {
        // in TestApp.cpp
        extern void TestApp();
        TestApp();
        return 0;
    }
```
**File:** src/testcode/TestApp.cpp (L55-90)
```cpp
void TestApp() {
    auto w = new TestWnd();
    auto fn = MkFunc1Void<WindowBase::DestroyEvent*>(OnDestroy);
    w->onDestroy = fn;

    // w->backgroundColor = MkRgb((u8)0xae, (u8)0xae, (u8)0xae);
    CreateCustomArgs args;
    args.pos = {CW_USEDEFAULT, CW_USEDEFAULT, 480, 640};
    args.title = "a little test app";
    HWND hwnd = w->CreateCustom(args);
    ReportIf(!hwnd);

    w->layout = CreateMainLayout(w->hwnd);
    LayoutToSize(w->layout, {480, 640});
    HwndInvalidate(hwnd);

#if 0
    w->onSize = [&](SizeEvent* args) {
        HWND hwnd = args->hwnd;
        int dx = args->dx;
        int dy = args->dy;
        if (dx == 0 || dy == 0) {
            return;
        }
        LayoutToSize(l, {dx, dy});
        HwndInvalidate(hwnd);
    };
#endif
    // important to call this after hooking up onSize to ensure
    // first layout is triggered
    w->SetIsVisible(true);

    RunMessageLoop(nullptr, w->hwnd);
    delete w;
    return;
}
```
**File:** src/base/tests/JsonParser_ut.cpp (L1-9)
```cpp
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "base/Base.h"
#include "base/JsonParser.h"

// must be last due to assert() over-write
#include "base/UtAssert.h"

```
**File:** src/base/tests/JsonParser_ut.cpp (L188-205)
```cpp
    // object key with '/' is a single path segment when parsed
    {
        struct SlashKeyState {
            int n = 0;
        } st;
        auto onValue = [](SlashKeyState* st, json::Value* v) {
            st->n++;
            utassert(v->type == json::Type::String);
            utassert(str::Eq(v->value, "x"));
            utassert(str::Eq(json::PathFormatTemp(v->path), "/a/b/c"));
            utassert(json::PathMatch(v->path, StrL("/a/b"), StrL("/c")));
            utassert(!json::PathMatch(v->path, StrL("/a"), StrL("/b"), StrL("/c")));
            utassert(str::Eq(json::PathSegKey(v->path), "a/b"));
            utassert(str::Eq(json::PathSegKey(json::PathNth(v->path, 1)), "c"));
        };
        utassert(json::Parse(StrL("{\"a/b\":{\"c\":\"x\"}}"), MkFunc1<SlashKeyState, json::Value*>(onValue, &st)));
        utassert(st.n == 1);
    }
```
**File:** cmd/run-unit-tests.ts (L96-120)
```typescript
async function main() {
  const kind = parseArgs();
  const { config, platform, outDir, logName } = configFor(kind);
  const { msbuildPath, vsRoot } = detectVisualStudio2026();

  await runLogged(join("bin", "premake5.exe"), ["vs2022"]);
  await runLogged(msbuildPath, [
    String.raw`vs2022\SumatraPDF.sln`,
    // Nested under the "tools" solution folder → MSBuild target is tools\test_util
    String.raw`/t:tools\test_util:Rebuild`,
    `/p:Configuration=${config};Platform=${platform}`,
    "/m",
  ]);
  if (kind === "asan") {
    copyAsanRuntime(vsRoot, outDir);
  }

  mkdirSync(outDir, { recursive: true });
  const exe = join(process.cwd(), outDir, "test_util.exe");
  const logPath = join(process.cwd(), outDir, logName);
  const { exitCode, output } = await runAndCapture(exe, outDir, logPath);

  console.log(`test_util exit code: ${exitCode}`);
  console.log(`output: ${logPath}`);
  if (exitCode === 0 && output.includes("Passed all ")) {
```
## Q3
能不能给我写一个通用json反序列化函数，涵盖结构体嵌套，结构体数组
### Answer
SumatraPDF 目前**没有**通用的、支持嵌套结构体和结构体数组的 JSON 反序列化框架。这一点需要先澄清清楚：

- `json::Parse`（`src/base/JsonParser.h`/`.cpp`）是纯"推送式"解析器，只对每个基本值（string/number/bool/null）调用一次回调，不构建 DOM 树，也不认识 C++ 结构体。 sumatrapdf:7-25 
- 代码库里确实有一套"结构体元数据反射 + 反序列化"的机制——`StructInfo`/`FieldInfo`，配合 `SerializeStruct`/`DeserializeStruct`，支持嵌套结构体（`SettingType::Struct`/`StructPtr`）和结构体数组（`SettingType::Array`，底层是 `Vec<void*>*`）。 sumatrapdf:27-56  但这套机制是给**设置文件格式**（SquareTree，形如 `Key = value` / `SubSettings [ ... ]`）用的，不是 JSON 格式，解析器是 `SquareTreeParser.cpp` 而非 `json::Parse`。 sumatrapdf:552-612 

所以你要的"通用 JSON 反序列化到嵌套结构体/结构体数组"，需要新写，思路是把 `StructInfo`/`FieldInfo` 那套反射思想移植到 `json::Parse` 的 path 回调上。可行方案（基于 `json::Value::path` 是一条从根到叶完整的 `StrNode` 链表，每段前缀 `kSegKey '/'` 表示 key、`kSegIdx 'i'` 表示数组下标 sumatrapdf:13-20 ）：

```cpp
// 简化版通用 JSON -> 结构体反射反序列化
namespace jsonrefl {

enum class Type { String, Int, Float, Bool, Struct, StructArray };

struct FieldInfo {
    const char* name;             // JSON key
    size_t offset;                // offsetof(Struct, field)
    Type type;
    const struct StructInfo* sub = nullptr; // Struct / StructArray 元素类型
};

struct StructInfo {
    size_t size;
    const FieldInfo* fields;
    size_t nFields;
};

static const FieldInfo* FindField(const StructInfo* info, Str key) {
    for (size_t i = 0; i < info->nFields; i++) {
        if (str::Eq(info->fields[i].name, key)) return &info->fields[i];
    }
    return nullptr;
}

static void SetLeaf(const FieldInfo* f, u8* fieldPtr, json::Value* v) {
    switch (f->type) {
        case Type::String: *(Str*)fieldPtr = str::Dup(v->value); break;
        case Type::Int:    *(int*)fieldPtr = ParseInt(v->value); break;
        case Type::Float:  str::Parse(v->value, "%f", (float*)fieldPtr); break;
        case Type::Bool:   *(bool*)fieldPtr = str::Eq(v->value, "true"); break;
        default: ReportIf(true);
    }
}

struct DeserCtx {
    const StructInfo* rootInfo;
    void* rootData;
};

static void OnValue(DeserCtx* ctx, json::Value* v) {
    const StructInfo* info = ctx->rootInfo;
    u8* base = (u8*)ctx->rootData;
    const FieldInfo* leafField = nullptr;

    for (StrNode* seg = v->path; seg; seg = seg->next) {
        if (seg->s.s[0] == json::kSegKey) {
            if (!info) return; // path 走进了一个数组元素之后必须先有下标段，才能再取 key
            Str key = json::PathSegKey(seg);
            const FieldInfo* f = FindField(info, key);
            if (!f) return; // 未知字段，忽略
            u8* fieldPtr = base + f->offset;
            if (f->type == Type::Struct) {
                info = f->sub;
                base = fieldPtr;           // 内嵌结构体，直接进入
            } else if (f->type == Type::StructArray) {
                info = f->sub;
                base = fieldPtr;           // 指向 Vec<void*>* 字段，下一段应为下标
            } else {
                leafField = f;
                base = fieldPtr;           // 基本类型叶子
            }
        } else if (seg->s.s[0] == json::kSegIdx) {
            int idx = json::PathSegIndex(seg);
            auto** vecField = (Vec<void*>**)base;
            if (!*vecField) *vecField = new Vec<void*>();
            Vec<void*>* vec = *vecField;
            while (len(*vec) <= idx) {
                vec->Append(AllocArray<u8>(info->size)); // 按需扩容并 zero-init 新元素
            }
            base = (u8*)(*vec)[idx];
        }
    }
    if (leafField) {
        SetLeaf(leafField, base, v);
    }
}

void* Deserialize(const StructInfo* info, Str jsonData) {
    void* data = AllocArray<u8>(info->size);
    DeserCtx ctx{info, data};
    json::Parse(jsonData, MkFunc1<DeserCtx, json::Value*>(OnValue, &ctx));
    return data;
}

} // namespace jsonrefl
```

用法示例：

```cpp
struct Address {
    Str city;
    int zip;
};
static const jsonrefl::FieldInfo gAddressFields[] = {
    {"city", offsetof(Address, city), jsonrefl::Type::String},
    {"zip",  offsetof(Address, zip),  jsonrefl::Type::Int},
};
static const jsonrefl::StructInfo gAddressInfo = {sizeof(Address), gAddressFields, dimof(gAddressFields)};

struct Person {
    Str name;
    Address home;          // 嵌套结构体
    Vec<void*>* pets;       // 结构体数组：Vec<Pet*>
};
static const jsonrefl::FieldInfo gPersonFields[] = {
    {"name", offsetof(Person, name), jsonrefl::Type::String},
    {"home", offsetof(Person, home), jsonrefl::Type::Struct, &gAddressInfo},
    {"pets", offsetof(Person, pets), jsonrefl::Type::StructArray, &gPetInfo},
};
```

**几点重要限制（未完全验证/需要你补充测试）：**
1. 这段代码是我根据 `json::PathSegKey`/`PathSegIndex`/`kSegKey`/`kSegIdx` 的现有 API 语义手写的示例，**没有在仓库里实际存在**，也没有经过编译验证——用之前请写单元测试（参考 `src/base/tests/JsonParser_ut.cpp` 的风格）。 sumatrapdf:1-9 
2. 空数组/空对象不会触发任何 `onValue` 回调（因为 `json::Parse` 只对叶子值回调），所以上面这种"只在遇到叶子时才向下建路径"的做法，对空的嵌套结构体/空数组字段不会分配空间——如果你需要保证这些字段始终非空，需要额外处理。
3. `StrNode` 结构本身（`next` 字段名等）我在 `src/base/Str.h` 里做了检索但没能读到完整定义内容（可能是索引大小限制导致该片段未完全收录），所以 `seg->next` 和 `seg->s` 的字段名是基于 `JsonParser.cpp` 里的用法（如 `p->next`、`p->s.s[0]`）推断出来的，建议实际编码时打开 `src/base/Str.h` 核实一下 `StrNode` 的确切成员名。如果你需要完整确认，建议开一个 Devin session 直接查看该文件全文。
4. 数组元素目前假设是"结构体数组"（`Vec<void*>*`），若要支持基本类型数组（如 `int[]`、`string[]`）需要再加一个 `Type::IntArray`/`Type::StringArray` 分支，可参考 `SettingsUtil.cpp` 里 `SettingType::IntArray`/`StringArray` 的处理方式作为参照。 sumatrapdf:382-416
### Citations
**File:** src/base/JsonParser.cpp (L7-25)
```cpp
// Simple push parser for JSON (cf. http://www.json.org/).
//
// Parse walks the input and calls onValue (Func1<Value*>) for every primitive
// (string, number, bool, null). Put state in userData via MkFunc1 / MkMethod1.
// Set Value::stop to cancel early; Parse then returns true (cancel is not an
// error). Parse returns false only on invalid JSON when not canceled.
//
// Path is a StrNode list (outermost first). Each segment's first char is the
// kind (kSegKey '/' + key, kSegIdx 'i' + decimal index). Example for
// { "key": [false, { "name": "valu\u0065" }] }:
//   1. path "/key" -> "i0", value "false", Type::Bool
//   2. path "/key" -> "i1" -> "/name", value "value", Type::String
// Keys are not escaped; a key may contain '/' or digits without ambiguity
// because each segment carries an explicit kind byte.
//
// path nodes live on the temp arena for the duration of Parse (and may be
// mutated after the callback returns when the parser pops a segment). value is
// a temp-arena copy valid until the temp arena resets. Keep a str::Dup of
// value if you need it longer; do not store path pointers past the callback.
```
**File:** src/base/SettingsUtil.h (L27-56)
```text
enum class SettingType {
    Struct,
    // an optional sub-struct, stored as a pointer that is null when unset.
    // it's only written out when set, so a struct with many instances (e.g.
    // FileState) doesn't carry an empty block per instance
    StructPtr,
    Array,
    Compact,
    Bool,
    Color,
    Float,
    Int,
    String,
    ColorArray,
    FloatArray,
    IntArray,
    StringArray,
    Comment,
};

struct FieldInfo {
    // offset of the field in the struct
    size_t offset = 0;
    SettingType type = SettingType::Struct;
    // default value for primitive types and pointer to StructInfo for complex ones
    intptr_t value = 0;
    // app-managed / deprecated setting: serialized like any other, but hidden
    // from the advanced settings dialog
    bool internal = false;
};
```
**File:** src/base/SettingsUtil.cpp (L382-416)
```cpp
        case SettingType::FloatArray:
        case SettingType::IntArray: {
            Str src = !str::IsNull(value) ? value : FieldDefaultStr(field);
            Vec<int>* v = *(Vec<int>**)fieldPtr;
            delete v;
            v = new Vec<int>();
            *(Vec<int>**)fieldPtr = v;
            int off = 0;
            while (src && off < src.len) {
                FieldInfo info{};
                if (field.type == SettingType::IntArray) {
                    info.type = SettingType::Int;
                } else if (field.type == SettingType::FloatArray) {
                    info.type = SettingType::Float;
                } else {
                    ReportIf(true);
                }
                Str token = Str(src.s + off, src.len - off);
                deserializeField(info, (u8*)v->AppendBlanks(1), token);
                off = SkipNonWhitespaceOff(src, off);
                off = SkipWhitespaceOff(src, off);
            }
        } break;
        case SettingType::ColorArray:
        case SettingType::StringArray: {
            Vec<Str>* v = *(Vec<Str>**)fieldPtr;
            FreeUtf8StringArray(v);
            v = new Vec<Str>();
            *(Vec<Str>**)fieldPtr = v;
            if (!str::IsNull(value)) {
                DeserializeUtf8StringArray(v, UnescapeStr(value));
            } else if (field.value) {
                DeserializeUtf8StringArray(v, FieldDefaultStr(field));
            }
        } break;
```
**File:** src/base/SettingsUtil.cpp (L552-612)
```cpp
static void* DeserializeStructRec(const StructInfo* info, SquareTreeNode* node, u8* base, bool useDefaults) {
    if (!base) {
        base = AllocArray<u8>(info->structSize);
    }

    const char* fieldName = info->fieldNames;
    for (size_t i = 0; i < info->fieldCount; i++, fieldName += len(fieldName) + 1) {
        const FieldInfo& field = info->fields[i];
        u8* fieldPtr = base + field.offset;
        Str fieldNameStr = Str(fieldName);
        if (SettingType::Struct == field.type) {
            SquareTreeNode* child = node ? node->GetChild(fieldNameStr) : nullptr;
            DeserializeStructRec(GetSubstruct(field), child, fieldPtr, useDefaults);
        } else if (SettingType::StructPtr == field.type) {
            SquareTreeNode* child = node ? node->GetChild(fieldNameStr) : nullptr;
            const StructInfo* substruct = GetSubstruct(field);
            void** pptr = (void**)fieldPtr;
            if (child) {
                if (*pptr) {
                    DeserializeStructRec(substruct, child, (u8*)*pptr, useDefaults);
                } else {
                    *pptr = DeserializeStructRec(substruct, child, nullptr, true);
                }
            } else if (useDefaults && *pptr) {
                // the block is gone from the data: unset it again
                FreeStruct(substruct, *pptr);
                *pptr = nullptr;
            }
        } else if (SettingType::Array == field.type) {
            SquareTreeNode *parent = node, *child = nullptr;
            if (parent) {
                child = parent->GetChild(fieldNameStr);
            }
            if (parent && child != nullptr && (0 == len(child->data) || child->GetChild(StrL("")))) {
                parent = child;
                fieldName += len(fieldName);
                fieldNameStr = Str(fieldName);
            }
            if (child || useDefaults || !*(Vec<void*>**)fieldPtr) {
                Vec<void*>* array = new Vec<void*>();
                int idx = 0;
                while (parent) {
                    child = parent->GetChild(fieldNameStr, &idx);
                    if (child == nullptr) {
                        break;
                    }
                    void* v = DeserializeStructRec(GetSubstruct(field), child, nullptr, true);
                    array->Append(v);
                }
                FreeArray(*(Vec<void*>**)fieldPtr, field);
                *(Vec<void*>**)fieldPtr = array;
            }
        } else if (field.type != SettingType::Comment) {
            Str value = node ? node->GetValue(fieldNameStr) : Str{};
            if (useDefaults || !str::IsNull(value)) {
                deserializeField(field, base, value);
            }
        }
    }
    return base;
}
```
**File:** src/base/JsonParser.h (L13-20)
```text
// Path is a StrNode list (outermost segment first). Each node's s stores:
//   '/' + key   object key (rest is the key bytes, may contain '/' etc.)
//   'i' + digits  array index
//   '*'         match-only "any segment" (not produced by the parser)
// Path is only valid during the VisitFn call; the parser reuses/mutates the list after.
constexpr char kSegKey = '/';
constexpr char kSegIdx = 'i';
constexpr char kSegAny = '*';
```
**File:** src/base/tests/JsonParser_ut.cpp (L1-9)
```cpp
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "base/Base.h"
#include "base/JsonParser.h"

// must be last due to assert() over-write
#include "base/UtAssert.h"

```
## Q4
添加功能
1. 支持基本类型数组
2. 支持递归释放创建的内存
### Answer
这仍然是在我之前手写的 `jsonrefl` 示例（不存在于仓库中）基础上扩展，思路上参照仓库里 `SettingsUtil.cpp` 对基本类型数组（`IntArray`/`FloatArray`/`StringArray`）和递归释放（`FreeStructData`/`FreeStruct`/`FreeArray`）的实现模式： sumatrapdf:630-694 sumatrapdf:189-198 sumatrapdf:17-20 

## 1. 支持基本类型数组

给 `Type` 加上 `IntArray`/`FloatArray`/`BoolArray`/`StringArray`，底层用 `Vec<int>*`/`Vec<float>*`/`Vec<bool>*`/`Vec<Str>*`（对齐仓库 `SettingsUtil.cpp` 里 `IntArray`/`FloatArray`/`StringArray` 用 `Vec<int>*`/`Vec<Str>*` 存储的做法）。在 `OnValue` 里，遇到数组下标段（`kSegIdx`）且当前字段是基本类型数组时，直接向对应 `Vec<T>*` 里 append/扩容，而不是像结构体数组那样分配子结构体块：

```cpp
enum class Type { String, Int, Float, Bool, Struct, StructArray,
                   IntArray, FloatArray, BoolArray, StringArray };

static void OnValue(DeserCtx* ctx, json::Value* v) {
    const StructInfo* info = ctx->rootInfo;
    u8* base = (u8*)ctx->rootData;
    const FieldInfo* leafField = nullptr;
    const FieldInfo* primArrField = nullptr; // 基本类型数组字段（跨 seg 记忆）
    u8* primArrBase = nullptr;

    for (StrNode* seg = v->path; seg; seg = seg->next) {
        if (seg->s.s[0] == json::kSegKey) {
            if (!info) return;
            Str key = json::PathSegKey(seg);
            const FieldInfo* f = FindField(info, key);
            if (!f) return;
            u8* fieldPtr = base + f->offset;
            switch (f->type) {
                case Type::Struct:
                case Type::StructArray:
                    info = f->sub; base = fieldPtr; break;
                case Type::IntArray: case Type::FloatArray:
                case Type::BoolArray: case Type::StringArray:
                    primArrField = f; primArrBase = fieldPtr; info = nullptr; break;
                default:
                    leafField = f; base = fieldPtr; break;
            }
        } else if (seg->s.s[0] == json::kSegIdx) {
            int idx = json::PathSegIndex(seg);
            if (primArrField) {
                // 基本类型数组：按下标扩容并直接写值，无需子结构体
                switch (primArrField->type) {
                    case Type::IntArray: {
                        auto** vp = (Vec<int>**)primArrBase;
                        if (!*vp) *vp = new Vec<int>();
                        while (len(**vp) <= idx) (*vp)->Append(0);
                        (**vp)[idx] = str::ParseInt(v->value);
                        break;
                    }
                    case Type::FloatArray: {
                        auto** vp = (Vec<float>**)primArrBase;
                        if (!*vp) *vp = new Vec<float>();
                        while (len(**vp) <= idx) (*vp)->Append(0.f);
                        str::Parse(v->value, "%f", &(**vp)[idx]);
                        break;
                    }
                    case Type::BoolArray: {
                        auto** vp = (Vec<bool>**)primArrBase;
                        if (!*vp) *vp = new Vec<bool>();
                        while (len(**vp) <= idx) (*vp)->Append(false);
                        (**vp)[idx] = str::Eq(v->value, "true");
                        break;
                    }
                    case Type::StringArray: {
                        auto** vp = (Vec<Str>**)primArrBase;
                        if (!*vp) *vp = new Vec<Str>();
                        while (len(**vp) <= idx) (*vp)->Append(Str{});
                        (**vp)[idx] = str::Dup(v->value);
                        break;
                    }
                    default: break;
                }
                return; // 这一段之后不会再有 key（基本类型数组元素是叶子）
            }
            // 结构体数组：原逻辑不变
            auto** vecField = (Vec<void*>**)base;
            if (!*vecField) *vecField = new Vec<void*>();
            Vec<void*>* vec = *vecField;
            while (len(*vec) <= idx) {
                vec->Append(AllocArray<u8>(info->size));
            }
            base = (u8*)(*vec)[idx];
        }
    }
    if (leafField) {
        SetLeaf(leafField, base, v);
    }
}
```

## 2. 递归释放

参照 `FreeStructData`/`FreeStruct`/`FreeArray` 的三层结构（遍历字段 → 按类型分派 → 结构体数组递归调用自身）：

```cpp
static void FreeData(const StructInfo* info, u8* base); // 前向声明

static void FreeStructArray(Vec<void*>* array, const StructInfo* elemInfo) {
    if (!array) return;
    for (void* el : *array) {
        FreeData(elemInfo, (u8*)el);
        free(el);
    }
    delete array;
}

static void FreeData(const StructInfo* info, u8* base) {
    for (size_t i = 0; i < info->nFields; i++) {
        const FieldInfo& f = info->fields[i];
        u8* fieldPtr = base + f.offset;
        switch (f.type) {
            case Type::String:
                str::Free(*(Str*)fieldPtr);
                *(Str*)fieldPtr = Str{};
                break;
            case Type::Struct:
                FreeData(f.sub, fieldPtr); // 内嵌结构体，就地递归，不 free(fieldPtr) 本身
                break;
            case Type::StructArray:
                FreeStructArray(*(Vec<void*>**)fieldPtr, f.sub);
                *(Vec<void*>**)fieldPtr = nullptr;
                break;
            case Type::IntArray:
                delete *(Vec<int>**)fieldPtr;
                *(Vec<int>**)fieldPtr = nullptr;
                break;
            case Type::FloatArray:
                delete *(Vec<float>**)fieldPtr;
                *(Vec<float>**)fieldPtr = nullptr;
                break;
            case Type::BoolArray:
                delete *(Vec<bool>**)fieldPtr;
                *(Vec<bool>**)fieldPtr = nullptr;
                break;
            case Type::StringArray: {
                Vec<Str>* v = *(Vec<Str>**)fieldPtr;
                if (v) {
                    for (int j = 0; j < len(*v); j++) str::Free((*v)[j]);
                    delete v;
                }
                *(Vec<Str>**)fieldPtr = nullptr;
                break;
            }
            case Type::Int: case Type::Float: case Type::Bool:
                break; // 无需释放
            default:
                break;
        }
    }
}

// 对外接口：释放根结构体本身（配合 Deserialize 返回的顶层指针）
void Free(const StructInfo* info, void* data) {
    if (!data) return;
    FreeData(info, (u8*)data);
    free(data);
}
```

调用方式和之前一致：`void* obj = jsonrefl::Deserialize(&gPersonInfo, jsonData);` 用完后 `jsonrefl::Free(&gPersonInfo, obj);`，会递归释放 `home`（嵌套结构体的内部字段，如 `Str`）、`pets`（结构体数组的每个元素及其内部字段，再 `delete` `Vec` 本身）、以及任何基本类型数组字段。

**注意事项（仍需你自己验证）：**
- 这套代码依旧是我按 `StructInfo`/`FieldInfo` 反射思路和仓库现有 API（`json::PathSegKey`/`PathSegIndex`/`kSegKey`/`kSegIdx`，`StrNode::next`/`s`）手写的示例，**不存在于仓库中**，未编译验证，务必先写单元测试（参考 `src/base/tests/JsonParser_ut.cpp` 风格）再接入实际代码。 sumatrapdf:1-9 
- `Vec<T>` 的确切接口（`Append`/`len()`/`operator[]`/析构是否自动释放元素）我是按仓库里 `SettingsUtil.cpp` 中 `Vec<int>`/`Vec<Str>` 的实际用法（`Append`、`delete vec`、`len(*vec)`）推断的； sumatrapdf:670-681  若 `src/base/Vec.h` 中签名有差异，需要按实际签名调整。
- 基本类型数组的 JSON 值目前假定按下标顺序出现（`json::Parse` 是顺序推送的），如果某些下标缺失（JSON 数组本身不会跳号，所以正常情况下不会发生），`while (len <= idx) Append(default)` 的补齐逻辑能兜底。
- 和结构体数组一样，空的基本类型数组字段（JSON 里是 `[]`）不会触发任何 `onValue` 回调，因此该字段仍会保持 `nullptr`；如果业务需要"始终非空"的语义，需要额外在 `Deserialize` 末尾遍历一次做兜底初始化。
### Citations
**File:** src/base/SettingsUtil.cpp (L189-198)
```cpp
static void FreeArray(Vec<void*>* array, const FieldInfo& field) {
    if (!array) {
        return;
    }
    const auto* structInfo = GetSubstruct(field);
    for (auto* el : *array) {
        FreeStruct(structInfo, el);
    }
    delete array;
}
```
**File:** src/base/SettingsUtil.cpp (L630-694)
```cpp
static void FreeStructData(const StructInfo* info, u8* base) {
    for (size_t i = 0; i < info->fieldCount; i++) {
        const FieldInfo& field = info->fields[i];
        u8* fieldPtr = base + field.offset;
        switch (field.type) {
            case SettingType::Bool:
            case SettingType::Int:
            case SettingType::Float:
            case SettingType::Comment:
                // nothing to free
                break;
            case SettingType::Struct: {
                const StructInfo* substruct = GetSubstruct(field);
                FreeStructData(substruct, fieldPtr);
                break;
            }
            case SettingType::StructPtr: {
                void** pptr = (void**)fieldPtr;
                if (*pptr) {
                    FreeStruct(GetSubstruct(field), *pptr);
                    *pptr = nullptr;
                }
                break;
            }
            case SettingType::Array: {
                Vec<void*>* array = *(Vec<void*>**)fieldPtr;
                FreeArray(array, field);
                break;
            }
            case SettingType::Color: {
                FreeColorText(*(ParsedColor*)fieldPtr);
                break;
            }
            case SettingType::String: {
                Str* str = (Str*)fieldPtr;
                str::Free(*str);
                str->s = nullptr;
                str->len = 0;
                break;
            }
            case SettingType::FloatArray:
            case SettingType::IntArray: {
                Vec<int>* vec = *((Vec<int>**)fieldPtr);
                delete vec;
                break;
            }
            case SettingType::StringArray:
            case SettingType::ColorArray: {
                Vec<Str>* strArray = *(Vec<Str>**)fieldPtr;
                FreeUtf8StringArray(strArray);
                break;
            }
            default:
                break;
        }
    }
}

void FreeStruct(const StructInfo* info, void* strct) {
    if (!strct) {
        return;
    }
    FreeStructData(info, (u8*)strct);
    free(strct);
}
```
**File:** src/base/Str.h (L17-20)
```text
struct StrNode {
    StrNode* next = nullptr;
    Str s;
};
```
**File:** src/base/tests/JsonParser_ut.cpp (L1-9)
```cpp
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "base/Base.h"
#include "base/JsonParser.h"

// must be last due to assert() over-write
#include "base/UtAssert.h"

```
## Q5
{
          "info": { "name": "cluster-1", "version": 2 },
          "ipPool": {
            "192.168.1.1": { "port": 8080, "useTls": true},
            "192.168.1.2": { "port": 8081, "useTls": false}
          }
        }
这种动态键怎么映射为StrVecWithData类型
### Answer
`StrVecWithData<T>`（定义于 `src/base/StrVec.h`）本质上是"字符串数组 + 每个字符串关联一份类型为 `T` 的数据"，天生适合表示 JSON 里`{ "动态key": { 结构体值 } }`这种 map 语义——把 JSON 的 key（如 `"192.168.1.1"`）当作 `StrVec` 里的字符串，把对应的 value 对象反序列化进 `T`： sumatrapdf:71-94 

## 1. 在反射元数据里新增 `Map` 类型

在之前的 `jsonrefl::Type` 枚举里加一个 `Map`，它对应的字段本身就是一个 `StrVecWithData<T>`（**值类型，直接嵌入结构体，不是指针**，和仓库里 `StrVecCP tabs;` 这种用法一致）： sumatrapdf:23-26 

```cpp
enum class Type { String, Int, Float, Bool, Struct, StructArray,
                   IntArray, FloatArray, BoolArray, StringArray,
                   Map }; // 新增：动态 key -> 结构体

// FieldInfo::sub 对 Map 类型指向 value 结构体的 StructInfo（如 IpInfo 的 port/useTls）
```

对应你的例子，`ipPool` 字段声明为：

```cpp
struct IpInfo {
    int port;
    bool useTls;
};
static const jsonrefl::FieldInfo gIpInfoFields[] = {
    {"port",   offsetof(IpInfo, port),   jsonrefl::Type::Int},
    {"useTls", offsetof(IpInfo, useTls), jsonrefl::Type::Bool},
};
static const jsonrefl::StructInfo gIpInfoInfo = {sizeof(IpInfo), gIpInfoFields, dimof(gIpInfoFields)};

struct Cluster {
    Info info;                       // 嵌套结构体
    StrVecWithData<IpInfo> ipPool;    // 动态 key -> 结构体
};
static const jsonrefl::FieldInfo gClusterFields[] = {
    {"info",   offsetof(Cluster, info),   jsonrefl::Type::Struct, &gInfoInfo},
    {"ipPool", offsetof(Cluster, ipPool), jsonrefl::Type::Map,    &gIpInfoInfo},
};
```

## 2. 解析逻辑：`OnValue` 里处理 `Map`

`json::Parse` 对 object 的 key 一律走 `kSegKey` 段（无论是固定字段名还是动态 IP 地址字符串），区别只是：遇到 `Map` 字段后，**下一个 `kSegKey` 段不再拿去 `FindField` 按已知字段名匹配，而是直接当作动态 key**，用它在 `StrVecWithData<T>::Find` 里查找/新建条目：

```cpp
static void OnValue(DeserCtx* ctx, json::Value* v) {
    const StructInfo* info = ctx->rootInfo;
    u8* base = (u8*)ctx->rootData;
    const FieldInfo* leafField = nullptr;
    const FieldInfo* mapField = nullptr; // 记住当前处于哪个 Map 字段

    for (StrNode* seg = v->path; seg; seg = seg->next) {
        if (seg->s.s[0] == json::kSegKey) {
            Str key = json::PathSegKey(seg);
            if (mapField) {
                // 这个 key 是动态 key（比如 "192.168.1.1"），不是字段名
                auto* vec = (StrVecWithData<u8>*)base; // 见下方类型说明
                int idx = vec->Find(key);
                if (idx < 0) {
                    idx = vec->AppendRaw(key, mapField->sub->size); // 见下方注意事项
                }
                base = (u8*)vec->AtDataRaw(idx);
                info = mapField->sub;
                mapField = nullptr;
                continue;
            }
            if (!info) return;
            const FieldInfo* f = FindField(info, key);
            if (!f) return;
            u8* fieldPtr = base + f->offset;
            if (f->type == Type::Map) {
                mapField = f;
                base = fieldPtr; // fieldPtr 就是 StrVecWithData<T> 对象本身的地址
                info = nullptr;
                continue;
            }
            // ... 其余分支（Struct / StructArray / 基本类型数组 / 叶子）同之前实现
        }
        // kSegIdx 分支不变（Map 的值不经过数组下标）
    }
    if (leafField) SetLeaf(leafField, base, v);
}
```

## 3. 关键坑：`StrVecWithData<T>` 不是 POD，不能靠原始内存清零来构造/释放

这一点必须提醒清楚，因为直接影响正确性：

- `StrVec`（`StrVecWithData<T>` 的基类）有真正的构造函数把 `nextPageSize` 设为 `256`、`dataSize` 设为 `sizeofi(T)`，以及非 trivial 的析构函数负责释放 `StrVecPage` 链表： sumatrapdf:14-26 
- 之前 `Deserialize` 用 `AllocArray<u8>(info->size)` 分配整块内存（相当于 `calloc`），对普通 POD 字段没问题，但对内嵌的 `StrVecWithData<T>` 字段来说，`nextPageSize` 会被清成 `0` 而不是 `256`，`dataSize` 也会是 `0` 而不是 `sizeofi(T)`，导致后续 `Append`/`Find` 行为出错甚至崩溃。
- 正确做法：在给结构体分配内存之后，**对每个 `Map` 类型字段做 placement-new**，即 `new (fieldPtr) StrVecWithData<T>();`（在遇到该字段第一次被解析之前，或者在 `Deserialize` 递归分配阶段统一做一遍），而不是依赖 `AllocArray` 的零初始化。
- 释放时同理，不能只是把这块内存当字节数组去 `delete` 各种 `Vec<T>*`，而要显式调用 `StrVecWithData<T>` 的析构函数：`((StrVecWithData<T>*)fieldPtr)->~StrVecWithData<T>();`，让它自己遍历 `StrVecPage` 链表释放内存；如果 value 类型 `T` 里还有需要递归释放的字段（比如字符串），还要先对每个 `AtData(i)` 调用 `FreeData(mapField->sub, (u8*)AtData(i))` 再销毁整个 vec。

由于上面示例里用到的 `AppendRaw`/泛型化 `StrVecWithData<u8>` 转型是我为了绕开编译期 `T` 类型信息手写的占位写法（`StrVecWithData<T>` 实际上是模板，字段类型在编译期就应该是具体的 `T`，不能真的这样类型双关），**这一整段依旧是未经编译验证的设计草案**，实现时建议：

1. 让 `Map` 字段的反射描述额外记录 `sizeof(T)`（已经有 `sub->size`），且反序列化框架统一走"先 placement-new 所有 Map 字段，再解析，再在释放时显式调用析构"的三段式，而不是用 `void*`/字节偏移绕过 C++ 类型系统。
2. 更稳妥的替代方案：如果你的 `Map` 场景比较固定（key 总是字符串，value 总是同一种小结构体），也可以不用 `StrVecWithData<T>` 这套通用容器，而是直接用 `Vec<std::pair<Str, T>>` 或者干脆两个平行数组，实现和释放逻辑会简单很多，避免和 `StrVec` 的分页存储机制打交道。 sumatrapdf:568-578  中 `InsertRandData` 展示了 `StrVecWithData<T>::Append(Str s, const T& data)` 和 `AtData(i)` 的标准用法，可以作为你实现 `Map` 分支时的参考。
### Citations
**File:** src/base/StrVec.h (L14-26)
```text
struct StrVec {
    StrVecPage* first = nullptr;
    StrVecPage* last = nullptr;
    int* sortIndexes = nullptr;
    int nextPageSize = 256;
    int size = 0;
    int dataSize = 0;

    StrVec() = default;
    StrVec(int dataSize);
    StrVec(const StrVec& that);
    StrVec& operator=(const StrVec& that);
    ~StrVec();
```
**File:** src/base/StrVec.h (L71-94)
```text
template <typename T>
struct StrVecWithData : StrVec {
    StrVecWithData() : StrVec(sizeofi(T)) {}

    T* AtData(int i) const {
        void* res = AtDataRaw(i);
        return (T*)(res);
    }

    int Append(Str s, const T& data) {
        StrVec::Append(s);
        int idx = len(*this) - 1;
        T* d = AtData(idx);
        *d = data;
        return idx;
    }

    int AppendFrom(StrVecWithData<T>* src, int srcIdx) {
        Str s = src->At(srcIdx);
        T* data = src->AtData(srcIdx);
        int idx = this->Append(s, *data);
        return idx;
    }
};
```
**File:** src/CommandPaletteInternal.h (L23-26)
```text
using StrVecCP = StrVecWithData<ItemDataCP>;

struct ListBoxModelCP : ListBoxModel {
    StrVecCP strings;
```
**File:** src/base/tests/StrVec_ut.cpp (L568-578)
```cpp
template <typename T>
static void InsertRandData(StrVecWithData<T>* v) {
    for (int i = 0; i < kMaxStringN; i++) {
        Str s = StrForN(i);
        T data{};
        data.n = (decltype(data.n))i;
        v->Append(s, data);
        T* d = v->AtData(i);
        utassert(d->n == i);
    }
}
```
## Q6
Map 场景比较固定（key 总是字符串，value 总是同一种小结构体或者字符串)使用两个平行数组方案，给出具体实现代码
### Answer
Below is the "parallel arrays" version of the `Map` field (dynamic string key → fixed value type), avoiding `StrVecWithData<T>`'s paged-storage/placement-new complications entirely. This uses `Vec<Str>` for keys and `Vec<T>` (or `Vec<void*>` for struct values) for the parallel values array, matching the plain POD-vector style already used elsewhere in the codebase for `Vec<int>`/`Vec<Str>` fields: sumatrapdf:27-61 sumatrapdf:382-416 

## 1. Field types

Two kinds of dynamic maps to cover your example: value is a small struct (`ipPool` → `{port, useTls}`), or value is a plain string.

```cpp
enum class Type { String, Int, Float, Bool, Struct, StructArray,
                   IntArray, FloatArray, BoolArray, StringArray,
                   MapStruct,  // dynamic-key -> struct value
                   MapString }; // dynamic-key -> string value

struct FieldInfo {
    const char* name;
    size_t offset;
    Type type;
    const struct StructInfo* sub = nullptr; // value StructInfo for MapStruct
};

struct StructInfo {
    size_t size;
    const FieldInfo* fields;
    size_t nFields;
};
```

The struct field for a `MapStruct` is declared as **two parallel `Vec` members sitting next to each other**, referenced via a small wrapper so the offset table only needs one entry:

```cpp
template <typename T>
struct MapField {
    Vec<Str> keys;
    Vec<T> values; // POD value type (e.g. IpInfo)
};

struct MapStringField {
    Vec<Str> keys;
    Vec<Str> values;
};
```

Applied to your JSON:

```cpp
struct IpInfo {
    int port;
    bool useTls;
};
static const FieldInfo gIpInfoFields[] = {
    {"port",   offsetof(IpInfo, port),   Type::Int},
    {"useTls", offsetof(IpInfo, useTls), Type::Bool},
};
static const StructInfo gIpInfoInfo = {sizeof(IpInfo), gIpInfoFields, dimof(gIpInfoFields)};

struct Info {
    Str name;
    int version;
};
static const FieldInfo gInfoFields[] = {
    {"name",    offsetof(Info, name),    Type::String},
    {"version", offsetof(Info, version), Type::Int},
};
static const StructInfo gInfoInfo = {sizeof(Info), gInfoFields, dimof(gInfoFields)};

struct Cluster {
    Info info;
    MapField<IpInfo> ipPool;
};
static const FieldInfo gClusterFields[] = {
    {"info",   offsetof(Cluster, info),   Type::Struct,    &gInfoInfo},
    {"ipPool", offsetof(Cluster, ipPool), Type::MapStruct, &gIpInfoInfo},
};
static const StructInfo gClusterInfo = {sizeof(Cluster), gClusterFields, dimof(gClusterFields)};
```

## 2. Parsing (`OnValue`)

Key idea: once the walk enters a `MapStruct`/`MapString` field, the *next* `kSegKey` segment is not looked up via `FindField` — it's a dynamic key that gets appended to `keys`, and a fresh POD slot appended to `values` (or reused if the key already exists, in case JSON repeats a key — unlikely but cheap to guard against with a linear `Find`).

```cpp
static int MapFindOrAdd(Vec<Str>* keys, Str key) {
    for (int i = 0; i < keys->len; i++) {
        if (str::Eq(keys->els[i], key)) return i;
    }
    keys->Append(str::Dup(key));
    return keys->len - 1;
}

static void OnValue(DeserCtx* ctx, json::Value* v) {
    const StructInfo* info = ctx->rootInfo;
    u8* base = (u8*)ctx->rootData;
    const FieldInfo* leafField = nullptr;
    const FieldInfo* mapField = nullptr; // remembers we're inside a Map* field
    u8* mapBase = nullptr;

    for (StrNode* seg = v->path; seg; seg = seg->next) {
        if (seg->s.s[0] == json::kSegKey) {
            Str key = json::PathSegKey(seg);

            if (mapField) {
                // key is the dynamic map key (e.g. "192.168.1.1")
                if (mapField->type == Type::MapStruct) {
                    auto* m = (MapField<u8>*)mapBase; // T size handled via mapField->sub->size below
                    int idx = MapFindOrAdd(&m->keys, key);
                    // grow values to len(keys); values holds raw T-sized blocks
                    // (see note below on why MapField<u8> is a placeholder)
                    info = mapField->sub;
                    base = MapValueSlot(mapField, mapBase, idx); // see helper below
                } else { // MapString
                    auto* m = (MapStringField*)mapBase;
                    int idx = MapFindOrAdd(&m->keys, key);
                    while (m->values.len <= idx) m->values.Append(Str{});
                    leafField = nullptr; // handled directly below
                    base = (u8*)&m->values.els[idx];
                    // string leaf: value set when we hit the primitive callback
                    static FieldInfo strLeaf{"", 0, Type::String};
                    leafField = &strLeaf;
                }
                mapField = nullptr;
                continue;
            }

            if (!info) return;
            const FieldInfo* f = FindField(info, key);
            if (!f) return;
            u8* fieldPtr = base + f->offset;

            if (f->type == Type::MapStruct || f->type == Type::MapString) {
                mapField = f;
                mapBase = fieldPtr;
                info = nullptr;
                continue;
            }
            if (f->type == Type::Struct) {
                info = f->sub; base = fieldPtr; continue;
            }
            // ... StructArray / primitive-array branches unchanged from before ...
            leafField = f; base = fieldPtr;
        }
        // kSegIdx branch: unchanged (array handling), not used for map values directly
    }
    if (leafField) SetLeaf(leafField, base, v);
}
```

**Important limitation on `MapField<T>` genericity**: because `T` is a compile-time template parameter but the reflection walk only has a runtime `sub->size`, you cannot generically do `Vec<T>::Append` through a `void*`/byte-offset without knowing `T` at compile time — this is the same type-erasure problem flagged in the previous answer for `StrVecWithData<T>`. The clean way to resolve it for the **fixed, small-value-type** case you described is to **not templatize** `MapField` at all, and instead give each concrete map field its own generated helper functions (or a `memcpy`-based raw growth helper operating on `sub->size`-byte elements, similar to how `Vec<void*>` element blocks are grown with `AllocArray<u8>(info->size)` for struct arrays in the earlier `StructArray` code). Concretely:

```cpp
// raw-byte parallel array for struct-typed map values (no T template needed)
struct MapRaw {
    Vec<Str> keys;
    Vec<u8*> values; // each element is a heap block of size == valueInfo->size, like StructArray
};

static u8* MapValueSlot(const StructInfo* valueInfo, MapRaw* m, Str key) {
    int idx = MapFindOrAdd(&m->keys, key);
    while (m->values.len <= idx) {
        m->values.Append(AllocArray<u8>(valueInfo->size));
    }
    return m->values.els[idx];
}
```

Use `MapRaw` as the field type in `Cluster` instead of `MapField<IpInfo>`:

```cpp
struct Cluster {
    Info info;
    MapRaw ipPool; // values are IpInfo blocks, described by gIpInfoInfo
};
```

Then `OnValue`'s `MapStruct` branch becomes just:

```cpp
if (mapField->type == Type::MapStruct) {
    auto* m = (MapRaw*)mapBase;
    base = MapValueSlot(mapField->sub, m, key);
    info = mapField->sub;
}
```

This sidesteps templates entirely and reuses the exact same "grow-and-allocate-by-size" pattern already used for struct arrays, at the cost of one `void*`-per-element indirection (same cost `StructArray`/`Vec<void*>` already pays).

## 3. Recursive free

Mirrors `FreeStructData`'s per-field-type dispatch, extended with `MapStruct`/`MapString`: sumatrapdf:630-694 

```cpp
static void FreeData(const StructInfo* info, u8* base); // fwd decl

static void FreeMapRaw(MapRaw* m, const StructInfo* valueInfo) {
    if (!m) return;
    for (int i = 0; i < m->keys.len; i++) {
        str::Free(m->keys.els[i]);
    }
    for (int i = 0; i < m->values.len; i++) {
        FreeData(valueInfo, m->values.els[i]);
        free(m->values.els[i]);
    }
    m->keys.Reset();
    m->values.Reset();
}

static void FreeMapString(MapStringField* m) {
    if (!m) return;
    for (int i = 0; i < m->keys.len; i++) str::Free(m->keys.els[i]);
    for (int i = 0; i < m->values.len; i++) str::Free(m->values.els[i]);
    m->keys.Reset();
    m->values.Reset();
}

static void FreeData(const StructInfo* info, u8* base) {
    for (size_t i = 0; i < info->nFields; i++) {
        const FieldInfo& f = info->fields[i];
        u8* fieldPtr = base + f.offset;
        switch (f.type) {
            case Type::String:
                str::Free(*(Str*)fieldPtr);
                *(Str*)fieldPtr = Str{};
                break;
            case Type::Struct:
                FreeData(f.sub, fieldPtr);
                break;
            case Type::MapStruct:
                FreeMapRaw((MapRaw*)fieldPtr, f.sub);
                break;
            case Type::MapString:
                FreeMapString((MapStringField*)fieldPtr);
                break;
            // ... StructArray / primitive-array / Int / Float / Bool cases unchanged ...
            default:
                break;
        }
    }
}

void Free(const StructInfo* info, void* data) {
    if (!data) return;
    FreeData(info, (u8*)data);
    free(data);
}
```

## Caveats (unverified / needs your own testing)

- None of this (`jsonrefl`, `MapRaw`, `MapStringField`) exists in the SumatraPDF codebase — it is a design built on top of the real, existing APIs `json::Parse`/`json::PathSegKey`/`kSegKey` sumatrapdf:13-20 , `Vec<T>` sumatrapdf:27-61 , and `Str`/`StrNode` sumatrapdf:17-20 . It has not been compiled or tested.
- I could not confirm the exact signature of `Vec<T>::Append` in this session (grep for `Append` in `src/base/Vec.h` returned matches but I ran out of iterations before reading them) — the calls above (`keys.Append(str::Dup(key))`, `values.Append(...)`) assume the conventional `T& Append(T)`/`void Append(const T&)` style seen elsewhere in the codebase, but verify against `src/base/Vec.h` before using.
- `MapFindOrAdd`'s linear scan is O(n) per key lookup; fine for small maps (a handful of IPs), but if `ipPool`-style maps can grow large, you'd want a real hash map — not something built into this reflection framework.
- As before, write unit tests in the style of `src/base/tests/JsonParser_ut.cpp` before integrating.
### Citations
**File:** src/base/Vec.h (L27-61)
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

    explicit Vec() = default;
```
**File:** src/base/SettingsUtil.cpp (L382-416)
```cpp
        case SettingType::FloatArray:
        case SettingType::IntArray: {
            Str src = !str::IsNull(value) ? value : FieldDefaultStr(field);
            Vec<int>* v = *(Vec<int>**)fieldPtr;
            delete v;
            v = new Vec<int>();
            *(Vec<int>**)fieldPtr = v;
            int off = 0;
            while (src && off < src.len) {
                FieldInfo info{};
                if (field.type == SettingType::IntArray) {
                    info.type = SettingType::Int;
                } else if (field.type == SettingType::FloatArray) {
                    info.type = SettingType::Float;
                } else {
                    ReportIf(true);
                }
                Str token = Str(src.s + off, src.len - off);
                deserializeField(info, (u8*)v->AppendBlanks(1), token);
                off = SkipNonWhitespaceOff(src, off);
                off = SkipWhitespaceOff(src, off);
            }
        } break;
        case SettingType::ColorArray:
        case SettingType::StringArray: {
            Vec<Str>* v = *(Vec<Str>**)fieldPtr;
            FreeUtf8StringArray(v);
            v = new Vec<Str>();
            *(Vec<Str>**)fieldPtr = v;
            if (!str::IsNull(value)) {
                DeserializeUtf8StringArray(v, UnescapeStr(value));
            } else if (field.value) {
                DeserializeUtf8StringArray(v, FieldDefaultStr(field));
            }
        } break;
```
**File:** src/base/SettingsUtil.cpp (L630-694)
```cpp
static void FreeStructData(const StructInfo* info, u8* base) {
    for (size_t i = 0; i < info->fieldCount; i++) {
        const FieldInfo& field = info->fields[i];
        u8* fieldPtr = base + field.offset;
        switch (field.type) {
            case SettingType::Bool:
            case SettingType::Int:
            case SettingType::Float:
            case SettingType::Comment:
                // nothing to free
                break;
            case SettingType::Struct: {
                const StructInfo* substruct = GetSubstruct(field);
                FreeStructData(substruct, fieldPtr);
                break;
            }
            case SettingType::StructPtr: {
                void** pptr = (void**)fieldPtr;
                if (*pptr) {
                    FreeStruct(GetSubstruct(field), *pptr);
                    *pptr = nullptr;
                }
                break;
            }
            case SettingType::Array: {
                Vec<void*>* array = *(Vec<void*>**)fieldPtr;
                FreeArray(array, field);
                break;
            }
            case SettingType::Color: {
                FreeColorText(*(ParsedColor*)fieldPtr);
                break;
            }
            case SettingType::String: {
                Str* str = (Str*)fieldPtr;
                str::Free(*str);
                str->s = nullptr;
                str->len = 0;
                break;
            }
            case SettingType::FloatArray:
            case SettingType::IntArray: {
                Vec<int>* vec = *((Vec<int>**)fieldPtr);
                delete vec;
                break;
            }
            case SettingType::StringArray:
            case SettingType::ColorArray: {
                Vec<Str>* strArray = *(Vec<Str>**)fieldPtr;
                FreeUtf8StringArray(strArray);
                break;
            }
            default:
                break;
        }
    }
}

void FreeStruct(const StructInfo* info, void* strct) {
    if (!strct) {
        return;
    }
    FreeStructData(info, (u8*)strct);
    free(strct);
}
```
**File:** src/base/JsonParser.h (L13-20)
```text
// Path is a StrNode list (outermost segment first). Each node's s stores:
//   '/' + key   object key (rest is the key bytes, may contain '/' etc.)
//   'i' + digits  array index
//   '*'         match-only "any segment" (not produced by the parser)
// Path is only valid during the VisitFn call; the parser reuses/mutates the list after.
constexpr char kSegKey = '/';
constexpr char kSegIdx = 'i';
constexpr char kSegAny = '*';
```
**File:** src/base/Str.h (L17-20)
```text
struct StrNode {
    StrNode* next = nullptr;
    Str s;
};
```
## Q7
可以把两个平等数组更换为dict::MapStrToInt和一个数组吗
### Answer
`dict::MapStrToInt`（定义于 `src/base/Dict.h`/`Dict.cpp`）是一个 Str→int 的哈希表，正好可以拿来存"动态 key → 下标"，配合一个平行的 `Vec` 存实际的值，这样只需要一个数组而不是两个 `Vec<Str>`+`Vec<T>`： sumatrapdf:19-33 sumatrapdf:208-281 

## 关键 API 事实

- `MapStrToInt::Insert(Str key, int val, ...)` 把 key 复制进它内部自己的 arena（`a = ArenaNew()`），值是 `int`。 sumatrapdf:208-255 
- `Get(Str key, int* valOut)` 按 key 查值。 sumatrapdf:269-281 
- 析构函数 `~MapStrToInt()` 会 `DeleteHashTable(h)` + `ArenaDelete(a)`，**自动释放所有 key 的内存**——不用像之前 `Vec<Str> keys` 方案那样手动 `str::Free` 每个 key。 sumatrapdf:214-217 
- 默认 `initialSize` 是 `kHashTableInitialSize = 16*1024`，对你这种小 map（几个 IP）太浪费，构造时应显式传小一点的初始值（仓库里 `HtmlFormatter.cpp` 就是这么做的：`new dict::MapStrToInt(measureCacheInitialSize)`）。 sumatrapdf:8-15 sumatrapdf:231-234 

## 1. 字段结构：`MapStrToInt` + 一个平行值数组

```cpp
// key -> index into `values`；值是结构体（每个元素是 sub->size 字节的堆块，
// 和之前 StructArray 用 Vec<void*> + AllocArray<u8>(size) 的做法一致）
struct MapRaw {
    dict::MapStrToInt keys{16}; // 小初始容量，见上面 caveat
    Vec<u8*> values;
};

// key -> index into `values`；值是字符串
struct MapStringRaw {
    dict::MapStrToInt keys{16};
    Vec<Str> values;
};
```

`Cluster` 声明不变，只是字段类型从 `MapField<IpInfo>` 换成 `MapRaw`：

```cpp
struct Cluster {
    Info info;
    MapRaw ipPool; // 值是 IpInfo，由 gIpInfoInfo 描述
};
```

## 2. 解析逻辑

`Insert` 只在 key 首次出现时插入；如果 key 已存在，`Insert` 返回 `false` 并把已有值写进 `existingValOut`，正好用来"查找或创建"下标：

```cpp
static u8* MapRawSlot(const StructInfo* valueInfo, MapRaw* m, Str key) {
    int idx;
    if (!m->keys.Insert(key, len(m->values), &idx)) {
        // key 已存在，idx 已经被 Insert 写成已有值
    } else {
        idx = len(m->values); // 刚插入，Insert 的第二个参数就是我们要用的新下标
    }
    while (len(m->values) <= idx) {
        m->values.Append(AllocArray<u8>(valueInfo->size));
    }
    return m->values.els[idx];
}

static Str* MapStringSlot(MapStringRaw* m, Str key) {
    int idx;
    if (!m->keys.Insert(key, len(m->values), &idx)) {
        // 已存在
    } else {
        idx = len(m->values);
    }
    while (len(m->values) <= idx) {
        m->values.Append(Str{});
    }
    return &m->values.els[idx];
}
```

`OnValue` 里的改动只是把之前"动态 key" 分支从操作两个 `Vec<Str>`/`Vec<T>` 换成调用上面两个 helper：

```cpp
if (mapField) {
    Str key = json::PathSegKey(seg); // 动态 key，如 "192.168.1.1"
    if (mapField->type == Type::MapStruct) {
        auto* m = (MapRaw*)mapBase;
        base = MapRawSlot(mapField->sub, m, key);
        info = mapField->sub;
    } else { // MapString
        auto* m = (MapStringRaw*)mapBase;
        Str* slot = MapStringSlot(m, key);
        base = (u8*)slot;
        static FieldInfo strLeaf{"", 0, Type::String};
        leafField = &strLeaf;
    }
    mapField = nullptr;
    continue;
}
```

## 3. 递归释放

由于 `MapStrToInt` 自己管理 key 内存，释放逻辑比"两个平行 `Vec<Str>`"方案简单：不用遍历 key 数组逐个 `str::Free`，只需析构 `MapStrToInt` 本身（释放它的 arena）并释放 `values` 里每个元素：

```cpp
static void FreeMapRaw(MapRaw* m, const StructInfo* valueInfo) {
    if (!m) return;
    for (int i = 0; i < len(m->values); i++) {
        FreeData(valueInfo, m->values.els[i]);
        free(m->values.els[i]);
    }
    m->values.Reset();
    m->keys.~MapStrToInt(); // 释放内部哈希表 + arena（含所有 key 副本）
}

static void FreeMapString(MapStringRaw* m) {
    if (!m) return;
    for (int i = 0; i < len(m->values); i++) {
        str::Free(m->values.els[i]);
    }
    m->values.Reset();
    m->keys.~MapStrToInt();
}
```

`FreeData` 里对应分支：

```cpp
case Type::MapStruct:
    FreeMapRaw((MapRaw*)fieldPtr, f.sub);
    break;
case Type::MapString:
    FreeMapString((MapStringRaw*)fieldPtr);
    break;
```

## 需要特别注意的坑

1. **`MapStrToInt` 不是 POD**：构造函数会 `a = ArenaNew(); h = NewHashTable(...)`。如果你的整个结构体（含内嵌 `MapRaw`）是靠 `AllocArray<u8>(info->size)`（相当于 `calloc`）一次性分配的，`MapRaw::keys` 字段里的 `a`/`h` 会是垃圾/零值，而不是真正构造出来的对象——之后调用 `Insert` 会直接崩溃。**必须在给结构体分配内存之后、解析之前，对每个 `MapStruct`/`MapString` 字段做 placement-new**：`new (fieldPtr) MapRaw();`（或者干脆让 `Deserialize` 递归分配阶段，对每个 `Map*` 类型字段单独 placement-new，而不是依赖整块内存清零）。这一点和之前提到的 `StrVecWithData<T>` 面临的问题完全一样。 sumatrapdf:208-212 
2. `values.els[idx] = m->values.els[idx]` 假设 `Vec<T>` 支持 `len()`/`Append`/`els[i]` 下标访问，这与仓库里 `Vec<int>`/`Vec<Str>` 的既有用法一致。 sumatrapdf:27-61 
3. `MapStrToInt::Insert` 的 key 查重是哈希表 O(1)，比之前"两个平行数组 + 线性扫描找 key"的 O(n) 方案更适合 key 数量较多的场景；但哈希表本身有额外的固定开销（entries 数组 + arena），如果 map 只有个位数条目，之前"两个 `Vec`"方案反而更省内存——这是你需要根据实际 `ipPool` 规模自己权衡的取舍点，我这里只是按你的要求做替换，没有对两种方案做性能测量。
4. 同样提醒：整套 `jsonrefl`/`MapRaw` 设计不存在于仓库中，是基于真实存在的 `dict::MapStrToInt` API sumatrapdf:19-33  和 `Vec<T>` sumatrapdf:27-61  手写的草案，未编译验证，务必先写单元测试。
### Citations
**File:** src/base/Dict.h (L8-15)
```text
// we are very generous with default initial size. It's a trade-off
// between memory used by hash table and how often we need to resize it.
// We allocate size*sizeof(ptr) which is 64k on 32-bit for 16k entries.
// 64k is very little on today's machines, especially for short-lived
// hash tables.
// Should use smaller values for long-lived hash tables, especially
// if there are many of them.
constexpr int kHashTableInitialSize = 16 * 1024;
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
**File:** src/base/Dict.cpp (L208-281)
```cpp
MapStrToInt::MapStrToInt(int initialSize) {
    // arena-allocate HashTableEntry entries and copies of string keys
    a = ArenaNew();
    h = NewHashTable(initialSize, a);
}

MapStrToInt::~MapStrToInt() {
    DeleteHashTable(h);
    ArenaDelete(a);
}

int MapStrToInt::Count() const {
    return h->nUsed;
}

// if a key exists:
//   * returns false
//   * sets existingValOut to existing value
//   * sets existingKeyOut to (interned) existing key

// if a key doesn't exist:
//   * returns true
//   * inserts a copy of the key allocated with the arena
//   * sets existingKeyOut to (interned) key
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
**File:** src/HtmlFormatter.cpp (L231-234)
```cpp
    MeasureCache* mc = &measureCaches[nMeasureCaches++];
    mc->font = font;
    mc->keys = new dict::MapStrToInt(measureCacheInitialSize);
    lastMeasureCache = mc;
```
**File:** src/base/Vec.h (L27-61)
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

    explicit Vec() = default;
```
## Q8
Deserialize 递归分配阶段，对每个 Map* 类型字段单独 placement-new，需要怎么实现
### Answer
Placement-new only matters for fields whose *default* state isn't all-zero-bytes — in this hypothetical `jsonrefl` framework that's only `MapRaw`/`MapStringRaw` (because they embed `dict::MapStrToInt`, which needs `ArenaNew()` + `NewHashTable()` to have a valid `a`/`h`) sumatrapdf:208-212 . Everything else (`Str`, `Vec<T>`, `int`, `bool`, pointer fields) is fine with zero-init because that's exactly what their default member initializers evaluate to (`Vec<T>::len=0, cap=0, els=nullptr` sumatrapdf:27-34 ).

So you need a `ConstructData` pass that is the mirror image of `FreeData` — same recursive field walk, but instead of tearing down non-trivial fields it brings them up. It has to run at **every place a raw block of `info->size` bytes gets allocated**, not just the top-level `Deserialize` call, because nested structs, struct-array elements, and map values are all separately-allocated blocks that can themselves contain a `Map*` field deeper down.

## 1. The recursive constructor

```cpp
static void ConstructData(const StructInfo* info, u8* base) {
    for (size_t i = 0; i < info->nFields; i++) {
        const FieldInfo& f = info->fields[i];
        u8* fieldPtr = base + f.offset;
        switch (f.type) {
            case Type::Struct:
                // embedded struct lives inline in this same block; recurse in place
                ConstructData(f.sub, fieldPtr);
                break;
            case Type::MapStruct:
                new (fieldPtr) MapRaw();       // ArenaNew()+NewHashTable() happens here
                break;
            case Type::MapString:
                new (fieldPtr) MapStringRaw();
                break;
            // StructArray / primitive arrays / String / Int / Float / Bool:
            // their at-rest state is all-zero, nothing to do here.
            // Their *elements*, once allocated, get constructed at the
            // allocation site (see below), not here.
            default:
                break;
        }
    }
}
```

This only needs to walk `Struct` fields eagerly (they're physically embedded in the same memory block), while `StructArray`/`Map*` element blocks are allocated later, on demand, during parsing — so they're constructed right where they're allocated instead.

## 2. Wire it into every allocation site

**Top-level `Deserialize`:**

```cpp
void* Deserialize(const StructInfo* info, Str jsonData) {
    void* data = AllocArray<u8>(info->size);
    ConstructData(info, (u8*)data);          // <-- added
    DeserCtx ctx{info, data};
    json::Parse(jsonData, MkFunc1<DeserCtx, json::Value*>(OnValue, &ctx));
    return data;
}
```

**`StructArray` element growth** (inside `OnValue`, where new array elements are appended):

```cpp
while (len(*vec) <= idx) {
    u8* el = AllocArray<u8>(info->size);
    ConstructData(info, el);                  // <-- added
    vec->Append(el);
}
```

**Map value slot growth** (`MapRawSlot` from the previous answer):

```cpp
static u8* MapRawSlot(const StructInfo* valueInfo, MapRaw* m, Str key) {
    int idx;
    if (!m->keys.Insert(key, len(m->values), &idx)) {
        // key existed, idx already holds the stored value
    } else {
        idx = len(m->values);
    }
    while (len(m->values) <= idx) {
        u8* el = AllocArray<u8>(valueInfo->size);
        ConstructData(valueInfo, el);          // <-- added
        m->values.Append(el);
    }
    return m->values.els[idx];
}
```

`MapStringRaw` doesn't need this since its values are `Str` (POD-safe).

## 3. Symmetry check with `FreeData`

Every place that calls `ConstructData(X, ptr)` on allocation must have a matching call to the destructor logic in `FreeData(X, ptr)` before that block is `free()`'d. That's already the case in the design from the previous answer: `FreeStructArray`/loop over `values.els[i]` calls `FreeData(...)` then `free(...)` for each struct-array/map-value element, and `FreeMapRaw`/`FreeMapString` explicitly call `m->keys.~MapStrToInt();` for the embedded map fields. As long as you keep `ConstructData` and `FreeData` field-type dispatch in lockstep (same `switch` cases, one constructing, one destructing), you won't leak or double-free.

## Caveats

- This entire `jsonrefl`/`MapRaw` design, including `ConstructData`, does **not exist in the SumatraPDF repository** — it's a hand-designed extension built only on real, verified primitives: `dict::MapStrToInt`'s constructor/destructor behavior sumatrapdf:208-217  and `Vec<T>`'s zero-init defaults sumatrapdf:27-61 . None of it has been compiled or tested — write unit tests before using it, per `src/base/tests/JsonParser_ut.cpp`'s style.
- If you ever add another non-trivially-constructible field type to this framework (e.g. adopting `StrVecWithData<T>` from an earlier answer instead of `MapRaw`), you must add a matching case to *both* `ConstructData` and `FreeData`, and to *every* allocation site (top-level `Deserialize`, `StructArray` growth, `Map*` value growth) — there's no single choke point that catches all of them automatically, since blocks get allocated in three different places.
- An alternative that avoids needing three separate "remember to placement-new" call sites: give every allocation of a `info->size`-byte block go through one shared helper, e.g. `static u8* AllocAndConstruct(const StructInfo* info) { u8* p = AllocArray<u8>(info->size); ConstructData(info, p); return p; }`, and use that helper everywhere instead of raw `AllocArray<u8>(...)` calls — reduces the risk of forgetting one call site.
### Citations
**File:** src/base/Dict.cpp (L208-217)
```cpp
MapStrToInt::MapStrToInt(int initialSize) {
    // arena-allocate HashTableEntry entries and copies of string keys
    a = ArenaNew();
    h = NewHashTable(initialSize, a);
}

MapStrToInt::~MapStrToInt() {
    DeleteHashTable(h);
    ArenaDelete(a);
}
```
**File:** src/base/Vec.h (L27-61)
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

    explicit Vec() = default;
```
## Q9
增加MapArray和MapStructArray解析功能
### Answer
Extending the same hand-designed `jsonrefl` framework: `MapArray` (dynamic key → array of primitives) and `MapStructArray` (dynamic key → array of structs) are "one level deeper" than the `MapStruct`/`MapString` case already covered — the map value itself is a `Vec<T>*`/`Vec<void*>*` instead of a scalar/struct block. Confirmed `Vec<T>::Append(const T&)` signature to make sure the array-growth code below is accurate: sumatrapdf:121-121 sumatrapdf:27-61 

## 1. New `Type` values and storage shape

```cpp
enum class Type { String, Int, Float, Bool, Struct, StructArray,
                   IntArray, FloatArray, BoolArray, StringArray,
                   MapStruct, MapString,
                   MapIntArray, MapFloatArray, MapBoolArray, MapStringArray, // NEW
                   MapStructArray };                                          // NEW
```

Reuse the `MapRaw`-style layout, but each value slot is now itself a **pointer to an array** rather than a flat struct block:

```cpp
// dynamic key -> Vec<int>* / Vec<float>* / Vec<bool>* / Vec<Str>*
struct MapArrayRaw {
    dict::MapStrToInt keys{16};
    Vec<void*> values; // each element: heap Vec<T>* cast to void*
};

// dynamic key -> Vec<void*>* (array of struct blocks, same shape as StructArray)
struct MapStructArrayRaw {
    dict::MapStrToInt keys{16};
    Vec<void*> values; // each element: heap Vec<void*>*
};
```

Example for `{"ipPool": {"192.168.1.1": [8080, 8443]}}` (map → int array) and `{"ipPool": {"192.168.1.1": [{"port":8080},{"port":8443}]}}` (map → struct array):

```cpp
struct Cluster {
    Info info;
    MapArrayRaw ipPorts;         // Type::MapIntArray, sub = nullptr (primitive)
    MapStructArrayRaw ipEntries; // Type::MapStructArray, sub = &gIpInfoInfo
};
```

## 2. Parsing: extending `OnValue`

Paths now look like `/ipPool -> /192.168.1.1 -> i0` (leaf is the array element, one more segment than the `MapStruct`/`MapString` case). The state machine needs a second "memory" slot: after resolving the dynamic key to a value slot, that slot itself behaves like a normal array field, so the existing `kSegIdx` array-growth logic can be reused almost verbatim — just retarget it at the map's slot pointer instead of a static field's `fieldPtr`.

```cpp
static void* MapArraySlot(Vec<void*>* mapValues, dict::MapStrToInt* keys, Str key,
                           bool isStructArray) {
    int idx;
    if (!keys->Insert(key, len(*mapValues), &idx)) {
        // key already existed; idx holds the stored value
    } else {
        idx = len(*mapValues);
    }
    while (len(*mapValues) <= idx) {
        mapValues->Append(nullptr); // array not yet created; created lazily on first element
    }
    return &mapValues->els[idx]; // return address of the void* slot itself (may be null)
}

static void OnValue(DeserCtx* ctx, json::Value* v) {
    const StructInfo* info = ctx->rootInfo;
    u8* base = (u8*)ctx->rootData;
    const FieldInfo* leafField = nullptr;
    const FieldInfo* mapField = nullptr;
    u8* mapBase = nullptr;
    void** dynArraySlot = nullptr; // NEW: address of the void* holding Vec<T>*/Vec<void*>*

    for (StrNode* seg = v->path; seg; seg = seg->next) {
        if (seg->s.s[0] == json::kSegKey) {
            Str key = json::PathSegKey(seg);

            if (mapField) {
                bool isArrayMap = mapField->type == Type::MapIntArray ||
                                   mapField->type == Type::MapFloatArray ||
                                   mapField->type == Type::MapBoolArray ||
                                   mapField->type == Type::MapStringArray ||
                                   mapField->type == Type::MapStructArray;
                if (isArrayMap) {
                    bool isStructArr = mapField->type == Type::MapStructArray;
                    auto* raw = isStructArr ? &((MapStructArrayRaw*)mapBase)->values
                                             : &((MapArrayRaw*)mapBase)->values;
                    dict::MapStrToInt* keys = isStructArr ? &((MapStructArrayRaw*)mapBase)->keys
                                                            : &((MapArrayRaw*)mapBase)->keys;
                    dynArraySlot = (void**)MapArraySlot(raw, keys, key, isStructArr);
                    info = mapField->sub; // element StructInfo, used only if MapStructArray
                } else if (mapField->type == Type::MapStruct) {
                    auto* m = (MapRaw*)mapBase;
                    base = MapRawSlot(mapField->sub, m, key);
                    info = mapField->sub;
                } else { // MapString
                    auto* m = (MapStringRaw*)mapBase;
                    base = (u8*)MapStringSlot(m, key);
                    static FieldInfo strLeaf{"", 0, Type::String};
                    leafField = &strLeaf;
                }
                mapField = nullptr;
                continue;
            }

            if (!info) return;
            const FieldInfo* f = FindField(info, key);
            if (!f) return;
            u8* fieldPtr = base + f->offset;

            bool isMapField = f->type == Type::MapStruct || f->type == Type::MapString ||
                               f->type == Type::MapIntArray || f->type == Type::MapFloatArray ||
                               f->type == Type::MapBoolArray || f->type == Type::MapStringArray ||
                               f->type == Type::MapStructArray;
            if (isMapField) {
                mapField = f;
                mapBase = fieldPtr;
                info = nullptr;
                continue;
            }
            if (f->type == Type::Struct) { info = f->sub; base = fieldPtr; continue; }
            // ... StructArray / plain primitive-array / leaf branches unchanged ...
            leafField = f; base = fieldPtr;

        } else if (seg->s.s[0] == json::kSegIdx) {
            int idx = json::PathSegIndex(seg);

            if (dynArraySlot) {
                // growing the array that lives at a dynamic map key's slot
                if (!*dynArraySlot) {
                    // lazily create the backing Vec<T>/Vec<void*> on first element
                    *dynArraySlot = info ? (void*)new Vec<void*>() : (void*)new Vec<int>();
                    // NOTE: which concrete Vec<T> to `new` depends on mapField's element
                    // primitive type; see caveat below on why this needs a type tag,
                    // not just a null-check, once more than one primitive type is in play.
                }
                if (info) { // MapStructArray: elements are struct blocks
                    auto* vec = (Vec<void*>*)*dynArraySlot;
                    while (len(*vec) <= idx) {
                        u8* el = AllocArray<u8>(info->size);
                        ConstructData(info, el);
                        vec->Append(el);
                    }
                    base = vec->els[idx]; // struct leaf fields resolved by subsequent kSegKey segs
                } else { // MapIntArray / MapFloatArray / MapBoolArray / MapStringArray
                    // dispatch on mapField's declared primitive kind (mapField still valid
                    // via a saved copy — see caveat) and grow the concrete Vec<T> the same
                    // way IntArray/FloatArray/BoolArray/StringArray already do
                }
                continue;
            }
            // ... existing plain StructArray / primitive-array kSegIdx handling unchanged ...
        }
    }
    if (leafField) SetLeaf(leafField, base, v);
}
```

## 3. Recursive free

```cpp
static void FreeMapArrayRaw(MapArrayRaw* m, Type elemType) {
    if (!m) return;
    for (int i = 0; i < len(m->values); i++) {
        void* p = m->values.els[i];
        if (!p) continue;
        switch (elemType) {
            case Type::MapIntArray:    delete (Vec<int>*)p; break;
            case Type::MapFloatArray:  delete (Vec<float>*)p; break;
            case Type::MapBoolArray:   delete (Vec<bool>*)p; break;
            case Type::MapStringArray: {
                auto* vs = (Vec<Str>*)p;
                for (int j = 0; j < len(*vs); j++) str::Free(vs->els[j]);
                delete vs;
                break;
            }
            default: break;
        }
    }
    m->values.Reset();
    m->keys.~MapStrToInt();
}

static void FreeMapStructArrayRaw(MapStructArrayRaw* m, const StructInfo* elemInfo) {
    if (!m) return;
    for (int i = 0; i < len(m->values); i++) {
        auto* vec = (Vec<void*>*)m->values.els[i];
        if (!vec) continue;
        for (int j = 0; j < len(*vec); j++) {
            FreeData(elemInfo, (u8*)vec->els[j]);
            free(vec->els[j]);
        }
        delete vec;
    }
    m->values.Reset();
    m->keys.~MapStrToInt();
}
```

`FreeData`'s dispatch gains matching cases:

```cpp
case Type::MapIntArray: case Type::MapFloatArray:
case Type::MapBoolArray: case Type::MapStringArray:
    FreeMapArrayRaw((MapArrayRaw*)fieldPtr, f.type);
    break;
case Type::MapStructArray:
    FreeMapStructArrayRaw((MapStructArrayRaw*)fieldPtr, f.sub);
    break;
```

`ConstructData` needs one addition too, since `MapArrayRaw`/`MapStructArrayRaw` embed `dict::MapStrToInt` (same non-zero-default problem covered earlier):

```cpp
case Type::MapIntArray: case Type::MapFloatArray:
case Type::MapBoolArray: case Type::MapStringArray:
    new (fieldPtr) MapArrayRaw();
    break;
case Type::MapStructArray:
    new (fieldPtr) MapStructArrayRaw();
    break;
```

(`Vec<void*> values` inside both wrapper structs is zero-init-safe per `Vec<T>`'s default member initializers, so the placement-new is only needed to fix up the embedded `MapStrToInt`.)

## Caveats — real design gaps, not just "unverified"

1. **The `MapIntArray` lazy-creation code above (`*dynArraySlot = info ? ... : (void*)new Vec<int>()`) is wrong/incomplete as written** — it can't tell `Vec<int>*` apart from `Vec<float>*`/`Vec<bool>*`/`Vec<Str>*` just from a null check. You need to keep `mapField` (or at least `mapField->type`) alive across the `kSegKey`→`kSegIdx` transition (store it in a small piece of per-call state, e.g. `Type dynArrayElemType`) and switch on it explicitly when lazily allocating and when appending each element — mirroring exactly how the existing plain `IntArray`/`FloatArray`/`BoolArray`/`StringArray` leaf-array code (from the earlier "support primitive arrays" answer) dispatches on `f->type`.
2. This whole `MapArrayRaw`/`MapStructArrayRaw`/`OnValue` extension is, like every prior answer in this thread, a **hand-written design that does not exist in the SumatraPDF repository** — it's built only on confirmed real APIs (`dict::MapStrToInt` sumatrapdf:19-33 , `Vec<T>::Append`/defaults sumatrapdf:27-61 , `json::Parse`'s path segments sumatrapdf:13-20 ). None of it has been compiled. Before integrating, write it up as unit tests in the style of `src/base/tests/JsonParser_ut.cpp` and fix the type-dispatch gap in point 1 first — that's a correctness bug, not a stylistic nit.
3. As before, `Vec<T>` here is used as a "POD-only" container (its copy assignment is `memcpy`-based) sumatrapdf:63-90 ; storing `void*` element pointers (rather than nesting `Vec<Vec<T>>` directly) sidesteps that constraint, which is why every array-of-array/map-of-array slot above is `Vec<void*>` of heap-allocated sub-`Vec`s rather than a truly nested `Vec<Vec<T>>`.
### Citations
**File:** src/base/Vec.h (L27-61)
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

    explicit Vec() = default;
```
**File:** src/base/Vec.h (L63-90)
```text
    // ensure that a Vec never shares its els buffer with another after a clone/copy
    // note: we don't inherit allocator as it's not needed for our use cases
    Vec(const Vec& other) {
        VecReserve(*this, other.len);
        len = other.len;
        // using memcpy, as Vec only supports POD types
        if (other.len > 0 && other.els && els) {
            memcpy((void*)els, (const void*)other.els, sizeof(T) * (size_t)other.len);
        }
    }

    // TODO: write Vec(const Vec&& other)

    Vec& operator=(const Vec& other) {
        if (this == &other) {
            return *this;
        }

        Reset();
        VecReserve(*this, other.len);
        // using memcpy, as Vec only supports POD types
        len = other.len;
        if (other.len > 0) {
            memcpy((void*)els, (const void*)other.els, sizeof(T) * (size_t)len);
            memset((void*)(els + len), 0, sizeof(T) * (size_t)(cap - len));
        }
        return *this;
    }
```
**File:** src/base/Vec.h (L121-121)
```text
    bool Append(const T& el) { return InsertAt(len, el); }
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
**File:** src/base/JsonParser.h (L13-20)
```text
// Path is a StrNode list (outermost segment first). Each node's s stores:
//   '/' + key   object key (rest is the key bytes, may contain '/' etc.)
//   'i' + digits  array index
//   '*'         match-only "any segment" (not produced by the parser)
// Path is only valid during the VisitFn call; the parser reuses/mutates the list after.
constexpr char kSegKey = '/';
constexpr char kSegIdx = 'i';
constexpr char kSegAny = '*';
```
## Q10
完善*dynArraySlot = info ? ... : (void*)new Vec<int>()区分Vec<float>*/Vec<bool>*/Vec<Str>*
### Answer
Fix: the missing piece is keeping the *element type* (not just "is it a struct array or not") alive across the `kSegKey → kSegIdx` transition, then switching on it explicitly at both allocation time and per-element append time — the same way the plain (non-map) `IntArray`/`FloatArray`/`BoolArray`/`StringArray` branches already dispatch on `f->type`.

## 1. Carry the element type through the state machine

Add one field to the per-call walk state:

```cpp
static void OnValue(DeserCtx* ctx, json::Value* v) {
    ...
    const FieldInfo* mapField = nullptr;
    Type dynArrayElemType = Type::Int;   // NEW: remembers which primitive Vec<T> to use
    void** dynArraySlot = nullptr;

    for (StrNode* seg = v->path; seg; seg = seg->next) {
        if (seg->s.s[0] == json::kSegKey) {
            ...
            if (mapField) {
                bool isArrayMap = mapField->type == Type::MapIntArray ||
                                   mapField->type == Type::MapFloatArray ||
                                   mapField->type == Type::MapBoolArray ||
                                   mapField->type == Type::MapStringArray ||
                                   mapField->type == Type::MapStructArray;
                if (isArrayMap) {
                    bool isStructArr = mapField->type == Type::MapStructArray;
                    auto* raw = isStructArr ? &((MapStructArrayRaw*)mapBase)->values
                                             : &((MapArrayRaw*)mapBase)->values;
                    dict::MapStrToInt* keys = isStructArr ? &((MapStructArrayRaw*)mapBase)->keys
                                                            : &((MapArrayRaw*)mapBase)->keys;
                    dynArraySlot = (void**)MapArraySlot(raw, keys, key, isStructArr);
                    dynArrayElemType = mapField->type;      // NEW: remember before we clear mapField
                    info = isStructArr ? mapField->sub : nullptr;
                }
                ...
                mapField = nullptr;
                continue;
            }
            ...
        } else if (seg->s.s[0] == json::kSegIdx) {
            int idx = json::PathSegIndex(seg);

            if (dynArraySlot) {
                switch (dynArrayElemType) {
                    case Type::MapStructArray: {
                        if (!*dynArraySlot) {
                            *dynArraySlot = new Vec<void*>();
                        }
                        auto* vec = (Vec<void*>*)*dynArraySlot;
                        while (len(*vec) <= idx) {
                            u8* el = AllocArray<u8>(info->size);
                            ConstructData(info, el);
                            vec->Append(el);
                        }
                        base = vec->els[idx];
                        break;
                    }
                    case Type::MapIntArray: {
                        if (!*dynArraySlot) *dynArraySlot = new Vec<int>();
                        auto* vec = (Vec<int>*)*dynArraySlot;
                        while (len(*vec) <= idx) vec->Append(0);
                        (*vec)[idx] = str::ParseInt(v->value);
                        break;
                    }
                    case Type::MapFloatArray: {
                        if (!*dynArraySlot) *dynArraySlot = new Vec<float>();
                        auto* vec = (Vec<float>*)*dynArraySlot;
                        while (len(*vec) <= idx) vec->Append(0.f);
                        str::Parse(v->value, "%f", &(*vec)[idx]);
                        break;
                    }
                    case Type::MapBoolArray: {
                        if (!*dynArraySlot) *dynArraySlot = new Vec<bool>();
                        auto* vec = (Vec<bool>*)*dynArraySlot;
                        while (len(*vec) <= idx) vec->Append(false);
                        (*vec)[idx] = str::Eq(v->value, "true");
                        break;
                    }
                    case Type::MapStringArray: {
                        if (!*dynArraySlot) *dynArraySlot = new Vec<Str>();
                        auto* vec = (Vec<Str>*)*dynArraySlot;
                        while (len(*vec) <= idx) vec->Append(Str{});
                        (*vec)[idx] = str::Dup(v->value);
                        break;
                    }
                    default:
                        ReportIf(true); // unreachable: dynArraySlot only set for the 5 map-array types
                }
                continue;
            }
            // ... existing plain StructArray / primitive-array kSegIdx handling unchanged ...
        }
    }
    ...
}
```

Key points:
- `dynArrayElemType` is set once, at the moment the dynamic key is resolved (right before `mapField` gets cleared), so it survives into the subsequent `kSegIdx` iterations for that same value.
- The `MapStructArray` case is folded into the same `switch` (rather than a separate `info ? ... : ...` null check) so there's exactly one dispatch point instead of two inconsistent ones.
- For scalar leaves (`MapIntArray`/`MapFloatArray`/`MapBoolArray`/`MapStringArray`), `base`/`leafField` are **not** used — the value is written directly into the `Vec<T>` slot inside the `kSegIdx` branch itself, since these array elements are also the leaf (no further `kSegKey` segment follows before the callback fires). `SetLeaf` is not needed for this path.
- `str::ParseInt`, `str::Parse(..., "%f", ...)`, `str::Eq(v->value, "true")` are the same conversions used in the plain (non-map) `IntArray`/`FloatArray`/`BoolArray` branches from the earlier answer — kept consistent rather than introducing new parsing helpers.

## 2. Matching free-side dispatch (already correct, just confirming it lines up)

`FreeMapArrayRaw`'s `switch (elemType)` from the previous answer already mirrors this same `Type` enum, so no changes are needed there — it was already keyed on `f.type`/`elemType` rather than a null check, unlike the construction side which had the bug.

## Caveats

- This is still part of the same hand-designed `jsonrefl` framework that **does not exist in the SumatraPDF repository** — nothing here has been compiled or tested. I verified `Vec<T>::Append(const T&)`'s signature exists as shown, but I was not able to fully confirm `str::ParseInt`/`str::Parse`'s exact signatures in `src/base/Str.h` in this session (search results didn't return the matching lines' content), so double check those against `src/base/Str.h` before use. sumatrapdf:121-121 
- `dynArrayElemType`'s default value (`Type::Int`) is just an arbitrary initializer to keep it well-defined before first use; it's never read unless `dynArraySlot` is non-null, so the specific default doesn't matter.
- Write unit tests (per `src/base/tests/JsonParser_ut.cpp` conventions) covering all four scalar map-array variants plus `MapStructArray`, including the "key repeats across multiple JSON objects" and "empty array value" edge cases, before integrating this into real code.
### Citations
**File:** src/base/Vec.h (L121-121)
```text
    bool Append(const T& el) { return InsertAt(len, el); }
```
