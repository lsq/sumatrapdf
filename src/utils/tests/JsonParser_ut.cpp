/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "utils/BaseUtil.h"
#include "utils/SettingsUtil.h"
#include "utils/JsonParser.h"
#include "utils/JsonUtil.h"
#include "utils/Log.h"

// must be last due to assert() over-write
#include "utils/UtAssert.h"

struct JsonValue {
    const char* path = nullptr;
    const char* value = nullptr;
    json::Type type{json::Type::String};

    JsonValue() = default;
    JsonValue(const char* path, const char* value, json::Type type = json::Type::String)
        : path(path), type(type), value(value) {}
};

class JsonVerifier : public json::ValueVisitor {
    const JsonValue* data;
    size_t dataLen;
    size_t idx;

  public:
    JsonVerifier(const JsonValue* data, size_t dataLen) : data(data), dataLen(dataLen), idx(0) {}
    ~JsonVerifier() { utassert(dataLen == idx); }

    virtual bool Visit(const char* path, const char* value, json::Type type) {
        utassert(idx < dataLen);
        const JsonValue& d = data[idx];
        utassert(type == d.type);
        utassert(str::Eq(path, d.path));
        utassert(str::Eq(value, d.value));

        idx++;
        return true;
    }
};

void JsonTest() {
    static const struct {
        const char* json;
        JsonValue value;
    } validJsonData[] = {
        // strings
        {"\"test\"", JsonValue("", "test")},
        {"\"\\\\\\n\\t\\u01234\"", JsonValue("",
                                             "\\\n\t\xC4\xA3"
                                             "4")},
        // numbers
        {"123", JsonValue("", "123", json::Type::Number)},
        {"-99.99", JsonValue("", "-99.99", json::Type::Number)},
        {"1.2E+15", JsonValue("", "1.2E+15", json::Type::Number)},
        {"0e-7", JsonValue("", "0e-7", json::Type::Number)},
        // keywords
        {"true", JsonValue("", "true", json::Type::Bool)},
        {"false", JsonValue("", "false", json::Type::Bool)},
        {"null", JsonValue("", "null", json::Type::Null)},
        // dictionaries
        {"{\"key\":\"test\"}", JsonValue("/key", "test")},
        {"{ \"no\" : 123 }", JsonValue("/no", "123", json::Type::Number)},
        {"{ \"bool\": true }", JsonValue("/bool", "true", json::Type::Bool)},
        {"{}", JsonValue()},
        // arrays
        {"[\"test\"]", JsonValue("[0]", "test")},
        {"[123]", JsonValue("[0]", "123", json::Type::Number)},
        {"[ null ]", JsonValue("[0]", "null", json::Type::Null)},
        {"[]", JsonValue()},
        // combination
        {"{\"key\":[{\"name\":-987}]}", JsonValue("/key[0]/name", "-987", json::Type::Number)},
    };

    for (size_t i = 0; i < dimof(validJsonData); i++) {
        JsonVerifier verifier(&validJsonData[i].value, validJsonData[i].value.value ? 1 : 0);
        utassert(json::Parse(validJsonData[i].json, &verifier));
    }

    static const struct {
        const char* json;
        JsonValue value;
    } invalidJsonData[] = {
        // dictionaries
        {"{\"key\":\"test\"", JsonValue("/key", "test")},
        {"{ \"no\" : 123, }", JsonValue("/no", "123", json::Type::Number)},
        {"{\"key\":\"test\"]", JsonValue("/key", "test")},
        // arrays
        {"[\"test\"", JsonValue("[0]", "test")},
        {"[123,]", JsonValue("[0]", "123", json::Type::Number)},
        {"[\"test\"}", JsonValue("[0]", "test")},
    };

    for (size_t i = 0; i < dimof(invalidJsonData); i++) {
        JsonVerifier verifier(&invalidJsonData[i].value, 1);
        utassert(!json::Parse(invalidJsonData[i].json, &verifier));
    }

    static const char* invalidJson[] = {
        "",  "string", "nada", "\"open", "\"\\xC4\"",   "\"\\u123h\"",   "'string'",       "01", ".1", "12.", "1e",
        "-", "-01",    "{",    "{,}",    "{\"key\": }", "{\"key: 123 }", "{ 'key': 123 }", "[",  "[,]"};

    JsonVerifier verifyError(nullptr, 0);
    {
        auto s = invalidJson[10]; // this one caused buffer overflow
        utassert(!json::Parse(s, &verifyError));
    }

    for (size_t i = 0; i < dimof(invalidJson); i++) {
        utassert(!json::Parse(invalidJson[i], &verifyError));
    }

    const JsonValue testData[] = {
        JsonValue("/ComicBookInfo/1.0/title", "Meta data demo"),
        JsonValue("/ComicBookInfo/1.0/publicationMonth", "4", json::Type::Number),
        JsonValue("/ComicBookInfo/1.0/publicationYear", "2010", json::Type::Number),
        JsonValue("/ComicBookInfo/1.0/credits[0]/primary", "true", json::Type::Bool),
        JsonValue("/ComicBookInfo/1.0/credits[0]/role", "Writer"),
        JsonValue("/ComicBookInfo/1.0/credits[1]/primary", "false", json::Type::Bool),
        JsonValue("/ComicBookInfo/1.0/credits[1]/role", "Publisher"),
        JsonValue("/ComicBookInfo/1.0/credits[2]", "null", json::Type::Null),
        JsonValue("/appID", "Test/123"),
    };
    const char* jsonSample =
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
}";
    JsonVerifier sampleVerifier(testData, dimof(testData));
    utassert(json::Parse(jsonSample, &sampleVerifier));
}

