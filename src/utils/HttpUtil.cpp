/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "utils/BaseUtil.h"
#include "utils/ThreadUtil.h"
#include "utils/FileUtil.h"
#include "utils/ScopedWin.h"
#include "utils/WinUtil.h"
#include "utils/StrQueue.h"
#include "utils/HttpUtil.h"

#include "utils/Log.h"

// per RFC 1945 10.15 and 3.7, a user agent product token shouldn't contain whitespace
constexpr const WCHAR* kUserAgent = L"SumatraPdfHTTP";

bool IsHttpRspOk(const HttpRsp* rsp) {
    if (rsp->error != ERROR_SUCCESS) {
        logf("HttpRspOk: rsp->error %d, should be %d (ERROR_SUCCESS)\n", (int)rsp->error, (int)ERROR_SUCCESS);
        return false;
    }
    if (rsp->httpStatusCode >= 300) {
        logf("HttpRspOk: rsp->httpStatusCode: %d\n", (int)rsp->httpStatusCode);
        return false;
    }
    return true;
}

// returns false if failed to download or status code is not 200
// for other scenarios, check HttpRsp
bool HttpGet(const char* urlA, HttpRsp* rspOut) {
    logf("HttpGet: url: '%s'\n", urlA);
    HINTERNET hReq = nullptr;
    DWORD infoLevel;
    DWORD headerBuffSize = sizeof(DWORD);
    WCHAR* url = ToWStrTemp(urlA);
    DWORD flags = INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_IGNORE_CERT_CN_INVALID;

    if (str::StartsWithI(urlA, "https")) {
        flags |= INTERNET_FLAG_SECURE;
    }

    rspOut->error = ERROR_SUCCESS;
    HINTERNET hInet = InternetOpenW(kUserAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) {
        logf("HttpGet: InternetOpen failed\n");
        LogLastError();
        goto Error;
    }

    hReq = InternetOpenUrlW(hInet, url, nullptr, 0, flags, 0);
    if (!hReq) {
        logf("HttpGet: InternetOpenUrl failed\n");
        LogLastError();
        goto Error;
    }

    infoLevel = HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER;
    if (!HttpQueryInfoW(hReq, infoLevel, &rspOut->httpStatusCode, &headerBuffSize, nullptr)) {
        logf("HttpGet: HttpQueryInfoW failed\n");
        LogLastError();
        goto Error;
    }

    for (;;) {
        char buf[1024];
        DWORD dwRead = 0;
        if (!InternetReadFile(hReq, buf, sizeof(buf), &dwRead)) {
            logf("HttpGet: InternetReadFile failed\n");
            LogLastError();
            goto Error;
        }
        if (0 == dwRead) {
            break;
        }
        InterlockedIncrement(&gAllowAllocFailure);
        bool ok = rspOut->data.Append(buf, dwRead);
        InterlockedDecrement(&gAllowAllocFailure);
        if (!ok) {
            logf("HttpGet: data.Append failed\n");
            goto Error;
        }
    }

Exit:
    if (hReq) {
        InternetCloseHandle(hReq);
    }
    if (hInet) {
        InternetCloseHandle(hInet);
    }
    return IsHttpRspOk(rspOut);

Error:
    rspOut->error = GetLastError();
    if (0 == rspOut->error) {
        rspOut->error = ERROR_GEN_FAILURE;
    }
    goto Exit;
}

constexpr const int kBufSize = 256 * 1024;

