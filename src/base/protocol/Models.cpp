#include "base/Base.h"
#include "base/protocol/Models.h"
#include "base/JsonParser.h"

namespace LocalSend {

#if 0
JsonValue
DeviceInfo::ToJson() const
{
	JsonValue v = JsonValue::Object();
	v["alias"] = alias;
	v["version"] = version;
	v["deviceModel"] = deviceModel;
	v["deviceType"] = deviceType;
	v["fingerprint"] = fingerprint;
	v["port"] = port;
	v["protocol"] = protocol;
	v["download"] = download;
	return v;
}


DeviceInfo
DeviceInfo::FromJson(const JsonValue& v)
{
	DeviceInfo d;
	if (v.Has("alias"))
		d.alias = v.At("alias").AsString(d.alias);
	if (v.Has("version"))
		d.version = v.At("version").AsString(d.version);
	if (v.Has("deviceModel"))
		d.deviceModel = v.At("deviceModel").AsString(d.deviceModel);
	if (v.Has("deviceType"))
		d.deviceType = v.At("deviceType").AsString(d.deviceType);
	if (v.Has("fingerprint"))
		d.fingerprint = v.At("fingerprint").AsString();
	if (v.Has("port"))
		d.port = static_cast<int>(v.At("port").AsInt(d.port));
	if (v.Has("protocol"))
		d.protocol = v.At("protocol").AsString(d.protocol);
	if (v.Has("download"))
		d.download = v.At("download").AsBool(d.download);
	return d;
}


JsonValue
FileMetadata::ToJson() const
{
	JsonValue v = JsonValue::Object();
	v["id"] = id;
	v["fileName"] = fileName;
	v["size"] = size;
	v["fileType"] = fileType;
	v["sha256"] = sha256.empty() ? JsonValue(nullptr) : JsonValue(sha256);
	v["preview"] = preview.empty() ? JsonValue(nullptr) : JsonValue(preview);

	if (!modified.empty() || !accessed.empty()) {
		JsonValue meta = JsonValue::Object();
		if (!modified.empty())
			meta["modified"] = modified;
		if (!accessed.empty())
			meta["accessed"] = accessed;
		v["metadata"] = meta;
	}
	return v;
}


FileMetadata
FileMetadata::FromJson(const JsonValue& v)
{
	FileMetadata f;
	if (v.Has("id"))
		f.id = v.At("id").AsString();
	if (v.Has("fileName"))
		f.fileName = v.At("fileName").AsString();
	if (v.Has("size"))
		f.size = v.At("size").AsInt();
	if (v.Has("fileType"))
		f.fileType = v.At("fileType").AsString(f.fileType);
	if (v.Has("sha256") && v.At("sha256").IsString())
		f.sha256 = v.At("sha256").AsString();
	if (v.Has("preview") && v.At("preview").IsString())
		f.preview = v.At("preview").AsString();
	if (v.Has("metadata")) {
		const JsonValue& m = v.At("metadata");
		if (m.Has("modified"))
			f.modified = m.At("modified").AsString();
		if (m.Has("accessed"))
			f.accessed = m.At("accessed").AsString();
	}
	return f;
}

#endif

// 将 char* 以 JSON 字符串形式写入 str::Builder（含两端引号）
// null 值写为 JSON null（不加引号）
static void JsonAppendStr(str::Builder& out, Str s) {
    if (len(s) == 0) {
        out.Append(StrL("null"));
        return;
    }
    out.AppendChar('"');
    for (const char* c = s.s; *c; c++) {
        switch (*c) {
            case '"':
                out.Append(StrL("\\\""));
                break;
            case '\\':
                out.Append(StrL("\\\\"));
                break;
            case '\n':
                out.Append(StrL("\\n"));
                break;
            case '\r':
                out.Append(StrL("\\r"));
                break;
            case '\t':
                out.Append(StrL("\\t"));
                break;
            default:
                // 控制字符用 \uXXXX 转义
                if ((unsigned char)*c < 0x20) {
                    out.Append(fmt("\\u%04x", (unsigned char)*c));
                } else {
                    out.AppendChar(*c);
                }
                break;
        }
    }
    out.AppendChar('"');
}

// 序列化 struct info* 为 JSON 对象片段（不含外层花括号）
// static_assert(sizeof(LocalSend::DeviceInfo) > 0, "DeviceInfo not visible here");
static void JsonAppendDeviceInfoFields(str::Builder& out, const DeviceInfo* inf) {
    // 统一提取字段，inf 为空时全部回退到默认值
    Str emptyStr = Str{};
    const Str alias       = inf ? inf->alias       : emptyStr;
    const Str version     = inf ? inf->version     : emptyStr;
    const Str deviceModel = inf ? inf->deviceModel : emptyStr;
    const Str deviceType  = inf ? inf->deviceType  : emptyStr;
    const Str fingerprint = inf ? inf->fingerprint : emptyStr;
    const int  port       = inf ? inf->port        : 0;
    const Str protocol    = inf ? inf->protocol    : emptyStr;
    const bool download   = inf && inf->download;

    out.Append(StrL("\"alias\":"));
    JsonAppendStr(out, alias);
    out.Append(StrL(",\"version\":"));
    JsonAppendStr(out, version);
    out.Append(StrL(",\"deviceModel\":"));
    JsonAppendStr(out, deviceModel);
    out.Append(StrL(",\"deviceType\":"));
    JsonAppendStr(out, deviceType);
    out.Append(StrL(",\"fingerprint\":"));
    JsonAppendStr(out, fingerprint);
    out.Append(str::Format(out.a, ",\"port\":%d", port));
    out.Append(StrL(",\"protocol\":"));
    JsonAppendStr(out, protocol);
    out.Append(fmt(",\"download\":%s", download ? StrL("true") : StrL("false")));
}

// 序列化 ServerConfig 为 JSON 对象片段（不含外层花括号）
static void JsonAppendFileMetadataFields(str::Builder& out, const FileMetadata* cfg) {
    // out.AppendFmt("\"port\":%d,", cfg->port);
    // out.AppendFmt("\"useTls\":%s", cfg->useTls ? "true" : "false");
    out.Append(StrL("\"id\":"));
    JsonAppendStr(out, cfg ? cfg->id : Str{});
    out.Append(StrL(",\"fileName\":"));
    TempStr unixPath = str::DupTemp(cfg ? cfg->fileName : Str{});
    str::TransCharsInPlace(unixPath, StrL("\\"), StrL("/"));
    JsonAppendStr(out, unixPath);
    out.Append(fmt(",\"size\":%lld", cfg->size));
    out.Append(StrL(",\"fileType\":"));
    JsonAppendStr(out, cfg ? cfg->fileType : Str{});
    out.Append(StrL(",\"sha256\":"));
    JsonAppendStr(out, cfg ? cfg->sha256 : Str{});
    out.Append(StrL(",\"preview\":"));
    JsonAppendStr(out, cfg ? cfg->preview : Str{});
    out.Append(StrL(",\"modified\":"));
    JsonAppendStr(out, cfg ? cfg->modified : Str{});
    out.Append(StrL(",\"accessed\":"));
    JsonAppendStr(out, cfg ? cfg->accessed : Str{});
    out.Append(StrL(",\"localPath\":"));
    JsonAppendStr(out, cfg ? cfg->localPath : Str{});
}

// 序列化整个 IpInfo 为 JSON 字符串
// 返回堆分配的 char*，调用方负责 free
TempStr UploadInfoToJson(const UploadInfo* up) {
    str::Builder out;

    out.Append(StrL("{\n"));

    // --- "info" 字段 ---
    out.Append(StrL("  \"info\":"));
    if (!up->info) {
        out.Append(StrL("null"));
    } else {
        out.Append(StrL("{\n"));
        out.Append(StrL("    "));
        JsonAppendDeviceInfoFields(out, up->info);
        out.Append(StrL("\n  }"));
    }
    out.Append(StrL(",\n"));

    // --- "ipPool" 字段（动态键对象）---
    out.Append(StrL("  \"files\":{\n"));
    int n = len(*up->files);
    for (int i = 0; i < n; i++) {
        Str key = up->files->At(i);
        const FileMetadata* cfg = *up->files->AtData(i);

        out.Append(StrL("    "));
        JsonAppendStr(out, key); // 动态键
        out.Append(StrL(":{\n"));
        out.Append(StrL("      "));
        JsonAppendFileMetadataFields(out, cfg);
        out.Append(StrL("\n    }"));
        if (i < n - 1) {
            out.AppendChar(',');
        }
        out.AppendChar('\n');
    }
    out.Append(StrL("  }\n"));

    out.Append(StrL("}"));

    return ToStrTemp(out); // 调用方负责 free
}

TempStr BuildPrepareUpload(const DeviceInfo* info, const StrVecWithData<FileMetadata*>* files) {
    UploadInfo upi;
    upi.info = info;
    upi.files = files;

    return UploadInfoToJson(&upi);
}

#if 0
// 根据 StructInfo 中的字段名找到 FieldInfo，写入值
static void WriteFieldByName(const StructInfo* si, u8* base,
                             const char* fieldName,
                             const char* value, json::Type type) {
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
#endif

static void PrepareUploadInfoDeserializer(PrepareUploadResult* pt, json::Value& v) {

        // --- /info/<fieldName> ---
        // Str kSessionPrefix = StrL("/sessionId");
        if (json::Type::String == v.type) {
            return;
        }
        if (json::PathMatch(v.path, StrL("/sessionId"))) {
            // if (!pt->sessionId) {
            //     pt->sessionId = str::Dup(value);
            //     return true;
            // }
            // 释放旧值再复制，重复出现也不会泄漏
            str::ReplaceWithCopy(&pt->sessionId, v.value);
            logf("/sessionId: %s OK!\n", pt->sessionId);
            return;
        }

        // --- /ipPool/<ip>/<fieldName> ---
        // Str kFilesPrefix = StrL("/files");
        if (json::PathMatch(v.path, StrL("/files"), StrL("*"))) {
            // const char* afterPrefix = v->path->s.s + len(kFilesPrefix);
            // 找到 IP 键结束位置（第一个 '/'）
            // const char* slash = str::FindChar(afterPrefix, '/');
            // if (!slash) {
            //     return true; // 路径格式不对，跳过
            // }
            // 提取 IP 键（临时字符串）
            StrNode* seg1 = json::PathNth(v.path, 1);
            logf("/files:%s: OK!\n", seg1->s);
            Str fileId = json::PathSegKey(seg1);
            logf("/files/%s: OK!\n", fileId);
            // const char* fieldName = slash + 1;

            // 查找或创建 ServerConfig 条目
            int idx = pt->fileTokens.Find(fileId, 0);
            if (idx < 0) {
                Str st = str::Dup(v.value);
                pt->fileTokens.Append(fileId, st);
                idx = len(pt->fileTokens) - 1;
                logf("Not found fileId: %s, add Value: %s\n", fileId, st);
                return;
            }
            Str* st =pt->fileTokens.AtData(idx);
            str::ReplaceWithCopy(st, v.value);
            logf("Found fileId: %s, replace Value: %s\n", fileId, *st);
            // WriteFieldByName(&gServerConfigInfo, (u8*)sc, fieldName, value, type);
        }
};

// 返回 true 表示 JSON 格式有效
// bool DeserializeIpInfo(const char* jsonData, PrepareUploadResult* out) {
// PrepareUploadInfoDeserializer visitor(out);
// return json::Parse(jsonData, &visitor);
// }

// 释放 PrepareUploadResult 内部资源
void FreePpInfo(PrepareUploadResult* ip) {
    if (ip->sessionId) {
        str::Free(ip->sessionId);
        ip->sessionId = Str{};
    }
    // 释放 ipPool 中每个 ServerConfig 的 char* 字段
    // （ServerConfig 当前只有 int 字段，无需额外释放）
    ip->fileTokens.Reset(); // 清空 StrVecWithData
}

bool ParsePrepareUploadResponse(Str body, PrepareUploadResult* out) {
    json::Reader r(body);
    for (json::Value& v : r) {
        PrepareUploadInfoDeserializer(out, v);
    }
    return r.Ok();
}

#if 0
// For server
IncomingPrepareUpload
ParsePrepareUploadRequest(const std::string& body)
{
	IncomingPrepareUpload req;
	JsonValue v = JsonValue::Parse(body);
	if (v.Has("info"))
		req.sender = DeviceInfo::FromJson(v.At("info"));
	if (v.Has("files")) {
		const JsonValue& files = v.At("files");
		for (const auto& kv : files.Items()) {
			FileMetadata f = FileMetadata::FromJson(kv.second);
			// La chiave dell'oggetto e' la fonte autorevole del fileId: usala
			// anche se "id" interno manca o diverge.
			if (f.id.empty())
				f.id = kv.first;
			req.files.push_back(f);
		}
	}
	return req;
}


JsonValue
BuildPrepareUploadResponse(const std::string& sessionId,
	const std::map<std::string, std::string>& fileTokens)
{
	JsonValue root = JsonValue::Object();
	root["sessionId"] = sessionId;
	JsonValue files = JsonValue::Object();
	for (const auto& kv : fileTokens)
		files[kv.first] = kv.second;
	root["files"] = files;
	return root;
}
#endif

} // namespace LocalSend