void TestJsonDeserializer() {
    printf("测试给出结构体信息，把json反序列化为结构体\n-------------------------\n");
    struct ServerConfig {
        char* host;
        int port;
        bool useTls;
    };

    // gReducedLogging = false;
    // gLogToDebugger = true;
    // gLogToConsole = true;
    // 2. 定义元数据（与 SettingsUtil 完全相同的格式）
    FieldInfo gServerConfigFields[] = {
        {offsetof(ServerConfig, host), SettingType::String, (intptr_t)"localhost"},
        {offsetof(ServerConfig, port), SettingType::Int, 8080},
        {offsetof(ServerConfig, useTls), SettingType::Bool, false},
    };
    StructInfo gServerConfigInfo = {sizeof(ServerConfig), 3, gServerConfigFields, "host\0port\0useTls"};

    // 3. 反序列化
    const char* json = R"({"host":"192.168.1.1","port":443,"useTls":true})";
    auto* cfg = (ServerConfig*)JsonDeserializeStruct(&gServerConfigInfo, json);
    // cfg->host == "192.168.1.1", cfg->port == 443, cfg->useTls == true
    printf("cfg->host == \"%s\", cfg->port == %d, cfg->useTls == %d\n", cfg->host, cfg->port, cfg->useTls);

    // struct IpInfo {
    //     char* info;
    //     StrVecWithData<ServerConfig> ipPool;
    // };

    // 4. 释放（复用 SettingsUtil 的 FreeStruct）
    FreeStruct(&gServerConfigInfo, cfg);

    // 下面是错误用法，FreeStruct只是用free标记内存为可重用状态，在生效后，cfg原来指向的内存可能没有清零，另外cfg也仍然指向原来的内存地址，
    if (!cfg) {
        printf("cfg free successful!");
    } else if (cfg->host) {
        printf("cfg after free: %s\n", cfg->host);
    }
}

struct FileInfo {
    char* id; // str::Dup 分配，需手动 str::Free
    char* fileName;
    i64 size;
    char* fileType;
    char* sha256;  // 可为 nullptr（JSON null）
    char* preview; // 可为 nullptr
};

using FilesMap = StrVecWithData<FileInfo>;

struct FilesVisitor : json::ValueVisitor {
    FilesMap& files;

    explicit FilesVisitor(FilesMap& f) : files(f) {}

