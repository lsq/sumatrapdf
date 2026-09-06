/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include <cstddef>
#include "base/Base.h"
#include "base/JsonParser.h"
#include "base/Dict.h"
#include "base/JsonUtil.h"

// must be last due to assert() over-write
#include "base/tests/UtAssert.h"

const Str demo = StrL(
    R"(
                {
                    "name": "honey Ratel",
                    "ages": [10, 20, 30],
                    "wifes": [{"name":"nancy", "age": 30,"info": {
                        "children": [
                            {
                                "name":"Tom",
                                "age": 10,
                                "sex": "Male"
                            },
                            {
                                "name":"Jeff",
                                "age": 8,
                                "sex": "Male"
                            }
                        ],
                        "friend": [
                            {
                                "name":"Lily",
                                "age": 12,
                                "sex": "Female"
                            },
                            {
                                "name":"Brown",
                                "age": 10,
                                "sex": "Male"
                            }
                        ]
                    }},
                    {"name":"Angle", "age": 20,"info": {
                        "children": [
                            {
                                "name":"Brown",
                                "age": 10,
                                "sex": "Male"
                            },
                            {
                                "name":"Lily",
                                "age": 8,
                                "sex": "Male"
                            }
                        ],
                        "friend": [
                            {
                                "name":"Tai",
                                "age": 12,
                                "sex": "Female"
                            },
                            {
                                "name":"BuJing",
                                "age": 10,
                                "sex": "Male"
                            }
                        ]
                    }}],
                    "house": {
                        "New York": ["street No.1"],
                        "Bei Jing": ["One", "Two", "Three"]
                    }
                }
            )");

struct ExpectedValue {
    Str path;
    Str value;
    json::Type type{json::Type::String};

    ExpectedValue() = default;
    ExpectedValue(Str path, Str value, json::Type type = json::Type::String) : path(path), value(value), type(type) {}
};

struct JsonVerifier {
    const ExpectedValue* data = nullptr;
    size_t dataLen = 0;
    size_t idx = 0;

    JsonVerifier(const ExpectedValue* data, size_t dataLen) : data(data), dataLen(dataLen), idx(0) {}
    ~JsonVerifier() { utassert(dataLen == idx); }

    void OnValue(json::Value& v) {
        utassert(idx < dataLen);
        const ExpectedValue& d = data[idx];
        utassert(v.type == d.type);
        utassert(str::Eq(json::PathFormatTemp(v.path), d.path));
        utassert(str::Eq(v.value, d.value));
        idx++;
    }
};

static bool ParseVerify(Str json, JsonVerifier* verifier) {
    json::Reader r(json);
    for (json::Value& v : r) {
        verifier->OnValue(v);
    }
    return r.Ok();
}

// how many values json has; ok tells if it was valid
static int CountValues(Str json, bool* ok) {
    int n = 0;
    json::Reader r(json);
    for (json::Value& v : r) {
        (void)v;
        n++;
    }
    *ok = r.Ok();
    return n;
}

