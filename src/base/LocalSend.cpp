#include "base/Base.h"
#include "base/File.h"
#include "base/client/UploadSession.h"
#include "base/net/IHttpClient.h"
#include "base/protocol/Constants.h"
#include "base/client/FileSource.h"
#include "base/protocol/Fingerprint.h"
#include "base/protocol/Models.h"
#include "base/net/HttpClient.h"
#include "base/Log.h"
#include "base/Win.h"

using namespace LocalSend;

bool ToRelativePaths(StrVec& absPaths, StrVec& relPaths) {
    if (len(absPaths) < 2) {
        return false;
    }

    TempStr base = str::DupTemp(absPaths.At(0));
    int bLen = len(base);
    // if (!str::IsEmpty(base) && base[bLen-1] != '\\' && base[bLen-1] != '/') {
    //     base = str::FormatTemp("%s\\", base);
    //     bLen = str::Leni(base);
    // }

    if (!dir::Exists(base)) return false;

    for (int i = 1; i < len(absPaths); i++) {
        Str fullPath = absPaths[i];
        if (!str::StartsWithI(fullPath, base)) {
            return false;
        }

        const char* rel = fullPath.s + bLen;
        if (path::IsSep(*rel)) {
            rel++;
        }
        relPaths.Append(rel);
    }
    return true;
}

TempStr GetRelativeTemp(Str basePath, Str absPath) {
    int bLen = len(basePath);
    if (!dir::Exists(basePath)) return nullptr;

    if (!str::StartsWithI(absPath, basePath)) {
        return nullptr;
    }

    const char* rel = absPath.s + bLen;
    if (path::IsSep(*rel)) {
        rel++;
    }

    return str::DupTemp(rel);
}

// 1. 生成带 .pdf 后缀的新文件名列表
StrVec GeneratePdfNames(const StrVec& fileList) {
    StrVec pdfNames;
    for (int i = 0; i < len(fileList); i++) {
        // str::Format 或类似拼接函数，根据实际框架调整
        TempStr newName = fmt("%s.pdf", fileList.At(i));
        pdfNames.Append(newName);
        // free(newName); // 如果 Format 返回的是堆内存且 Append 会深拷贝
    }
    return pdfNames;
}

// 2. 批量重命名文件
// renameFiles=true 为正向重命名(原→pdf)，false 为反向还原(pdf→原)
bool RenameFiles(const StrVec& src, const StrVec& dst, bool forward) {
    const StrVec& from = forward ? src : dst;
    const StrVec& to = forward ? dst : src;

    if (len(from) != len(to)) return false;

    for (int i = 0; i < len(from); i++) {
        if (!file::Rename(to.At(i), from.At(i))) {
            // ⚠️ 部分失败时的回滚策略：
            // 将已经成功重命名的文件还原回去
            for (int j = i - 1; j >= 0; j--) {
                file::Rename(from.At(j), to.At(j)); // 尽力还原，忽略错误
            }
            return false;
        }
    }
    return true;
}

struct TempRenameGuard {
    StrVec origAbsPaths; // 原始绝对路径（用于 rename 和还原）
    StrVec pdfAbsPaths;  // 带 .pdf 后缀的绝对路径（用于实际文件操作）
    StrVec origRelPaths; // 基于 basePath 转换的相对路径（供外部业务使用）
    bool renamed = false;

    // fileList: 绝对路径列表
    // basePath: 计算相对路径的基准目录（必须为绝对路径）
    explicit TempRenameGuard(const StrVec& fileList, Str basePath) {
        for (int i = 0; i < len(fileList); i++) {
            Str absPath = fileList.At(i);

            // 1. 保存原始绝对路径
            origAbsPaths.Append(absPath);

            // 2. 生成带 .pdf 后缀的绝对路径
            TempStr pdfPath = fmt("%s.pdf", absPath);
            pdfAbsPaths.Append(pdfPath);
            // free(pdfPath);

            // 3. 计算并保存相对路径
            TempStr relPath = GetRelativeTemp(basePath, absPath);
            origRelPaths.Append(relPath);
        }
    }

    bool Enter() {
        if (len(origAbsPaths) != len(pdfAbsPaths)) return false;

        // 预检查：防止覆盖已存在的 .pdf 文件
        for (int i = 0; i < len(pdfAbsPaths); i++) {
            if (file::Exists(pdfAbsPaths.At(i))) {
                logf("TempRenameGuard: target already exists: %s", pdfAbsPaths.At(i));
                return false;
            }
        }

        renamed = RenameFiles(origAbsPaths, pdfAbsPaths, true);
        return renamed;
    }