    bool Visit(const char* path, const char* value, json::Type type) override {
        // 路径格式：/files/f1/id, /files/f1/fileName, ...
        const char* prefix = "/files/";
        if (!str::StartsWith(path, prefix)) {
            return true;
        }
        const char* afterPrefix = path + str::Len(prefix);
        const char* slash = str::FindChar(afterPrefix, '/');
        if (!slash) {
            return true; // 没有字段名，跳过
        }

        // 提取动态键（文件ID）
        TempStr fileId = str::DupTemp(afterPrefix, slash - afterPrefix);
        const char* fieldName = slash + 1;

        // 查找或创建 FileInfo 条目
        int idx = files.Find(fileId);
        if (idx < 0) {
            FileInfo fi{};
            idx = files.Append(fileId, fi);
        }
        FileInfo* fi = files.AtData(idx);

        // 填充字段（null 值时 value 为 "null"，需判断 type）
        const char* v = (type == json::Type::Null) ? nullptr : value;
        if (str::Eq(fieldName, "id")) {
            str::ReplaceWithCopy(&fi->id, v);
        } else if (str::Eq(fieldName, "fileName")) {
            str::ReplaceWithCopy(&fi->fileName, v);
        } else if (str::Eq(fieldName, "size")) {
            fi->size = v ? _atoi64(v) : 0;
        } else if (str::Eq(fieldName, "fileType")) {
            str::ReplaceWithCopy(&fi->fileType, v);
        } else if (str::Eq(fieldName, "sha256")) {
            str::ReplaceWithCopy(&fi->sha256, v);
        } else if (str::Eq(fieldName, "preview")) {
            str::ReplaceWithCopy(&fi->preview, v);
        }
        return true;
    }
};

void FreeFilesMap(FilesMap& files) {
    for (int i = 0; i < files.Size(); i++) {
        FileInfo* fi = files.AtData(i);
        str::Free(fi->id);
        str::Free(fi->fileName);
        str::Free(fi->fileType);
        str::Free(fi->sha256);
        str::Free(fi->preview);
    }
    // files 本身析构时会释放字符串键（文件ID）
}

void TestJsonMap() {
    printf("测试把json 反序列化为StrVecWithData<FileInfo>结构体\n-------------------------\n");
    FilesMap files;
    FilesVisitor visitor(files);

    const char* jsonData = R"({"files": {
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

    bool ok = json::Parse(jsonData, &visitor);
    utassert(ok);

    // gReducedLogging = true;
    // gLogToDebugger = true;
    // gLogToConsole = true;
    // 遍历结果
    for (int i = 0; i < files.Size(); i++) {
        const char* fileId = files.At(i); // 键：动态文件ID
        FileInfo* fi = files.AtData(i);   // 值：FileInfo 结构体
        // logf("upload info %s: id=%s, fileName=%s, size=%lld\n", fileId, fi->id, fi->fileName, fi->size);
        printf("upload info %s: id=%s, fileName=%s, size=%lld\n", fileId, fi->id, fi->fileName, fi->size);
    }
    // gLogToConsole = false;

    FreeFilesMap(files);
}

struct Info {
    char* name = nullptr;
    int version = 0;
};

struct ServerConfig {
    int port = 0;
    bool useTls = false;
};

struct IpInfo {
    struct Info* info = nullptr;
    StrVecWithData<ServerConfig> ipPool;
};

// --- info 的元数据 ---
static const FieldInfo gInfoFields[] = {
    {offsetof(struct Info, name), SettingType::String, 0},
    {offsetof(struct Info, version), SettingType::Int, 0},
};
static const StructInfo gInfoInfo = {sizeof(struct Info), 2, gInfoFields, "name\0version\0"};

// --- ServerConfig 的元数据 ---
static const FieldInfo gServerConfigFields[] = {
    {offsetof(ServerConfig, port), SettingType::Int, 0},
    {offsetof(ServerConfig, useTls), SettingType::Bool, 0},
};
static const StructInfo gServerConfigInfo = {sizeof(ServerConfig), 2, gServerConfigFields, "port\0useTls\0"};