void JsonTest() {
    static const struct {
        Str json;
        ExpectedValue value;
    } validJsonData[] = {
        // strings
        {StrL("\"test\""), ExpectedValue(StrL(""), StrL("test"))},
        {StrL("\"\\\\\\n\\t\\u01234\""), ExpectedValue(StrL(""), StrL("\\\n\t\xC4\xA3"
                                                                      "4"))},
        // \u0000 is valid JSON
        {StrL("\"\\u0000\""), ExpectedValue(StrL(""), Str("\0", 1))},
        // numbers
        {StrL("123"), ExpectedValue(StrL(""), StrL("123"), json::Type::Number)},
        {StrL("-99.99"), ExpectedValue(StrL(""), StrL("-99.99"), json::Type::Number)},
        {StrL("1.2E+15"), ExpectedValue(StrL(""), StrL("1.2E+15"), json::Type::Number)},
        {StrL("0e-7"), ExpectedValue(StrL(""), StrL("0e-7"), json::Type::Number)},
        // keywords
        {StrL("true"), ExpectedValue(StrL(""), StrL("true"), json::Type::Bool)},
        {StrL("false"), ExpectedValue(StrL(""), StrL("false"), json::Type::Bool)},
        {StrL("null"), ExpectedValue(StrL(""), StrL("null"), json::Type::Null)},
        // dictionaries
        {StrL("{\"key\":\"test\"}"), ExpectedValue(StrL("/key"), StrL("test"))},
        {StrL("{ \"no\" : 123 }"), ExpectedValue(StrL("/no"), StrL("123"), json::Type::Number)},
        {StrL("{ \"bool\": true }"), ExpectedValue(StrL("/bool"), StrL("true"), json::Type::Bool)},
        {StrL("{}"), ExpectedValue()},
        // arrays
        {StrL("[\"test\"]"), ExpectedValue(StrL("[0]"), StrL("test"))},
        {StrL("[123]"), ExpectedValue(StrL("[0]"), StrL("123"), json::Type::Number)},
        {StrL("[ null ]"), ExpectedValue(StrL("[0]"), StrL("null"), json::Type::Null)},
        {StrL("[]"), ExpectedValue()},
        // combination
        {StrL("{\"key\":[{\"name\":-987}]}"), ExpectedValue(StrL("/key[0]/name"), StrL("-987"), json::Type::Number)},
    };

    for (size_t i = 0; i < dimof(validJsonData); i++) {
        JsonVerifier verifier(&validJsonData[i].value, validJsonData[i].value.value ? 1 : 0);
        utassert(ParseVerify(validJsonData[i].json, &verifier));
    }

    static const struct {
        Str json;
        ExpectedValue value;
    } invalidJsonData[] = {
        // dictionaries
        {StrL("{\"key\":\"test\""), ExpectedValue(StrL("/key"), StrL("test"))},
        {StrL("{ \"no\" : 123, }"), ExpectedValue(StrL("/no"), StrL("123"), json::Type::Number)},
        {StrL("{\"key\":\"test\"]"), ExpectedValue(StrL("/key"), StrL("test"))},
        // arrays
        {StrL("[\"test\""), ExpectedValue(StrL("[0]"), StrL("test"))},
        {StrL("[123,]"), ExpectedValue(StrL("[0]"), StrL("123"), json::Type::Number)},
        {StrL("[\"test\"}"), ExpectedValue(StrL("[0]"), StrL("test"))},
    };

    for (size_t i = 0; i < dimof(invalidJsonData); i++) {
        JsonVerifier verifier(&invalidJsonData[i].value, 1);
        utassert(!ParseVerify(invalidJsonData[i].json, &verifier));
    }

    static Str invalidJson[] = {StrL(""),
                                StrL("string"),
                                StrL("nada"),
                                StrL("\"open"),
                                StrL("\"\\xC4\""),
                                StrL("\"\\u123h\""),
                                StrL("'string'"),
                                StrL("01"),
                                StrL(".1"),
                                StrL("12."),
                                StrL("1e"),
                                StrL("1.e5"),
                                StrL("1.E+2"),
                                StrL("1e+"),
                                StrL("1e-"),
                                StrL("-"),
                                StrL("-01"),
                                StrL("{"),
                                StrL("{,}"),
                                StrL("{\"key\": }"),
                                StrL("{\"key: 123 }"),
                                StrL("{ 'key': 123 }"),
                                StrL("["),
                                StrL("[,]")};

    JsonVerifier verifyError(nullptr, 0);
    {
        Str s = invalidJson[10]; // this one caused buffer overflow
        utassert(!ParseVerify(s, &verifyError));
    }

    for (size_t i = 0; i < dimof(invalidJson); i++) {
        utassert(!ParseVerify(invalidJson[i], &verifyError));
    }

    const ExpectedValue testData[] = {
        ExpectedValue(StrL("/ComicBookInfo/1.0/title"), StrL("Meta data demo")),
        ExpectedValue(StrL("/ComicBookInfo/1.0/publicationMonth"), StrL("4"), json::Type::Number),
        ExpectedValue(StrL("/ComicBookInfo/1.0/publicationYear"), StrL("2010"), json::Type::Number),
        ExpectedValue(StrL("/ComicBookInfo/1.0/credits[0]/primary"), StrL("true"), json::Type::Bool),
        ExpectedValue(StrL("/ComicBookInfo/1.0/credits[0]/role"), StrL("Writer")),
        ExpectedValue(StrL("/ComicBookInfo/1.0/credits[1]/primary"), StrL("false"), json::Type::Bool),
        ExpectedValue(StrL("/ComicBookInfo/1.0/credits[1]/role"), StrL("Publisher")),
        ExpectedValue(StrL("/ComicBookInfo/1.0/credits[2]"), StrL("null"), json::Type::Null),
        ExpectedValue(StrL("/appID"), StrL("Test/123")),
    };
    Str jsonSample = StrL(
        "{\n\
    \"ComicBookInfo/1.0\": {\n\
        \"title\": \"Meta data demo\",\n\
        \"publicationMonth\": 4,\n\
        \"publicationYear\": 2010,\n\
        \"credits\": [\n\
            { \"primary\": true, \"role\": \"Writer\" },\n\
            { \"primary\": false, \"role\": \"Publisher\" },\n\
            null\n\
        ]\n\
    },\n\
    \"appID\": \"Test/123\"\n\
}");
    JsonVerifier sampleVerifier(testData, dimof(testData));
    utassert(ParseVerify(jsonSample, &sampleVerifier));

    // U+2028 / U+2029 must be escaped so the result is safe in a JS string literal
    {
        const char lineSep[] = {'\xE2', '\x80', '\xA8'};
        const char paraSep[] = {'\xE2', '\x80', '\xA9'};
        utassert(str::Eq(json::EscapeStrTemp(Str(lineSep, 3)), StrL("\\u2028")));
        utassert(str::Eq(json::EscapeStrTemp(Str(paraSep, 3)), StrL("\\u2029")));
        utassert(str::Eq(json::EscapeStrTemp(StrL("a\"b\\c\n")), StrL("a\\\"b\\\\c\\n")));
        utassert(str::Eq(json::EscapeStrTemp(StrL("\b\f\r\t")), StrL("\\b\\f\\r\\t")));
    }

    // PathMatch / PathBuildTemp / PathSeg helpers
    {
        StrNode* p = json::PathBuildTemp(StrL("/key"), StrL("i0"), StrL("/name"));
        utassert(str::Eq(json::PathFormatTemp(p), StrL("/key[0]/name")));
        utassert(json::PathMatch(p, StrL("/key"), StrL("i0"), StrL("/name")));
        utassert(json::PathMatch(p, StrL("/key"), StrL("*"), StrL("/name")));
        utassert(json::PathMatch(p, StrL("*"), StrL("*"), StrL("*")));
        utassert(!json::PathMatch(p, StrL("/key"), StrL("i1"), StrL("/name")));
        utassert(!json::PathMatch(p, StrL("/key"), StrL("i0")));                            // too short
        utassert(!json::PathMatch(p, StrL("/key"), StrL("i0"), StrL("/name"), StrL("/x"))); // too long
        utassert(json::PathSegIndex(json::PathNth(p, 1)) == 0);
        utassert(json::PathSegIndex(p) == -1); // key segment
        utassert(str::Eq(json::PathSegKey(p), StrL("key")));
        utassert(str::Eq(json::PathSegKey(json::PathNth(p, 2)), StrL("name")));
        utassert(!json::PathSegKey(json::PathNth(p, 1)).s); // index segment
        utassert(!json::PathNth(p, 3));
        utassert(json::PathMatch(nullptr));
        utassert(!json::PathMatch(nullptr, StrL("/id")));

        // key containing '/' is one segment, not nested keys
        StrNode* slashKey = json::PathBuildTemp(StrL("/ComicBookInfo/1.0"), StrL("/title"));
        utassert(str::Eq(json::PathFormatTemp(slashKey), StrL("/ComicBookInfo/1.0/title")));
        utassert(json::PathMatch(slashKey, StrL("/ComicBookInfo/1.0"), StrL("/title")));
        utassert(!json::PathMatch(slashKey, StrL("/ComicBookInfo"), StrL("/1.0"), StrL("/title")));
        utassert(str::Eq(json::PathSegKey(slashKey), StrL("ComicBookInfo/1.0")));
    }

    // object key with '/' is a single path segment when parsed
    {
        int n = 0;
        json::Reader r(StrL("{\"a/b\":{\"c\":\"x\"}}"));
        for (json::Value& v : r) {
            n++;
            utassert(v.type == json::Type::String);
            utassert(str::Eq(v.value, StrL("x")));
            utassert(str::Eq(json::PathFormatTemp(v.path), StrL("/a/b/c")));
            utassert(json::PathMatch(v.path, StrL("/a/b"), StrL("/c")));
            utassert(!json::PathMatch(v.path, StrL("/a"), StrL("/b"), StrL("/c")));
            utassert(str::Eq(json::PathSegKey(v.path), StrL("a/b")));
            utassert(str::Eq(json::PathSegKey(json::PathNth(v.path, 1)), StrL("c")));
        }
        utassert(r.Ok());
        utassert(n == 1);
    }

    // stopping mid-parse: break out of the loop, the rest is never read
    {
        int n = 0;
        json::Reader r(StrL("{\"a\":1,\"b\":2,\"c\":3}"));
        for (json::Value& v : r) {
            n++;
            utassert(json::PathMatch(v.path, StrL("/a")));
            utassert(v.type == json::Type::Number);
            utassert(str::Eq(v.value, StrL("1")));
            break;
        }
        utassert(r.Ok());
        utassert(n == 1);
    }

    // stopping on the first array element
    {
        int n = 0;
        json::Reader r(StrL("[\"first\",\"second\"]"));
        for (json::Value& v : r) {
            n++;
            utassert(json::PathSegIndex(v.path) == 0);
            utassert(!v.path->next);
            utassert(str::Eq(v.value, StrL("first")));
            break;
        }
        utassert(r.Ok());
        utassert(n == 1);
    }

    // nesting depth limit is 128 (depth >= 128 fails)
    {
        auto nestArray = [](int depth) -> TempStr {
            str::Builder b;
            for (int i = 0; i < depth; i++) {
                b.AppendChar('[');
            }
            b.AppendChar('1');
            for (int i = 0; i < depth; i++) {
                b.AppendChar(']');
            }
            return ToStrTemp(b);
        };

        bool ok = false;
        utassert(CountValues(nestArray(127), &ok) == 1);
        utassert(ok);
        utassert(CountValues(nestArray(128), &ok) == 0);
        utassert(!ok);

        auto nestObject = [](int depth) -> TempStr {
            str::Builder b;
            for (int i = 0; i < depth; i++) {
                b.Append(StrL("{\"k\":"));
            }
            b.AppendChar('1');
            for (int i = 0; i < depth; i++) {
                b.AppendChar('}');
            }
            return ToStrTemp(b);
        };

        utassert(CountValues(nestObject(127), &ok) == 1);
        utassert(ok);
        utassert(CountValues(nestObject(128), &ok) == 0);
        utassert(!ok);
    }

    // UTF-8 BOM is skipped
    {
        int n = 0;
        char bomJson[] = {'\xEF', '\xBB', '\xBF', '7'};
        json::Reader r(Str(bomJson, 4));
        for (json::Value& v : r) {
            n++;
            utassert(!v.path);
            utassert(v.type == json::Type::Number);
            utassert(str::Eq(v.value, StrL("7")));
        }
        utassert(r.Ok());
        utassert(n == 1);
    }

    // multi-element array path indices
    {
        int n = 0;
        json::Reader r(StrL("[10,20,30]"));
        for (json::Value& v : r) {
            utassert(v.type == json::Type::Number);
            utassert(v.path && !v.path->next);
            utassert(json::PathSegIndex(v.path) == n);
            utassert(str::Eq(json::PathFormatTemp(v.path), fmt("[%d]", n)));
            utassert(str::Eq(v.value, fmt("%d", (n + 1) * 10)));
            n++;
        }
        utassert(r.Ok());
        utassert(n == 3);
    }

    {
        auto onValue = [](void*, json::Value& v) {
            TempStr path = json::PathFormatTemp(v.path, true);
            printf("key: %s, value: %.*s\n", path.s, v.value.len, v.value.s);
        };
        json::Reader r(demo);
        for (json::Value& v : r) {
            onValue(nullptr, v);
        }
        // json::Parse(demo, MkFunc1<void, json::Value*>(onValue, nullptr));
    }
}

