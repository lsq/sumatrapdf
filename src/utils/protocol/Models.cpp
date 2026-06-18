#include "utils/protocol/Models.h"
#include "utils/BaseUtil.h"
#include "utils/JsonParser.h"

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
            case '"':  out.Append("\\\""); break;
            case '\\': out.Append("\\\\"); break;
            case '\n': out.Append("\\n");  break;
            case '\r': out.Append("\\r");  break;
            case '\t': out.Append("\\t");  break;
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
static void JsonAppendDeviceInfoFields(StrBuilder& out, const struct DeviceInfo* inf) {
    out.Append("\"alias\":");
    JsonAppendStr(out, inf ? inf->alias: nullptr);
    // 若有更多字段，继续追加：
    // out.Append(",\"otherField\":");
    // JsonAppendStr(out, inf ? inf->otherField : nullptr);
    // out.AppendFmt(", \"version\":%d", inf->version);
    out.Append(",\"version\":");
    JsonAppendStr(out, inf ? inf->version: nullptr);
    out.Append(",\"deviceModel\":");
    JsonAppendStr(out, inf ? inf->deviceModel: nullptr);
    out.Append(",\"deviceType\":");
    JsonAppendStr(out, inf ? inf->deviceType: nullptr);
    out.Append(",\"fingerprint\":");
    JsonAppendStr(out, inf ? inf->fingerprint: nullptr);
    out.AppendFmt(",\"port\":%d", inf->port);
    out.Append(",\"protocol\":");
    JsonAppendStr(out, inf ? inf->protocol: nullptr);
    out.AppendFmt(",\"download\":%s", inf ? (inf->download ? "true" : "false") : "false");
}

// 序列化 ServerConfig 为 JSON 对象片段（不含外层花括号）
static void JsonAppendFileMetadataFields(StrBuilder& out, const FileMetadata* cfg) {
    // out.AppendFmt("\"port\":%d,", cfg->port);
    // out.AppendFmt("\"useTls\":%s", cfg->useTls ? "true" : "false");
    out.Append("\"id\":");
    JsonAppendStr(out, cfg ? cfg->id: nullptr);
    out.Append(",\"fileName\":");
    JsonAppendStr(out, cfg ? cfg->fileName: nullptr);
    out.AppendFmt(",\"size\":%lld", cfg->size);
    out.Append(",\"fileType\":");
    JsonAppendStr(out, cfg ? cfg->fileType: nullptr);
    out.Append(",\"sha256\":");
    JsonAppendStr(out, cfg ? cfg->sha256: nullptr);
    out.Append(",\"preview\":");
    JsonAppendStr(out, cfg ? cfg->preview: nullptr);
    out.Append(",\"modified\":");
    JsonAppendStr(out, cfg ? cfg->modified: nullptr);
    out.Append(",\"accessed\":");
    JsonAppendStr(out, cfg ? cfg->accessed: nullptr);
    out.Append(",\"localPath\":");
    JsonAppendStr(out, cfg ? cfg->localPath: nullptr);
}

// 序列化整个 IpInfo 为 JSON 字符串
// 返回堆分配的 char*，调用方负责 free
TempStr UploadInfoToJson(const UploadInfo* up) {
    StrBuilder out;

    out.Append("{\n");

    // --- "info" 字段 ---
    out.Append("  \"info\":");
    if (!up->info) {
        out.Append("null");
    } else {
        out.Append("{\n");
        out.Append("    ");
        JsonAppendDeviceInfoFields(out, up->info);
        out.Append("\n  }");
    }
    out.Append(",\n");

    // --- "ipPool" 字段（动态键对象）---
    out.Append("  \"files\":{\n");
    int n = up->files->Size();
    for (int i = 0; i < n; i++) {
        const char*         key = up->files->At(i);
        const FileMetadata* cfg = *up->files->AtData(i);

        out.Append("    ");
        JsonAppendStr(out, key);   // 动态键
        out.Append(":{\n");
        out.Append("      ");
        JsonAppendFileMetadataFields(out, cfg);
        out.Append("\n    }");
        if (i < n - 1) {
            out.AppendChar(',');
        }
        out.AppendChar('\n');
    }
    out.Append("  }\n");

    out.Append("}");

    return out.StealData(GetTempAllocator());  // 调用方负责 free
}


TempStr BuildPrepareUpload(const DeviceInfo* info,
	const StrVecWithData<FileMetadata*>* files)
{
    UploadInfo upi;
    upi.info = info;
    upi.files = files;

    TempStr preUploadInfos = UploadInfoToJson(&upi);

    return preUploadInfos;
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

struct PrepareUploadInfoDeserializer : json::ValueVisitor {
    PrepareUploadResult* out;

    explicit PrepareUploadInfoDeserializer(PrepareUploadResult* o) : out(o) {}

    bool Visit(const char* path, const char* value, json::Type type) override {
        // --- /info/<fieldName> ---
        const char* kSessionPrefix = "/sessionId";
        if (str::StartsWith(path, kSessionPrefix)) {
            if (!out->sessionId) {
                out->sessionId =  str::Dup(value);
                return true;
            }
            out->sessionId =  str::Dup(value);
            return true;
        }

        // --- /ipPool/<ip>/<fieldName> ---
        const char* kFilesPrefix = "/files/";
        if (str::StartsWith(path, kFilesPrefix)) {
            const char* afterPrefix = path + str::Len(kFilesPrefix);
            // 找到 IP 键结束位置（第一个 '/'）
            // const char* slash = str::FindChar(afterPrefix, '/');
            // if (!slash) {
            //     return true; // 路径格式不对，跳过
            // }
            // 提取 IP 键（临时字符串）
            char* fileId = str::Dup(afterPrefix);
            // const char* fieldName = slash + 1;

            // 查找或创建 ServerConfig 条目
            int idx = out->fileTokens.Find(StrSpan(fileId));
            if (idx < 0) {
                char* st = str::Dup(value);
                out->fileTokens.Append(fileId, st);
                idx = out->fileTokens.Size() - 1;
                return true;
            }
            char** st = out->fileTokens.AtData(idx);
            str::ReplaceWithCopy(st, value);
            // WriteFieldByName(&gServerConfigInfo, (u8*)sc, fieldName, value, type);
            return true;
        }

        return true;
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
        ip->sessionId = nullptr;
    }
    // 释放 ipPool 中每个 ServerConfig 的 char* 字段
    // （ServerConfig 当前只有 int 字段，无需额外释放）
    ip->fileTokens.Reset(); // 清空 StrVecWithData
}

bool
ParsePrepareUploadResponse(const char* body, PrepareUploadResult* out)
{
    PrepareUploadInfoDeserializer visitor(out);
    return json::Parse(body, &visitor);
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