// 根据 StructInfo 中的字段名找到 FieldInfo，写入值
static void WriteFieldByName(const StructInfo* si, u8* base, const char* fieldName, const char* value,
                             json::Type type) {
    if (type == json::Type::Null) {
        return; // null 值跳过，保留默认值
    }
    const char* names = si->fieldNames;
    for (int i = 0; i < si->fieldCount; i++, names += str::Len(names) + 1) {
        if (str::Eq(names, fieldName)) {
            // 复用 deserializeField 的逻辑（或内联实现）
            const FieldInfo& f = si->fields[i];
            u8* ptr = base + f.offset;
            switch (f.type) {
                case SettingType::String:
                    str::ReplaceWithCopy((char**)ptr, value);
                    break;
                case SettingType::Int:
                    *(int*)ptr = atoi(value);
                    break;
                case SettingType::Bool:
                    *(bool*)ptr = str::EqI(value, "true") || str::Eq(value, "1");
                    break;
                case SettingType::Float:
                    *(float*)ptr = (float)atof(value);
                    break;
                default:
                    break;
            }
            return;
        }
    }
}

struct IpInfoDeserializer : json::ValueVisitor {
    IpInfo* out;

    explicit IpInfoDeserializer(IpInfo* o) : out(o) {}

    bool Visit(const char* path, const char* value, json::Type type) override {
        // --- /info/<fieldName> ---
        const char* kInfoPrefix = "/info/";
        if (str::StartsWith(path, kInfoPrefix)) {
            if (!out->info) {
                out->info = new struct Info {};
            }
            const char* fieldName = path + str::Len(kInfoPrefix);
            WriteFieldByName(&gInfoInfo, (u8*)out->info, fieldName, value, type);
            return true;
        }

        // --- /ipPool/<ip>/<fieldName> ---
        const char* kPoolPrefix = "/ipPool/";
        if (str::StartsWith(path, kPoolPrefix)) {
            const char* afterPrefix = path + str::Len(kPoolPrefix);
            // 找到 IP 键结束位置（第一个 '/'）
            const char* slash = str::FindChar(afterPrefix, '/');
            if (!slash) {
                return true; // 路径格式不对，跳过
            }
            // 提取 IP 键（临时字符串）
            TempStr ipKey = str::DupTemp(afterPrefix, slash - afterPrefix);
            const char* fieldName = slash + 1;

            // 查找或创建 ServerConfig 条目
            int idx = out->ipPool.Find(ipKey, 0);
            if (idx < 0) {
                ServerConfig sc{};
                out->ipPool.Append(ipKey, sc);
                idx = out->ipPool.Size() - 1;
            }
            ServerConfig* sc = out->ipPool.AtData(idx);
            WriteFieldByName(&gServerConfigInfo, (u8*)sc, fieldName, value, type);
            return true;
        }

        return true;
    }
};

// 返回 true 表示 JSON 格式有效
bool DeserializeIpInfo(const char* jsonData, IpInfo* out) {
    IpInfoDeserializer visitor(out);
    return json::Parse(jsonData, &visitor);
}

// 释放 IpInfo 内部资源
void FreeIpInfo(IpInfo* ip) {
    if (ip->info) {
        str::Free(ip->info->name);
        delete ip->info;
        ip->info = nullptr;
    }
    // 释放 ipPool 中每个 ServerConfig 的 char* 字段
    // （ServerConfig 当前只有 int 字段，无需额外释放）
    ip->ipPool.Reset(); // 清空 StrVecWithData
}

void TestIpInfo() {
    printf("测试使用visitor反序列化\n--------------------------\n");
    const char* ipinfo = R"(
        {
          "info": { "name": "cluster-1", "version": 2 },
          "ipPool": {
            "192.168.1.1": { "port": 8080, "useTls": true},
            "192.168.1.2": { "port": 8081, "useTls": false}
          }
        }
    )";

    IpInfo out;
    bool ok = DeserializeIpInfo(ipinfo, &out);
    utassert(ok);

    printf("ip info: name=%s, useTls=%d\n", out.info->name, out.info->version);
    for (int i = 0; i < out.ipPool.Size(); i++) {
        char* ip = out.ipPool.At(i);
        ServerConfig* serverInfo = out.ipPool.AtData(i);
        printf("server Info: ip=%s, port=%d, useTls=%s\n", ip, serverInfo->port, serverInfo->useTls ? "true" : "false");
    }

    FreeIpInfo(&out);
}