using namespace jsonrefl;

void TestJsonDeserializer() {
    printf("测试给出结构体信息，把json反序列化为结构体\n-------------------------\n");
    struct ServerConfig {
        Str host;
        int port;
        bool useTls;
    };

    // gReducedLogging = false;
    // gLogToDebugger = true;
    // gLogToConsole = true;
    // 2. 定义元数据（与 SettingsUtil 完全相同的格式）
    FieldInfo gServerConfigFields[] = {
        {offsetof(ServerConfig, host), Type::String, (intptr_t)"localhost"},
        {offsetof(ServerConfig, port), Type::Int, 8080},
        {offsetof(ServerConfig, useTls), Type::Bool, false},
    };
    StructInfo gServerConfigInfo = {sizeof(ServerConfig), 3, gServerConfigFields, "host\0port\0useTls"};

    // 3. 反序列化
    const char* json = R"({"host":"192.168.1.1","port":443,"useTls":true})";
    // TempStr hostInfo = fmt("%s", json);
    // auto* cfg = (ServerConfig*)JsonDeserializeStruct(&gServerConfigInfo, hostInfo);
    auto* cfg = (ServerConfig*)jsonrefl::Deserialize(&gServerConfigInfo, Str(json));
    // cfg->host == "192.168.1.1", cfg->port == 443, cfg->useTls == true
    printf("cfg->host == \"%s\", cfg->port == %d, cfg->useTls == %d\n", cfg->host.s, cfg->port, cfg->useTls);

    // struct IpInfo {
    //     char* info;
    //     StrVecWithData<ServerConfig> ipPool;
    // };

    // 4. 释放（复用 SettingsUtil 的 FreeStruct）
    Free(&gServerConfigInfo, cfg);

    // 下面是错误用法，FreeStruct只是用free标记内存为可重用状态，在生效后，cfg原来指向的内存可能没有清零，另外cfg也仍然指向原来的内存地址，
    // if (!cfg) {
    //     printf("cfg free successful!");
    // } else if (cfg->host.s) { //错误用法
    //     printf("cfg after free: %s\n", cfg->host.s);
    // }
}

