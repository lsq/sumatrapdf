// #pragma once
// #include "base/SettingsUtil.h"

// 将 JSON 数据反序列化为结构体
// 使用与 SettingsUtil 完全相同的 StructInfo/FieldInfo 元数据
// strct 为 nullptr 时自动分配，调用方用 FreeStruct(info, ptr) 释放
// void* JsonDeserializeStruct(StructInfo* info, Str jsonData, void* strct);
namespace jsonrefl {

enum class Type {
    String,
    Int,
    Float,
    Bool,
    Struct,
    StructArray,
    IntArray,
    FloatArray,
    BoolArray,
    StringArray,
    MapStruct, // dynamic-key -> struct value
    MapString, // dynamic-key -> struct value
    MapIntArray,
    MapFloatArray,
    MapBoolArray,
    MapStringArray, // Map Array
    MapStructArray
};

struct FieldInfo {
    // offset of the field in the struct
    size_t offset = 0;
    Type type = Type::Struct;
    // default value for primitive types and pointer to StructInfo for complex ones
    intptr_t value = 0;
    // app-managed / deprecated setting: serialized like any other, but hidden
    // from the advanced settings dialog
    bool internal = false;
};

struct StructInfo {
    u16 structSize = 0;
    u16 fieldCount = 0;
    const FieldInfo* fields = nullptr;
    // one string of fieldCount zero-terminated names of all fields
    // in the order of fields
    const char* fieldNames = nullptr;
    // one string of fieldCount zero-terminated per-field doc comments, in the
    // same order as fieldNames (empty entry if a field has no comment)
    const char* fieldComments = nullptr;
    // true if this struct has a Bool field named IsTemporary (array elements
    // with that flag set are omitted when serializing)
    bool couldBeTemporary = false;
};

// template <typename T>
// raw-byte parallel array for struct-typed map values (no T template needed)
struct MapRawAry {
    Vec<Str> keys;
    // each element is a heap block of size == valueInfo->size, like StructArray
    Vec<u8*> values; // POD value type (e.g. IpInfo)
};

struct MapStringRawAry {
    Vec<Str> keys;
    Vec<Str> values;
};

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

void* Deserialize(const StructInfo* info, Str jsonData);
void Free(const StructInfo* info, void* data);

} // namespace jsonrefl
