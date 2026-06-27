// #pragma once
// #include "utils/SettingsUtil.h"

// 将 JSON 数据反序列化为结构体
// 使用与 SettingsUtil 完全相同的 StructInfo/FieldInfo 元数据
// strct 为 nullptr 时自动分配，调用方用 FreeStruct(info, ptr) 释放
void* JsonDeserializeStruct(StructInfo* info, const char* jsonData, void* strct = nullptr);