struct FileInfo {
    Str id; // str::Dup 分配，需手动 str::Free
    Str fileName;
    int size;
    Str fileType;
    Str sha256;  // 可为 nullptr（ 	test_util.exe!Str::operator bool() 行 242	C++ JSON null）
    Str preview; // 可为 nullptr
};

struct FileCollection {
    Str sessionId;
    MapRaw files;
};

static const FieldInfo gFileInfoFields[] = {
    {offsetof(struct FileInfo, id), Type::String, 0},     {offsetof(struct FileInfo, fileName), Type::String, 0},
    {offsetof(struct FileInfo, size), Type::Int, 0},      {offsetof(struct FileInfo, fileType), Type::String, 0},
    {offsetof(struct FileInfo, sha256), Type::String, 0}, {offsetof(struct FileInfo, preview), Type::String, 0},
};

static const StructInfo gFileInfoInfo = {sizeof(FileInfo), dimof(gFileInfoFields), gFileInfoFields,
                                         "id\0fileName\0size\0fileType\0sha256\0preview\0"};

static const FieldInfo gFileCollectionFields[] = {
    {offsetof(FileCollection, sessionId), Type::String, 0},
    {offsetof(FileCollection, files), Type::MapStruct, (intptr_t)&gFileInfoInfo},
};

