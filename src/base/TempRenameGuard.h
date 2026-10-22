#pragma once

// 假设 Str, StrVec, TempStr 等类型已在 base.h 中定义
// 如果不在，请在此处 #include 对应的头文件
#include "base.h"

// --- 辅助函数声明 ---
TempStr GetRelativeTemp(Str basePath, Str absPath);
StrVec GeneratePdfNames(const StrVec& fileList);
bool RenameFiles(const StrVec& src, const StrVec& dst, bool forward);

// --- 核心结构体定义 ---
struct TempRenameGuard {
    StrVec origAbsPaths;
    StrVec pdfAbsPaths;
    StrVec origRelPaths;
    bool renamed = false;

    explicit TempRenameGuard(const StrVec& fileList, Str basePath);
    ~TempRenameGuard();

    bool Enter();

    const StrVec& GetRelativePaths() const;
    const StrVec& GetPdfAbsPaths() const;

    // 禁用拷贝
    TempRenameGuard(const TempRenameGuard&) = delete;
    TempRenameGuard& operator=(const TempRenameGuard&) = delete;
};
