// #include "common/common.h"
#include "base/Base.h"
#include "base/Dict.h"
#include "base/JsonParser.h"
#include "base/JsonUtil.h"
// #include "base/StrUtil.h"

// https://deepwiki.com/search/jsonparsekeyvalue_bd5374cd-40e8-4640-9734-092cef1686d3
// 简化版通用 JSON -> 结构体反射反序列化
namespace jsonrefl {

static const FieldInfo* FindField(const StructInfo* info, Str key) {
    const char* p = info->fieldNames;
    for (size_t i = 0; i < info->fieldCount; i++, p += len(p) + 1) {
        if (str::Eq(Str(p), key)) return &info->fields[i];
    }
    return nullptr;
}

// MapFindOrAdd's linear scan is O(n) per key lookup; fine for small maps (a handful of IPs), but if ipPool-style maps
// can grow large, you'd want a real hash map — not something built into this reflection framework.
static int MapFindOrAdd(Vec<Str>* keys, Str key) {
    for (int i = 0; i < len(*keys); i++) {
        if (str::Eq(keys->els[i], key)) return i;
    }
    VecAppend(*keys, str::Dup(key));
    return keys->len - 1;
}

static void SetLeaf(const FieldInfo* f, u8* fieldPtr, json::Value& v) {
    switch (f->type) {
        case Type::String:
            *(Str*)fieldPtr = str::Dup(v.value);
            break;
        case Type::Int:
            *(int*)fieldPtr = ParseInt(v.value);
            break;
        case Type::Float:
            str::Parse(v.value, "%f", (float*)fieldPtr);
            break;
        case Type::Bool:
            *(bool*)fieldPtr = str::Eq(v.value, StrL("true"));
            break;
        default:
            ReportIf(true);
    }
}

struct DeserCtx {
    const StructInfo* rootInfo;
    void* rootData;
};

static void ConstructData(const StructInfo* info, u8* base) {
    for (size_t i = 0; i < info->fieldCount; i++) {
        const FieldInfo& f = info->fields[i];
        u8* fieldPtr = base + f.offset;
        switch (f.type) {
            case Type::Struct:
                // embedded struct lives inline in this same block; recurse in place
                ConstructData((StructInfo*)f.value, fieldPtr);
                break;
            case Type::MapStruct:
                new (fieldPtr) MapRaw(); // ArenaNew()+NewHashTable() happens here
                break;
            case Type::MapString:
                new (fieldPtr) MapStringRaw();
                break;
            case Type::MapIntArray:
            case Type::MapFloatArray:
            case Type::MapBoolArray:
            case Type::MapStringArray:
                new (fieldPtr) MapArrayRaw();
                break;
            case Type::MapStructArray:
                new (fieldPtr) MapStructArrayRaw();
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

static u8* AllocAndConstruct(const StructInfo* info) {
    u8* p = AllocArray<u8>(info->structSize);
    ConstructData(info, p);
    return p;
}

static u8* MapValueSlotAry(const StructInfo* valueInfo, MapRawAry* m, Str key) {
    int idx = MapFindOrAdd(&m->keys, key);
    while (m->values.len <= idx) {
        VecAppend(m->values, AllocArray<u8>(valueInfo->structSize));
    }
    return m->values.els[idx];
}

static u8* MapRawSlot(const StructInfo* valueInfo, MapRaw* m, Str key) {
    int idx;
    if (!m->keys.Insert(key, len(m->values), &idx)) {
        // key 已存在，idx 已经被 Insert 写成已有值
    } else {
        idx = len(m->values); // 刚插入，Insert 的第二个参数就是我们要用的新下标
    }
    while (len(m->values) <= idx) {
        u8* el = AllocAndConstruct(valueInfo);
        VecAppend(m->values, el);
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
        VecAppend(m->values, Str{});
    }
    return &m->values.els[idx];
}

static void* MapArraySlot(Vec<void*>* mapValues, dict::MapStrToInt* keys, Str key, bool isStructArray) {
    int idx;
    if (!keys->Insert(key, len(*mapValues), &idx)) {
        // key already existed; idx holds the stored value
    } else {
        idx = len(*mapValues);
    }
    while (len(*mapValues) <= idx) {
        VecAppend(*mapValues, nullptr); // array not yet created; created lazily on first element
    }
    return isStructArray ? (void*)&mapValues->els[idx]
                         : (void*)&mapValues->els[idx]; // return address of the void* slot itself (may be null)
}

static void OnValue(DeserCtx* ctx, json::Value& v) {
    const StructInfo* info = ctx->rootInfo;
    u8* base = (u8*)ctx->rootData;
    const FieldInfo* leafField = nullptr;
    const FieldInfo* primArrField = nullptr; // 基本类型数组字段(跨seg记忆)
    u8* primArrBase = nullptr;
    const FieldInfo* mapField = nullptr; // remembers we're inside a Map* field
    u8* mapBase = nullptr;
    Type dynArrayElemType = Type::Int; // remembers which primitive Vec<T> to use
    void** dynArraySlot = nullptr;     // NEW: address of the void* holding Vec<T>*/Vec<void*>*

    for (StrNode* seg = v.path; seg; seg = seg->next) {
        if (seg->s.s[0] == json::kSegKey) {
            Str key = json::PathSegKey(seg);

            if (mapField) {
                bool isArrayMap = mapField->type == Type::MapIntArray || mapField->type == Type::MapFloatArray ||
                                  mapField->type == Type::MapBoolArray || mapField->type == Type::MapStringArray ||
                                  mapField->type == Type::MapStructArray;
                if (isArrayMap) {
                    bool isStructArr = mapField->type == Type::MapStructArray;
                    auto* raw = isStructArr ? &((MapStructArrayRaw*)mapBase)->values : &((MapArrayRaw*)mapBase)->values;
                    dict::MapStrToInt* keys =
                        isStructArr ? &((MapStructArrayRaw*)mapBase)->keys : &((MapArrayRaw*)mapBase)->keys;
                    dynArraySlot = (void**)MapArraySlot(raw, keys, key, isStructArr);
                    dynArrayElemType = mapField->type;   // remember before we clear mapField
                    info = (StructInfo*)mapField->value; // element StructInfo, used only if MapStructArray
                }
                // key is the dynamic map key (e.g. "192.168.1.1")
                else if (mapField->type == Type::MapStruct) {
                    // auto* m = (MapRawAry*)mapBase; // T size handled via mapField->sub->size below
                    auto* m = (MapRaw*)mapBase; // T size handled via mapField->sub->size below

                    // int idx = MapFindOrAdd(&m->keys, key);
                    // grow values to len(keys); values holds raw T-sized blocks
                    // (see note below on why MapField<u8> is a placeholder)
                    // info = (StructInfo*)mapField->value;
                    if (false) {
                        base = MapValueSlotAry((StructInfo*)mapField->value, (MapRawAry*)m, key); // see helper below
                    }
                    base = MapRawSlot((StructInfo*)mapField->value, m, key); // see helper below
                    info = (StructInfo*)mapField->value;
                } else { // MapString
                    auto* m = (MapStringRaw*)mapBase;
                    // int idx = MapFindOrAdd(&m->keys, key);
                    // while (m->values.len <= idx) m->values.Append(Str{});
                    // leafField = nullptr; // handled directly below
                    Str* slot = MapStringSlot(m, key);
                    base = (u8*)slot;
                    // string leaf: value set when we hit the primitive callback
                    static FieldInfo strLeaf{0, Type::String};
                    leafField = &strLeaf;
                }
                mapField = nullptr;
                continue;
            }

            if (!info) return; // path 走进了一个数组元素之后必须先有下标段，才能再取 key
            const FieldInfo* f = FindField(info, key);
            if (!f) return; // 未知字段，忽略
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

            switch (f->type) {
                case Type::MapStruct:
                case Type::MapString:
                    mapField = f;
                    mapBase = fieldPtr;
                    info = nullptr;
                    break;
                case Type::Struct:
                case Type::StructArray:
                    // 内嵌结构体，直接进入
                    // 指向 Vec<void*>* 字段，下一段应为下标
                    info = (StructInfo*)f->value;
                    base = fieldPtr;
                    break;
                case Type::IntArray:
                case Type::FloatArray:
                case Type::BoolArray:
                case Type::StringArray:
                    primArrField = f;
                    primArrBase = fieldPtr;
                    info = nullptr;
                    break;
                default:
                    leafField = f;
                    base = fieldPtr;
                    break; // 基本类型叶子
            }
        } else if (seg->s.s[0] == json::kSegIdx) {
            int idx = json::PathSegIndex(seg);

            if (dynArraySlot) {
                // growing the array that lives at a dynamic map key's slot
                // if (!*dynArraySlot) {
                // lazily create the backing Vec<T>/Vec<void*> on first element
                // *dynArraySlot = info ? (void*)new Vec<void*>() : (void*)new Vec<int>();
                // NOTE: which concrete Vec<T> to `new` depends on mapField's element
                // primitive type; see caveat below on why this needs a type tag,
                // not just a null-check, once more than one primitive type is in play.
                // }
                // if (info) { // MapStructArray: elements are struct blocks
                //     auto* vec = (Vec<void*>*)*dynArraySlot;
                //     while (len(*vec) <= idx) {
                //         u8* el = AllocArray<u8>(info->structSize);
                //         ConstructData(info, el);
                //         vec->Append(el);
                //     }
                //     base = (u8*)vec->els[idx]; // struct leaf fields resolved by subsequent kSegKey segs
                // } else { // MapIntArray / MapFloatArray / MapBoolArray / MapStringArray
                //     // dispatch on mapField's declared primitive kind (mapField still valid
                //     // via a saved copy — see caveat) and grow the concrete Vec<T> the same
                //     // way IntArray/FloatArray/BoolArray/StringArray already do
                // }
                switch (dynArrayElemType) {
                    case Type::MapStructArray: {
                        if (!*dynArraySlot) {
                            *dynArraySlot = new Vec<void*>();
                        }
                        auto* vec = (Vec<void*>*)*dynArraySlot;
                        while (len(*vec) <= idx) {
                            u8* el = AllocArray<u8>(info->structSize);
                            ConstructData(info, el);
                            VecAppend(*vec, el);
                        }
                        base = (u8*)vec->els[idx];
                        break;
                    }
                    case Type::MapIntArray: {
                        if (!*dynArraySlot) *dynArraySlot = new Vec<int>();
                        auto* vec = (Vec<int>*)*dynArraySlot;
                        while (len(*vec) <= idx) VecAppend(*vec, 0);
                        (*vec)[idx] = ParseInt(v.value);
                        break;
                    }
                    case Type::MapFloatArray: {
                        if (!*dynArraySlot) *dynArraySlot = new Vec<float>();
                        auto* vec = (Vec<float>*)*dynArraySlot;
                        while (len(*vec) <= idx) VecAppend(*vec, 0.f);
                        str::Parse(v.value, "%f", &(*vec)[idx]);
                        break;
                    }
                    case Type::MapBoolArray: {
                        if (!*dynArraySlot) *dynArraySlot = new Vec<bool>();
                        auto* vec = (Vec<bool>*)*dynArraySlot;
                        while (len(*vec) <= idx) VecAppend(*vec, false);
                        (*vec)[idx] = str::Eq(v.value, StrL("true"));
                        break;
                    }
                    case Type::MapStringArray: {
                        if (!*dynArraySlot) *dynArraySlot = new Vec<Str>();
                        auto* vec = (Vec<Str>*)*dynArraySlot;
                        while (len(*vec) <= idx) VecAppend(*vec, Str{});
                        (*vec)[idx] = str::Dup(v.value);
                        break;
                    }
                    default:
                        ReportIf(true); // unreachable: dynArraySlot only set for the 5 map-array types
                }
                continue;
            }

            if (primArrField) {
                // 基本类型数组：按下标扩容并直接写值，无需子结构体
                switch (primArrField->type) {
                    case Type::IntArray: {
                        auto** vp = (Vec<int>**)primArrBase;
                        if (!*vp) *vp = new Vec<int>();
                        while (len(**vp) <= idx) VecAppend(*(*vp), 0);
                        (**vp)[idx] = ParseInt(v.value);
                        break;
                    }
                    case Type::FloatArray: {
                        auto** vp = (Vec<float>**)primArrBase;
                        if (!*vp) *vp = new Vec<float>();
                        while (len(**vp) <= idx) VecAppend(*(*vp), 0.f);
                        str::Parse(v.value, "%f", &(**vp)[idx]);
                        break;
                    }
                    case Type::BoolArray: {
                        auto** vp = (Vec<bool>**)primArrBase;
                        if (!*vp) *vp = new Vec<bool>();
                        while (len(**vp) <= idx) VecAppend(*(*vp), false);
                        (**vp)[idx] = str::Eq(v.value, StrL("true"));
                        break;
                    }
                    case Type::StringArray: {
                        auto** vp = (Vec<Str>**)primArrBase;
                        if (!*vp) *vp = new Vec<Str>();
                        while (len(**vp) <= idx) VecAppend(*(*vp),Str{});
                        (**vp)[idx] = str::Dup(v.value);
                        break;
                    }
                    default:
                        break;
                }
                return; // 这一段之后不会再有 key（基本类型数组元素是叶子）
            }
            auto** vecField = (Vec<void*>**)base;
            if (!*vecField) *vecField = new Vec<void*>();
            Vec<void*>* vec = *vecField;
            while (len(*vec) <= idx) {
                u8* el = AllocAndConstruct(info);
                VecAppend(*vec, el); // 按需扩容并 zero-init 新元素
            }
            base = (u8*)(*vec)[idx];
        }
    }
    if (leafField) {
        SetLeaf(leafField, base, v);
    }
}

void* Deserialize(const StructInfo* info, Str jsonData) {
    void* data = AllocArray<u8>(info->structSize);
    ConstructData(info, (u8*)data);
    DeserCtx ctx{info, data};
    json::Reader r(jsonData);
    for (json::Value& v : r) {
        OnValue(&ctx, v);
    }
    // json::Parse(jsonData, MkFunc1<DeserCtx, json::Value*>(OnValue, &ctx));
    return data;
}

static void FreeData(const StructInfo* info, u8* base); // 前向声明

static void FreeMapRawAry(MapRawAry* m, const StructInfo* valueInfo) {
    if (!m) return;
    for (int i = 0; i < m->keys.len; i++) {
        str::Free(m->keys.els[i]);
    }
    for (int i = 0; i < m->values.len; i++) {
        FreeData(valueInfo, m->values.els[i]);
        free(m->values.els[i]);
    }
    VecReset(m->keys);
    VecReset(m->values);
}

static void FreeMapStringAry(MapStringRawAry* m) {
    if (!m) return;
    for (int i = 0; i < m->keys.len; i++) str::Free(m->keys.els[i]);
    for (int i = 0; i < m->values.len; i++) str::Free(m->values.els[i]);
    VecReset(m->keys);
    VecReset(m->values);
}

static void FreeMapRaw(MapRaw* m, const StructInfo* valueInfo) {
    if (!m) return;
    for (int i = 0; i < len(m->values); i++) {
        FreeData(valueInfo, m->values.els[i]);
        free(m->values.els[i]);
    }
    VecReset(m->values);
    m->keys.~MapStrToInt(); // 释放内部哈希表 + arena（含所有 key 副本）
}

static void FreeMapString(MapStringRaw* m) {
    if (!m) return;
    for (int i = 0; i < len(m->values); i++) {
        str::Free(m->values.els[i]);
    }
    VecReset(m->values);
    m->keys.~MapStrToInt();
}

static void FreeMapArrayRaw(MapArrayRaw* m, Type elemType) {
    if (!m) return;
    for (int i = 0; i < len(m->values); i++) {
        void* p = m->values.els[i];
        if (!p) continue;
        switch (elemType) {
            case Type::MapIntArray:
                delete (Vec<int>*)p;
                break;
            case Type::MapFloatArray:
                delete (Vec<float>*)p;
                break;
            case Type::MapBoolArray:
                delete (Vec<bool>*)p;
                break;
            case Type::MapStringArray: {
                auto* vs = (Vec<Str>*)p;
                for (int j = 0; j < len(*vs); j++) str::Free(vs->els[j]);
                delete vs;
                break;
            }
            default:
                break;
        }
    }
    VecReset(m->values);
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
    VecReset(m->values);
    m->keys.~MapStrToInt();
}

static void FreeStructArray(Vec<void*>* array, const StructInfo* elemInfo) {
    if (!array) return;
    for (void* el : *array) {
        FreeData(elemInfo, (u8*)el);
        free(el);
    }
    delete array;
}

static void FreeData(const StructInfo* info, u8* base) {
    for (size_t i = 0; i < info->fieldCount; i++) {
        const FieldInfo& f = info->fields[i];
        u8* fieldPtr = base + f.offset;
        switch (f.type) {
            case Type::String:
                str::Free(*(Str*)fieldPtr);
                *(Str*)fieldPtr = Str{};
                break;
            case Type::Struct:
                FreeData((StructInfo*)f.value, fieldPtr); // 内嵌结构体，就地递归，不 free(fieldPtr) 本身
                break;
            case Type::MapStruct:
                if (false) FreeMapRawAry((MapRawAry*)fieldPtr, (StructInfo*)f.value);
                FreeMapRaw((MapRaw*)fieldPtr, (StructInfo*)f.value);
                break;
            case Type::MapString:
                if (false) FreeMapStringAry((MapStringRawAry*)fieldPtr);
                FreeMapString((MapStringRaw*)fieldPtr);
                break;
            case Type::MapIntArray:
            case Type::MapFloatArray:
            case Type::MapBoolArray:
            case Type::MapStringArray:
                FreeMapArrayRaw((MapArrayRaw*)fieldPtr, f.type);
                break;
            case Type::MapStructArray:
                FreeMapStructArrayRaw((MapStructArrayRaw*)fieldPtr, (StructInfo*)f.value);
                break;
            case Type::StructArray:
                FreeStructArray(*(Vec<void*>**)fieldPtr, (StructInfo*)f.value);
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
            case Type::Int:
            case Type::Float:
            case Type::Bool:
                // break; // 无需释放
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

} // namespace jsonrefl