static const StructInfo gFileCollectionInfo = {sizeof(FileCollection), dimof(gFileCollectionFields),
                                               gFileCollectionFields, "sessionId\0files\0"};

void TestJsonMap() {
    printf("测试把json 反序列化为StrVecWithData<FileInfo>结构体\n-------------------------\n");

    const char* jsonData = R"({"sessionId": "abc-123-cde",
    "files": {
        "文件ID1": {
            "id": "文件ID1",
            "fileName": "images/sample-0.jpg",
            "size": 322817,
            "fileType": "image/jpeg",
            "sha256": null,
            "preview": null
        },
        "文件ID2": {
            "id": "文件ID2",
            "fileName": "images/sample-1.jpg",
            "size": 839937,
            "fileType": "image/jpeg",
            "sha256": null,
            "preview": null
        }
    }})";

    FileCollection* data = (FileCollection*)Deserialize(&gFileCollectionInfo, Str(jsonData));

    // gReducedLogging = true;
    // gLogToDebugger = true;
    // gLogToConsole = true;
    // 遍历结果
    printf("sessionId: %s\n", data->sessionId.s);
    for (auto [k, v] : data->files.keys) {
        Str fileId = k;                                  // 键：动态文件ID
        FileInfo* fi = (FileInfo*)data->files.values[v]; // 值：FileInfo 结构体
        // logf("upload info %s: id=%s, fileName=%s, size=%lld\n", fileId, fi->id, fi->fileName, fi->size);
        printf("upload info %s: id=%s, fileName=%s, size=%d\n", fileId.s, fi->id.s, fi->fileName.s, fi->size);
    }
    // gLogToConsole = false;

    Free(&gFileCollectionInfo, data);
}