    ~TempRenameGuard() {
        if (renamed) {
            RenameFiles(origAbsPaths, pdfAbsPaths, false);
        }
    }

    // 获取相对路径列表（供后续业务逻辑使用）
    const StrVec& GetRelativePaths() const { return origRelPaths; }

    // 获取当前生效的 pdf 绝对路径列表（供文件读取使用）
    const StrVec& GetPdfAbsPaths() const { return pdfAbsPaths; }

    TempRenameGuard(const TempRenameGuard&) = delete;
    TempRenameGuard& operator=(const TempRenameGuard&) = delete;
};

int LocalSender(Str host, StrVec& p, const Func1<UploadProgress*>& cbProgress) {
    int port = kDefaultPort;
    Str alias = str::Dup("Honey Ratel");
    StrVec fileList;
    // if (!ToRelativePaths(p, fileList)) {
    //     logf("Error: ToRelativePaths failed!\n");
    //     free(alias);
    //     return 1;
    // }
    SliceFirst(p, fileList);
    Str basePath = p.At(0);

    StrVec* paths = &fileList;
    if (paths->IsEmpty()) {
        return 1;
    }

    TempRenameGuard guard(fileList, basePath);
    const StrVec& pdfPaths = guard.GetPdfAbsPaths();
    const StrVec& relPaths = guard.GetRelativePaths();
    if (!guard.Enter()) {
        logf("Failed to prepare .pdf files in %s\n", basePath);
        return 1;
    }

    StrVecWithData<FileMetadata*> fileMetas;
    for (int i = 0; i < len(*paths); ++i) {
        FileMetadata* m = (FileMetadata*)DeserializeStruct(&gFileMetadataInfo, nullptr);
        Str id = str::Dup(fmt("file-%d", i + 1));
        if (!BuildFileMetadata(pdfPaths.At(i), relPaths.At(i), id, m)) {
            logf("Error: BuildFileMetadata(%s) failed!\n", paths->At(i));
            return 1;
        }
        fileMetas.Append(id, m);
    }

    DeviceInfo* info = (DeviceInfo*)DeserializeStruct(&gDeviceInfoInfo, nullptr);
    info->alias = alias;
    info->port = port;
    TempStr exeDir = GetSelfExeDirTemp();
    TempStr fingerprintPath = path::JoinTemp(exeDir, "localsend_fingerprint");
    info->fingerprint = LoadOrCreateFingerprint(fingerprintPath);

    HttpClient http;

    UploadSession session(http, info);
    SendReport report = session.Send(host, port, 4, 64 * 1024, &fileMetas, cbProgress);

    if (!report.prepared) {
        const char* hint = "";
        switch (report.prepareStatus) {
            case 0:
                hint = " (connessione fallita: host/port/files?)";
                break;
            case 204:
                hint = " (Finished (No file transfer needed))";
                break;
            case 400:
                hint = " (Invalid body)";
                break;
            case 401:
                hint = " (PIN required / Invalid PIN)";
                break;
            case 403:
                hint = " (Rejected)";
                break;
            case 409:
                hint = " (Blocked by another session)";
                break;
            case 429:
                hint = " (Too many requests)";
                break;
            case 500:
                hint = " (Unknown error by receiver)";
                break;
        }
        if (-1 == report.prepareStatus) {
            logf("Please turn off LocalSend app's https, ensuring transfering from http\n");
        }
        logf("prepare-upload fallito: HTTP %d%s\n", report.prepareStatus, TempStr(hint));
        // return 1;
    }

    logf("Sessione: %s\n", report.sessionId);
    for (const auto& f : report.files) {
        logf("  [%-9s] %s%s%s\n", TempStr(StatusText(f.status)), f.fileName,
             TempStr(str::IsEmpty(f.detail) ? "" : "  -> "), f.detail);
        str::Free(f.detail);
    }

    logf("report->AllSent: %s\n", report.AllSent() ? TempStr("true") : TempStr("false"));
    if (report.sessionId) {
        str::Free(report.sessionId);
    }
    FreeStruct(&gDeviceInfoInfo, info);
    FileMetadata* t;
    for (int i = 0; i < len(fileMetas); i++) {
        t = *fileMetas.AtData(i);
        if (t) {
            FreeStruct(&gFileMetadataInfo, t);
        }
    }

    return report.AllSent() ? 0 : 1;
}