// Download content of a url to a file
bool HttpGetToFile(const char* urlA, const char* destFilePath, const Func1<HttpProgress*>& cbProgress) {
    logf("HttpGetToFile: url: '%s', file: '%s'\n", urlA, destFilePath);
    bool ok = false;
    HINTERNET hReq = nullptr, hInet = nullptr;
    DWORD dwRead = 0;
    DWORD headerBuffSize = sizeof(DWORD);
    DWORD statusCode = 0;
    WCHAR* url = ToWStrTemp(urlA);
    char* buf = nullptr;

    HttpProgress progress{};

    WCHAR* pathW = ToWStrTemp(destFilePath);
    HANDLE hf =
        CreateFileW(pathW, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (INVALID_HANDLE_VALUE == hf) {
        logf("HttpGetToFile: CreateFileW('%s') failed\n", destFilePath);
        LogLastError();
        goto Exit;
    }

    buf = AllocArray<char>(kBufSize);
    if (!buf) {
        goto Exit;
    }

    hInet = InternetOpenW(kUserAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) {
        goto Exit;
    }

    hReq = InternetOpenUrlW(hInet, url, nullptr, 0, 0, 0);
    if (!hReq) {
        goto Exit;
    }

    if (!HttpQueryInfoW(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &headerBuffSize, nullptr)) {
        goto Exit;
    }

    if (statusCode != 200) {
        goto Exit;
    }

    for (;;) {
        if (!InternetReadFile(hReq, buf, kBufSize, &dwRead)) {
            goto Exit;
        }
        if (dwRead == 0) {
            break;
        }
        DWORD size;
        BOOL wroteOk = WriteFile(hf, buf, (DWORD)dwRead, &size, nullptr);
        if (!wroteOk) {
            goto Exit;
        }
        progress.nDownloaded += (i64)dwRead;
        cbProgress.Call(&progress);

        if (size != dwRead) {
            goto Exit;
        }
    }

    ok = true;
Exit:
    CloseHandle(hf);
    if (hReq) {
        InternetCloseHandle(hReq);
    }
    if (hInet) {
        InternetCloseHandle(hInet);
    }
    if (!ok) {
        file::Delete(destFilePath);
    }
    free(buf);
    return ok;
}

bool HttpPost(const char* serverA, int port, const char* urlA, StrBuilder* headers, StrBuilder* data) {
    StrBuilder resp(2048);
    bool ok = false;
    char* hdr = nullptr;
    DWORD hdrLen = 0;
    HINTERNET hConn = nullptr, hReq = nullptr;
    void* d = nullptr;
    DWORD dLen = 0;
    unsigned int timeoutMs = 15 * 1000;
    DWORD respHttpCode = 0;
    DWORD respHttpCodeSize = sizeof(respHttpCode);
    DWORD dwRead = 0;
    DWORD flags;
    DWORD dwService;
    WCHAR* server = ToWStrTemp(serverA);
    WCHAR* url = ToWStrTemp(urlA);
    DWORD infoLevel;

    DWORD accessType = INTERNET_OPEN_TYPE_PRECONFIG;
    HINTERNET hInet = InternetOpenW(kUserAgent, accessType, nullptr, nullptr, 0);
    if (!hInet) {
        goto Exit;
    }
    dwService = INTERNET_SERVICE_HTTP;
    hConn = InternetConnectW(hInet, server, (INTERNET_PORT)port, nullptr, nullptr, dwService, 0, 1);
    if (!hConn) {
        goto Exit;
    }

    flags = INTERNET_FLAG_NO_UI;
    if (port == 443) {
        flags |= INTERNET_FLAG_SECURE;
    }
    hReq = HttpOpenRequestW(hConn, L"POST", url, nullptr, nullptr, nullptr, flags, 0);
    if (!hReq) {
        goto Exit;
    }

    if (headers && headers->size() > 0) {
        hdr = headers->Get();
        hdrLen = (DWORD)headers->size();
    }
    if (data && data->size() > 0) {
        d = data->Get();
        dLen = (DWORD)data->size();
    }

    InternetSetOptionW(hReq, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(hReq, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    if (!HttpSendRequestA(hReq, hdr, hdrLen, d, dLen)) {
        goto Exit;
    }

    infoLevel = HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER;
    HttpQueryInfoW(hReq, infoLevel, &respHttpCode, &respHttpCodeSize, nullptr);

    do {
        char buf[1024];
        if (!InternetReadFile(hReq, buf, sizeof(buf), &dwRead)) {
            goto Exit;
        }
        ok = resp.Append(buf, dwRead);
        if (!ok) {
            goto Exit;
        }
    } while (dwRead > 0);

#if 0
    // it looks like I should be calling HttpEndRequest(), but it always claims
    // a timeout even though the data has been sent, received and we get HTTP 200
    if (!HttpEndRequest(hReq, nullptr, 0, 0)) {
        LogLastError();
        goto Exit;
    }
#endif
    ok = (200 == respHttpCode);
Exit:
    if (hReq) {
        InternetCloseHandle(hReq);
    }
    if (hConn) {
        InternetCloseHandle(hConn);
    }
    if (hInet) {
        InternetCloseHandle(hInet);
    }
    return ok;
}

void FileReaderThread(FileReaderData* td) {
    // AutoDelete del(td); // 下面已经用delete管理了，不需要了
    WCHAR* pathW = ToWStrTemp(td->filePath);
    HANDLE hFile = CreateFileW(pathW, GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        // logf("HttpPostFileStream reader: CreateFileW failed for '%s'\n", td->filePath);
        *td->errorOut = true;
        td->queue->MarkFinished();
        free(td->filePath);
        delete td;
        DestroyTempAllocator();
        return;
    }

    char* buf = AllocArray<char>(td->chunkSize);
    // long tt = 0;
    for (;;) {
        DWORD dwRead = 0;
        if (!ReadFile(hFile, buf, (DWORD)td->chunkSize, &dwRead, nullptr)) {
            // logf("HttpPostFileStream reader: ReadFile failed\n");
            *td->errorOut = true;
            break;
        }
        // tt += dwRead;
        // logf("debug: file read length: %d\n, total read: %d\n", dwRead, tt);
        if (dwRead == 0) {
            break; // EOF
        }
        if (!td->queue->Push(buf, (int)dwRead)) {
            break; // 队列已关闭
        }
    }

    free(buf);
    CloseHandle(hFile);
    // logf("debug: FileReaderThread exiting, calling MarkFinished...\n");
    td->queue->MarkFinished();
    free(td->filePath);
    delete td;
    DestroyTempAllocator();
}

// -----------------------------------------------------------------------
// 主函数：流式 POST 文件
// -----------------------------------------------------------------------
bool HttpPostFileStream(const char* serverA, int port, const char* urlPathA,
                        const char* filePath,
                        int maxQueueChunks,
                        int chunkSize,
                        Func1<HttpUploadProgress*>& cbProgress) {
    logf("HttpPostFileStream: server='%s' port=%d path='%s' file='%s'\n",
         serverA, port, urlPathA, filePath);

    bool ok = false;
    HINTERNET hInet = nullptr, hConn = nullptr, hReq = nullptr;
    DWORD respCode = 0, respCodeSize = sizeof(respCode);
    DWORD timeoutMs = 60 * 1000;

    // 获取文件大小（用于 Content-Length 和进度计算）
    i64 fileSize = file::GetSize(filePath);
    if (fileSize < 0) {
        logf("HttpPostFileStream: file not found or empty\n");
        return false;
    }

    // 创建有界并发队列，启动生产者线程
    BoundedByteQueue queue(maxQueueChunks);
    bool readerError = false;

    auto td = new FileReaderData{&queue, str::Dup(filePath), chunkSize, &readerError};
    auto fn = MkFunc0(FileReaderThread, td);
    RunAsync(fn, "HttpPostFileStreamReader");

    // 建立 HTTP 连接
    WCHAR* server  = ToWStrTemp(serverA);
    WCHAR* urlPath = ToWStrTemp(urlPathA);

    hInet = InternetOpenW(kUserAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) {
        logf("HttpPostFileStream: InternetOpenW failed\n");
        goto Exit;
    }

    hConn = InternetConnectW(hInet, server, (INTERNET_PORT)port,
                             nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 1);
    if (!hConn) {
        logf("HttpPostFileStream: InternetConnectW failed\n");
        goto Exit;
    }

    {
        DWORD flags = INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD;
        if (port == 443) {
            flags |= INTERNET_FLAG_SECURE;
        }
        hReq = HttpOpenRequestW(hConn, L"POST", urlPath, nullptr, nullptr, nullptr, flags, 0);
    }
    if (!hReq) {
        logf("HttpPostFileStream: HttpOpenRequestW failed\n");
        goto Exit;
    }

    InternetSetOptionW(hReq, INTERNET_OPTION_SEND_TIMEOUT,    &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(hReq, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    // 添加 Content-Length 头
    {
        TempStr hdr = str::FormatTemp("Content-Length: %lld\r\nContent-Type: application/octet-stream\r\n\r\n",
                                      (long long)fileSize);
        WCHAR* hdrW = ToWStrTemp(hdr);
        HttpAddRequestHeadersW(hReq, hdrW, (DWORD)-1, HTTP_ADDREQ_FLAG_ADD);
    }

    // 开始流式发送（不在此处传入 body，后续用 InternetWriteFile 写入）
    {
        INTERNET_BUFFERS bufIn{};
        bufIn.dwStructSize  = sizeof(INTERNET_BUFFERS);
        bufIn.dwBufferTotal = (DWORD)fileSize;
        if (!HttpSendRequestExW(hReq, &bufIn, nullptr, 0, 0)) {
            logf("HttpPostFileStream: HttpSendRequestExW failed\n");
            LogLastError();
            goto Exit;
        }
    }

    // 消费队列，逐块写入 HTTP 请求体
    {
        HttpUploadProgress progress{};
        progress.totalSize = fileSize;

        ByteChunk chunk;
        // int tt = 0;
        for (;;) {
            if (!queue.Pop(chunk)) {
                break; // 队列结束
            }
            DWORD dwWritten = 0;
            // logf("debug: chunk data length: %d\n", chunk.len);
            BOOL writeOk = InternetWriteFile(hReq, chunk.data, (DWORD)chunk.len, &dwWritten);
            free(chunk.data);
            if (!writeOk) {
                logf("HttpPostFileStream: InternetWriteFile failed\n");
                LogLastError();
                goto Exit;
            }
            // tt += dwWritten;
            // logf("debug: send data length: %d\nTotal send: %d\n", dwWritten, tt);
            progress.nUploaded += (i64)dwWritten;
            cbProgress.Call(&progress);
        }
    }

    if (readerError) {
        logf("HttpPostFileStream: file reader thread reported error\n");
        goto Exit;
    }

    // 结束请求，等待服务器响应
    if (!HttpEndRequest(hReq, nullptr, 0, 0)) {
        logf("HttpPostFileStream: HttpEndRequest failed\n");
        LogLastError();
        goto Exit;
    }

    HttpQueryInfoW(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                   &respCode, &respCodeSize, nullptr);
    logf("HttpPostFileStream: response code %d\n", (int)respCode);
    ok = (respCode >= 200 && respCode < 300);

Exit:
    if (hReq)  InternetCloseHandle(hReq);
    if (hConn) InternetCloseHandle(hConn);
    if (hInet) InternetCloseHandle(hInet);
    return ok;
}

struct UploadWorkerCtx {
    StrQueue*       queue;
    const char*     serverA;
    int             port;
    const char*     urlA;
    int             chunkSize;
    UploadProgress* progress;
};

static void UploadWorkerThread(UploadWorkerCtx* ctx) {
    for (;;) {
        char* path = ctx->queue->PopFront();
        if (!path) {
            // Stop() 触发，立即退出
            break;
        }
        if (ctx->queue->IsSentinel(path)) {
        //     SetEvent(ctx->queue->hEvent); // 传递 sentinel 给下一个线程
            break;
        }

        // 找到对应的 FileUploadState（按路径匹配）
        FileUploadState* fstate = nullptr;
        for (int i = 0; i < ctx->progress->fileStates.Size(); i++) {
            if (str::Eq(ctx->progress->fileStates[i]->filePath, path)) {
                fstate = ctx->progress->fileStates[i];
                break;
            }
        }
        if (!fstate) {
            free(path);
            continue;
        }

        fstate->isActive = true;

        // 构造单文件进度回调：每上传一块，更新 fstate 和全局 uploadedBytes
        PerChunkCbArgs cbArgs{fstate, ctx->progress};
        auto perChunkCb = MkFunc1(+[](PerChunkCbArgs *args, HttpUploadProgress* p) {
            FileUploadState* fs = args->fstate;
            UploadProgress*  gp = args->gProgress;

            // 本次新增字节数
            i64 delta = p->nUploaded - fs->uploadedBytes.Load();
            if (delta > 0) {
                fs->uploadedBytes.Add(delta);
                gp->uploadedBytes.Add(delta);
                // 触发全局进度回调
                gp->cbProgress.Call(gp);
            }
        }, &cbArgs);

        bool ok = HttpPostFileStream(
            ctx->serverA, ctx->port, ctx->urlA,
            path, 4, ctx->chunkSize, perChunkCb);

        fstate->isActive = false;
        fstate->isDone   = true;
        fstate->isFailed = !ok;

        AtomicIntInc(&ctx->progress->nCompleted);
        if (!ok) AtomicIntInc(&ctx->progress->nFailed);

        // 完成后再触发一次回调，通知文件级状态变化
        ctx->progress->cbProgress.Call(ctx->progress);

        free(path);
    }
    DestroyTempAllocator();
}

int HttpPostFilesStreamPool(
    const char* serverA, int port, const char* urlA,
    const StrVec& filePaths,
    int workerCount, int chunkSize,
    const Func1<UploadProgress*>& cbProgress,
    StrQueue* stopQueue)
{
    int n = filePaths.Size();
    if (n == 0) {
        return 0;
    }
    // 并发数不超过文件数
    if (workerCount > n) {
        workerCount = n;
    }

    StrQueue localQueue;
    StrQueue* queue = stopQueue ? stopQueue : &localQueue;
    UploadProgress* progress = new UploadProgress;
    progress->nTotal     = n;
    progress->totalBytes = 0;
    progress->cbProgress = cbProgress;
    // 预分配每个文件的状态，并统计总字节数
    // progress.files.Reverse();
    for (int i = 0; i < n; i++) {
        // FileUploadState& fs = *progress.fileStates.AppendBlanks(1);
        // FileUploadState* fs = progress->fileStates.AppendBlanks(1);
        auto* fs = new FileUploadState();
        fs->filePath     = filePaths.At(i);
        fs->totalBytes   = file::GetSize(fs->filePath);
        if (fs->totalBytes > 0) {
            progress->totalBytes += fs->totalBytes;
        }
        progress->fileStates.Append(fs);
    }

    // ... 启动线程池、生产者写队列、等待完成（同前）...
    // 启动固定数量的工作线程
    UploadWorkerCtx ctx{};
    ctx.queue     = queue;
    ctx.serverA   = serverA;
    ctx.port      = port;
    ctx.urlA      = urlA;
    ctx.chunkSize = chunkSize;
    ctx.progress  = progress;

    Vec<HANDLE> hThreads;
    for (int i = 0; i < workerCount; i++) {
        auto fn = MkFunc0(UploadWorkerThread, &ctx);
        HANDLE h = StartThread(fn, "HttpUploadWorker");
        if (h) {
            hThreads.Append(h);
        }
    }

    // 生产者：把所有文件路径写入队列
    for (int i = 0; i < n; i++) {
        if (queue->IsStopped()) {
            break;
        }
        queue->Append(filePaths.At(i));
    }
    if (!queue->IsFinished()) {
        queue->MarkFinished();
    }

    // 等待所有工作线程退出
    for (int i = 0; i < hThreads.Size(); i++) {
        HANDLE h = hThreads[i];
        WaitForSingleObject(h, INFINITE);
        SafeCloseHandle(&h);
    }

    return (int)progress->nFailed;
}


/* 调用
HttpPostFilesStreamPool(server, 443, "/api/upload", files, 4, 64*1024,
    MkFunc1([](const UploadProgress* p, void*) {
        // 全局进度
        logf("总进度: %d/%d 文件, %lld/%lld 字节\n",
             p->nCompleted.Load(), p->nTotal,
             p->uploadedBytes.Load(), p->totalBytes);

        // 每个文件的进度
        for (int i = 0; i < p->files.Size(); i++) {
            const FileUploadState& fs = p->files[i];
            if (!fs.isActive && !fs.isDone) continue;
            int pct = (fs.totalBytes > 0)
                ? (int)(fs.uploadedBytes.Load() * 100 / fs.totalBytes)
                : -1;
            logf("  [%s] %s %d%%\n",
                 fs.isDone ? (fs.isFailed ? "FAIL" : "OK") : "...",
                 path::GetBaseNameTemp(fs.filePath),
                 pct);
        }
    }, nullptr),
    queue);

*/