struct Info {
    Str name;
    int version = 0;
};

struct HostConfig {
    int port = 0;
    bool useTls = false;
};

struct IpInfo {
    Info info;
    MapRaw ipPool;
};

// --- info 的元数据 ---
static const FieldInfo gInfoFields[] = {
    {offsetof(struct Info, name), Type::String, 0},
    {offsetof(struct Info, version), Type::Int, 0},
};
static const StructInfo gInfoInfo = {sizeof(struct Info), 2, gInfoFields, "name\0version\0"};

static const FieldInfo gHostConfigFields[] = {
    {offsetof(HostConfig, port), Type::Int, 0},
    {offsetof(HostConfig, useTls), Type::Bool, 0},
};
static const StructInfo gHostConfigInfo = {sizeof(HostConfig), 2, gHostConfigFields, "port\0useTls\0"};

static const FieldInfo gIpInfoFields[] = {
    {offsetof(IpInfo, info), Type::Struct, (intptr_t)&gInfoInfo},
    {offsetof(IpInfo, ipPool), Type::MapStruct, (intptr_t)&gHostConfigInfo},
};

static const StructInfo gIpInfoInfo = {sizeof(struct IpInfo), 2, gIpInfoFields, "info\0ipPool\0"};

struct Wife {
    Str name;
    int age;
    MapStructArrayRaw info;
};

struct Human {
    Str name;
    int age;
    Str sex;
};

struct Persion {
    Str name;
    Vec<int>* ages;
    Vec<Wife*>* wifes;
    MapArrayRaw house;
};

static const FieldInfo gHumanFields[] = {
    {offsetof(Human, name), Type::String, 0},
    {offsetof(Human, age), Type::Int, 0},
    {offsetof(Human, sex), Type::String, (intptr_t)"Male"},
};

static const StructInfo gHumanInfo = {sizeof(Human), dimof(gHumanFields), gHumanFields, "name\0age\0sex\0"};

