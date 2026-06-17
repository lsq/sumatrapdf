#include "utils/BaseUtil.h"
#include "utils/SettingsUtil.h"
#include "utils/JsonParser.h"
#include "utils/JsonUtil.h"
// #include "utils/StrUtil.h"

// ---- 路径解析辅助 ----

// 跳过开头的 '/'，读取到下一个 '/'、'[' 或 '\0'，写入 seg
// 返回剩余路径指针
static const char* ParsePathSeg(const char* path, StrBuilder& seg) {
    if (*path == '/') path++;
    while (*path && *path != '/' && *path != '[') {
        seg.AppendChar(*path++);
    }
    return path;
}

// 解析 "[N]"，返回 N，path 指向 ']' 之后
static const char* ParseArrayIdx(const char* path, int& idx) {
    ReportIf(*path != '[');
    path++;
    idx = 0;
    while (*path >= '0' && *path <= '9') {
        idx = idx * 10 + (*path++ - '0');
    }
    if (*path == ']') path++;
    return path;
}

// ---- 在 StructInfo 中按名称查找字段索引 ----
static int FindField(const StructInfo* info, const char* name) {
    const char* fn = info->fieldNames;
    for (int i = 0; i < (int)info->fieldCount; i++, fn += str::Len(fn) + 1) {
        if (str::EqI(fn, name)) return i;
    }
    return -1;
}

static const StructInfo* SubInfo(const FieldInfo& f) {
    return (const StructInfo*)f.value;
}

// ---- 写入单个原始字段 ----
static void WriteField(u8* base, const FieldInfo& f,
                       const char* value, json::Type jtype) {
    u8* ptr = base + f.offset;
    switch (f.type) {
        case SettingType::Bool:
            *(bool*)ptr = str::EqI(value, "true") || str::Eq(value, "1");
            break;
        case SettingType::Int:
            *(int*)ptr = atoi(value);
            break;
        case SettingType::Float:
            *(float*)ptr = (float)atof(value);
            break;
        case SettingType::String:
        case SettingType::Color:
            free(*(char**)ptr);
            *(char**)ptr = str::Dup(value);
            break;
        default:
            break; // Struct/Array/Compact 由路径递归处理
    }
}

// ---- 数组状态：记录已分配的数组元素 ----
struct ArraySlot {
    char* prefix;          // 路径前缀，如 "/items"
    Vec<void*>* vec;       // 指向结构体中的 Vec<void*>*
    const StructInfo* elemInfo;
    int lastIdx = -1;      // 上次处理的索引
};

// ---- Visitor ----
struct JsonStructVisitor : json::ValueVisitor {
    const StructInfo* rootInfo;
    u8* rootBase;
    Vec<ArraySlot> slots;

    JsonStructVisitor(const StructInfo* info, u8* base)
        : rootInfo(info), rootBase(base) {}

    ~JsonStructVisitor() {
        for (int i = 0; i < slots.Size(); i++) {
            free(slots[i].prefix);
        }
    }

    // 查找或创建数组槽
    ArraySlot* GetOrCreateSlot(const char* prefix,
                               Vec<void*>** vecPtr,
                               const StructInfo* elemInfo) {
        for (int i = 0; i < slots.Size(); i++) {
            if (str::Eq(slots[i].prefix, prefix)) return &slots[i];
        }
        // 新建
        if (!*vecPtr) {
            *vecPtr = new Vec<void*>();
        }
        ArraySlot s;
        s.prefix = str::Dup(prefix);
        s.vec = *vecPtr;
        s.elemInfo = elemInfo;
        s.lastIdx = -1;
        slots.Append(s);
        return &slots[slots.Size() - 1];
    }

    // 核心：根据路径找到 (base, StructInfo, fieldIdx)，写入值
    bool Visit(const char* path, const char* value, json::Type jtype) override {
        // 从根开始递归解析路径
        VisitPath(path, value, jtype, rootInfo, rootBase, "");
        return true;
    }

    void VisitPath(const char* path, const char* value, json::Type jtype,
                   const StructInfo* info, u8* base,
                   const char* pathSoFar) {
        if (!*path) return;

        StrBuilder seg;
        const char* rest = ParsePathSeg(path, seg);
        if (seg.size() == 0) return;

        int fi = FindField(info, seg.Get());
        if (fi < 0) return; // 未知字段，忽略

        const FieldInfo& f = info->fields[fi];

        if (*rest == '\0') {
            // 叶子节点：直接写入
            WriteField(base, f, value, jtype);
            return;
        }

        if (*rest == '[') {
            // 数组字段：/arrayField[N]/subField
            if (f.type != SettingType::Array) return;

            int idx;
            rest = ParseArrayIdx(rest, idx);

            // 构造数组前缀路径
            StrBuilder prefixBuf;
            prefixBuf.Append(pathSoFar);
            prefixBuf.AppendChar('/');
            prefixBuf.Append(seg.Get());
            TempStr prefix = prefixBuf.StealData(GetTempAllocator());

            Vec<void*>** vecPtr = (Vec<void*>**)(base + f.offset);
            ArraySlot* slot = GetOrCreateSlot(prefix, vecPtr, SubInfo(f));

            // 确保元素已分配
            while (slot->vec->Size() <= idx) {
                void* elem = AllocArray<u8>(slot->elemInfo->structSize);
                slot->vec->Append(elem);
            }

            u8* elemBase = (u8*)slot->vec->at(idx);
            VisitPath(rest, value, jtype, slot->elemInfo, elemBase, prefix);
            return;
        }

        if (*rest == '/') {
            // 嵌套结构体：/parent/child
            if (f.type != SettingType::Struct &&
                f.type != SettingType::Compact &&
                f.type != SettingType::Prerelease) return;

            StrBuilder prefixBuf;
            prefixBuf.Append(pathSoFar);
            prefixBuf.AppendChar('/');
            prefixBuf.Append(seg.Get());
            TempStr prefix = prefixBuf.StealData(GetTempAllocator());

            u8* subBase = base + f.offset;
            VisitPath(rest, value, jtype, SubInfo(f), subBase, prefix);
            return;
        }
    }
};

// ---- 公开入口 ----
void* JsonDeserializeStruct(StructInfo* info, const char* jsonData,
                            void* strct) {
    u8* base = (u8*)strct;
    if (!base) {
        base = AllocArray<u8>(info->structSize);
    }
    JsonStructVisitor visitor(info, base);
    json::Parse(jsonData, &visitor);
    return base;
}