// 将 char* 以 JSON 字符串形式写入 StrBuilder（含两端引号）
// null 值写为 JSON null（不加引号）
static void JsonAppendStr(StrBuilder& out, const char* s) {
    if (!s) {
        out.Append("null");
        return;
    }
    out.AppendChar('"');
    for (const char* c = s; *c; c++) {
        switch (*c) {
            case '"':
                out.Append("\\\"");
                break;
            case '\\':
                out.Append("\\\\");
                break;
            case '\n':
                out.Append("\\n");
                break;
            case '\r':
                out.Append("\\r");
                break;
            case '\t':
                out.Append("\\t");
                break;
            default:
                // 控制字符用 \uXXXX 转义
                if ((unsigned char)*c < 0x20) {
                    out.AppendFmt("\\u%04x", (unsigned char)*c);
                } else {
                    out.AppendChar(*c);
                }
                break;
        }
    }
    out.AppendChar('"');
}

// 序列化 struct info* 为 JSON 对象片段（不含外层花括号）
static void JsonAppendInfoFields(StrBuilder& out, const struct Info* inf) {
    out.Append("\"name\":");
    JsonAppendStr(out, inf ? inf->name : nullptr);
    // 若有更多字段，继续追加：
    // out.Append(",\"otherField\":");
    // JsonAppendStr(out, inf ? inf->otherField : nullptr);
    out.AppendFmt(", \"version\":%d", inf->version);
}

// 序列化 ServerConfig 为 JSON 对象片段（不含外层花括号）
static void JsonAppendServerConfigFields(StrBuilder& out, const ServerConfig* cfg) {
    out.AppendFmt("\"port\":%d,", cfg->port);
    out.AppendFmt("\"useTls\":%s", cfg->useTls ? "true" : "false");
}

// 序列化整个 IpInfo 为 JSON 字符串
// 返回堆分配的 char*，调用方负责 free
char* IpInfoToJson(const IpInfo* ip) {
    StrBuilder out;

    out.Append("{\n");

    // --- "info" 字段 ---
    out.Append("  \"info\":");
    if (!ip->info) {
        out.Append("null");
    } else {
        out.Append("{\n");
        out.Append("    ");
        JsonAppendInfoFields(out, ip->info);
        out.Append("\n  }");
    }
    out.Append(",\n");

    // --- "ipPool" 字段（动态键对象）---
    out.Append("  \"ipPool\":{\n");
    int n = ip->ipPool.Size();
    for (int i = 0; i < n; i++) {
        const char* key = ip->ipPool.At(i);
        const ServerConfig* cfg = ip->ipPool.AtData(i);

        out.Append("    ");
        JsonAppendStr(out, key); // 动态键
        out.Append(":{\n");
        out.Append("      ");
        JsonAppendServerConfigFields(out, cfg);
        out.Append("\n    }");
        if (i < n - 1) {
            out.AppendChar(',');
        }
        out.AppendChar('\n');
    }
    out.Append("  }\n");

    out.Append("}");

    return out.StealData(); // 调用方负责 free
}

void TestJsonSerializer() {
    printf("测试使用StrVecWithData序列化\n--------------------------\n");
    IpInfo ipinfo;

    Info inf{str::Dup("lsq"), 3};

    ServerConfig sc = {1234, true};

    // StrVecWithData<ServerConfig> sr;
    // sr.Append("192.168.124.1", sc);

    ipinfo.info = &inf;
    ipinfo.ipPool.Append("192.168.124.1", sc);

    char* json = IpInfoToJson(&ipinfo);
    printf("ipinfo: \n%s\n", json);
    free(inf.name);
    free(json);
}