static const FieldInfo gWifeFields[] = {
    {offsetof(Wife, name), Type::String, 0},
    {offsetof(Wife, age), Type::Int, 0},
    {offsetof(Wife, info), Type::MapStructArray, (intptr_t)&gHumanInfo},
};

static const StructInfo gWifeInfo = {sizeof(Wife), dimof(gWifeFields), gWifeFields, "name\0age\0info\0"};

static const FieldInfo gPersionField[] = {
    {offsetof(Persion, name), Type::String, 0},
    {offsetof(Persion, ages), Type::IntArray, 0},
    {offsetof(Persion, wifes), Type::StructArray, (intptr_t)&gWifeInfo},
    {offsetof(Persion, house), Type::MapStringArray, 0},
};

static const StructInfo gPersionInfo = {sizeof(Persion), dimof(gPersionField), gPersionField,
                                        "name\0ages\0wifes\0house\0"};

void TestNestMap() {
    printf("测试嵌套Map\n--------------------------\n");
    Persion* p = (Persion*)Deserialize(&gPersionInfo, demo);
    auto s = p->house.values;
    for (auto [k, v] : p->house.keys) {
        auto vec = (Vec<Str>*)s[v];
        for (int i = 0; i < len(*vec); i++) printf("House->%s: %s\n", k.s, ((*vec)[i]).s);
    }

    auto a = p->ages;
    printf("Ages: ");
    for (int i = 0; i < len(*a); i++) printf("%d ", (*a)[i]);
    printf("\n");
    auto w = p->wifes;
    for (int i = 0; i < len(*w); i++) {
        auto ws = (*w)[i];
        printf("wife: %s, age: %d, ", ws->name.s, ws->age);
        auto info = ws->info.values;
        printf("info: \n");
        for (auto [k, v] : ws->info.keys) {
            printf("\t%s: \n", k.s);
            auto infoValue = (Vec<Human*>*)ws->info.values[v];
            for (int j = 0; j < len(*infoValue); j++) {
                auto h = (*infoValue)[j];
                printf("\t\tname: %s, age: %d, sex: %s\n", h->name.s, h->age, h->sex.s);
            }
        }
    }

    Free(&gPersionInfo, p);
}
void TestIpInfo() {
    TestJsonDeserializer();
    TestNestMap();

    printf("测试字典迭代\n--------------------------\n");
    dict::MapStrToInt m;
    m.Insert(StrL("foo"), 1);
    m.Insert(StrL("bar"), 2);
    m.Insert(StrL("haha"), 3);

    for (auto [key, val] : m) {
        printf("%s = %d\n", key.s, val);
        if (str::Eq(key, StrL("haha"))) {
            break; // early-break
        }
    }

    printf("测试使用反序列化(ip)\n--------------------------\n");
    const char* ipinfo = R"(
        {
          "info": { "name": "cluster-1", "version": 2 },
          "ipPool": {
            "192.168.1.1": { "port": 8080, "useTls": true},
            "192.168.1.2": { "port": 8081, "useTls": false}
          }
        }
    )";

    IpInfo* out = (IpInfo*)Deserialize(&gIpInfoInfo, Str(ipinfo));

    printf("ip info: name=%s, version=%d\n", out->info.name.s, out->info.version);
    // 采用平行数组，遍历效率更高（在N<1000时）
    // for (int i = 0; i < len(out->ipPool.keys); i++) {
    //     Str ip = out->ipPool.keys[i];
    //     HostConfig* serverInfo = (HostConfig*)out->ipPool.values[i];
    //     printf("server Info: ip=%s, port=%d, useTls=%s\n", ip.s, serverInfo->port,
    //            serverInfo->useTls ? "true" : "false");
    // }
    for (auto [k, v] : out->ipPool.keys) {
        Str ip = k;
        HostConfig* serverInfo = (HostConfig*)out->ipPool.values[v];
        printf("server Info: ip=%s, port=%d, useTls=%s\n", ip.s, serverInfo->port,
               serverInfo->useTls ? "true" : "false");
    }

    Free(&gIpInfoInfo, out);

    TestJsonMap();
}
