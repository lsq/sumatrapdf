#include "base/Base.h"
#include "base/client/UploadSession.h"
// #include "base/StrUtil.h"
#include "base/StrQueue.h"
#include "base/Dict.h"
// #include "base/Thread.h"
#include "base/net/IHttpClient.h"
#include "base/net/HttpClient.h"
#include "base/protocol/Constants.h"
#include "base/protocol/Models.h"
#include "base/Win.h"

namespace LocalSend {

bool SendReport::AllSent() const {
    if (len(files) == 0) return false;
    for (const auto& f : files) {
        if (f.status != FileOutcome::Status::Sent) return false;
    }
    return true;
}

UploadSession::UploadSession(IHttpClient& http, DeviceInfo* info) : fHttp(http), fInfo(info) {}

// Separa file accettati e rifiutati.
struct UploadTask {
    const FileMetadata* file = nullptr;
    Str token;
    FileOutcome outcome;
};

const char* StatusText(FileOutcome::Status s) {
    switch (s) {
        case FileOutcome::Status::Sent:
            return "SENT";
        case FileOutcome::Status::Rejected:
            return "REJECTED";
        case FileOutcome::Status::Error:
            return "ERRORE";
    }
    return "?";
}

struct UploadWorkerCtx {
    StrQueue* queue;
    Str sessionId;
    Str host;
    int port;
    int chunkSize;
    UploadProgress* uprogress;
    Vec<UploadTask>* uts;
    dict::MapStrToInt* tasksSet;
};

static void UploadWorkerThread(UploadWorkerCtx* t) {
    for (;;) {
        if (t->uprogress->cancelled) {
            break; // 用户取消：队列里剩下的文件不再上传
        }
        Str path = t->queue->PopFront();
        if (len(path) == 0) {
            // Stop() 触发，立即退出
            break;
        }
        if (t->queue->IsSentinel(path)) {
            //     SetEvent(ctx->queue->hEvent); // 传递 sentinel 给下一个线程
            break;
        }

        int tsi = 0;
        bool getOr = t->tasksSet->Get(path, &tsi);
        if (!getOr) {
            printf("tasksSet cannot find path: %s\n", path.s);
            // str::Free(path);
            continue;
        }
        UploadTask* ts = &((*t->uts)[tsi]);

        // 找到对应的 FileUploadState（按路径匹配）
        FileUploadState* fstate = nullptr;
        for (int i = 0; i < len(t->uprogress->fileStates); i++) {
            if (str::Eq(Str(t->uprogress->fileStates[i]->filePath), path)) {
                fstate = t->uprogress->fileStates[i];
                break;
            }
        }
        if (!fstate) {
            // str::Free(path);
            continue;
        }

        fstate->isActive = true;

        TempStr tToken = url::UrlEncodeTemp((ts->token).s);
        TempStr tSessionId = url::UrlEncodeTemp((t->sessionId).s);
        TempStr tFileId = url::UrlEncodeTemp((ts->file->id).s);
        TempStr urlPath = fmt("%s?sessionId=%s&fileId=%s&token=%s", TempStr(kApiUpload), tSessionId, tFileId, tToken);

        PerChunkCbArgs cbArgs{fstate, t->uprogress};
        auto perChunkCb = MkFunc1(
            +[](PerChunkCbArgs* args, HttpUploadProgress* p) {
                FileUploadState* fs = args->fstate;
                UploadProgress* gp = args->gProgress;

                // 本次新增字节数
                i64 delta = p->nUploaded - fs->uploadedBytes.Load();
                if (delta > 0) {
                    fs->uploadedBytes.Add(delta);
                    gp->uploadedBytes.Add(delta);
                    // 触发全局进度回调
                    gp->cbProgress.Call(gp);
                }
                if (gp->cancelled) {
                    p->abort = true; // 中止正在上传的这个文件
                }
            },
            &cbArgs);
        // bool bugO = _CrtCheckMemory();
        HttpClient http;
        str::Builder header;
        header.Append(fmt("Content-Type: %s\r\n", ts->file->fileType));
        HttpResponse up;
        http.PostFile(t->host, t->port, urlPath, &header, ts->file->localPath, 4, t->chunkSize, perChunkCb, &up);
        bool ok = up.IsOk();
        if (ok) {
            ts->outcome.status = FileOutcome::Status::Sent;
            // sentBytes.Add(ts->file->size);
        } else {
            ts->outcome.status = FileOutcome::Status::Error;
            // detail 之后会被 str::Free，必须是堆上的副本（不能是 StrL 字面量）
            if (t->uprogress->cancelled) {
                ts->outcome.detail = str::Dup(StrL("cancelled"));
            } else if (up.httpStatusCode == -1) {
                ts->outcome.detail = str::Dup(StrL("connessione/IO fallita"));
            } else {
                ts->outcome.detail = str::Dup(fmt("HTTP %d", (int)up.httpStatusCode));
            }
            // anyError = true;
        }

        fstate->isActive = false;
        fstate->isDone = true;
        fstate->isFailed = !ok;

        AtomicIntInc(&t->uprogress->nCompleted);
        if (!ok) AtomicIntInc(&t->uprogress->nFailed);

        // 完成后再触发一次回调，通知文件级状态变化
        t->uprogress->cbProgress.Call(t->uprogress);

        // str::Free(path);
    }
    DestroyTempArena();
};

void UploadSession::Cancel(Str host, int port, Str sessionId) {
    // cancel 没有请求体（sessionId 在 query 里），所以也不发 Content-Type
    str::Builder body;
    TempStr path = fmt("%s?sessionId=%s", TempStr(kApiCancel), url::UrlEncodeTemp(sessionId.s));
    fHttp.Post(host, port, path, nullptr, &body, nullptr);
    // path 是 TempStr（临时 arena），不能 str::Free
}

SendReport UploadSession::Send(Str host, int port, int workerCount, int chunkSize,
                               const StrVecWithData<FileMetadata*>* files, // const std::string& pin,
                               UploadProgress* progressIn) {
    SendReport report;

    // Passo 1: prepare-upload (solo metadati).
    TempStr preparePath = str::DupTemp(Str(kApiPrepareUpload));
    // if (!pin.empty())
    // 	preparePath += "?pin=" + UrlEncode(pin);

    char bbuf[512]{};
    str::Builder body;
    body.UseExternalBuffer(Str(bbuf, sizeofi(bbuf)));
    body.Append(BuildPrepareUpload(fInfo, files));
    logf("BuildPrepareUpload(json): %s\n", ToStrTemp(body));
    char scratch[512]{};
    str::Builder headers;
    headers.UseExternalBuffer(Str(scratch, sizeofi(scratch)));
    headers.Append(StrL("Content-Type: application/json\r\nAccept: application/json\r\n"));
    HttpResponse resp;
    fHttp.Post(host, port, preparePath, &headers, &body, &resp);
    report.prepareStatus = (int)resp.httpStatusCode;

    if (!resp.IsOk()) {
        // 204 = Finished (No file transfer needed);
        // 400 Invalid body
        // 401 PIN required/Invalid PIN
        // 403 = Rejected
        // 409 = Blocked by another session
        // 429 = Too many requests
        // 500 = Unknown error by receiver

        return report;
    }

    PrepareUploadResult prep;
    TempStr uploadData = ToStrTemp(resp.data);
    logf("BuildPrepareUpload(response data): %s\n", uploadData);
    // printf("UploadSession::Send uploadData:%s\n", uploadData.s);
    if (!ParsePrepareUploadResponse(uploadData, &prep)) {
        return report;
    };
    // for (int t = 0; t < len(prep.fileTokens); t++) {
    //     logf("ParsePrepareUploadResponse: %s\n", *prep.fileTokens.AtData(t));
    // }
    report.prepared = true;
    report.sessionId = str::Dup(prep.sessionId);

    StrQueue localQueue;
    StrQueue* queue = &localQueue;
    UploadProgress localProgress;
    UploadProgress* uprogress = progressIn ? progressIn : &localProgress;
    uprogress->totalBytes = 0;

    // Calcola il totale dei byte accettati per il progresso.
    // long long totalBytes = 0;
    // {
    // int idx = 0;
    // for (const auto& f : *files) {
    //     int i = prep.fileTokens.Find(f);
    // 	if (i > -1) {
    //         FileMetadata* m = *files->AtData(idx);
    //         i64 sz =  m->size;
    // 		uprogress->totalBytes += sz;
    //         uprogress->nTotal     += 1;
    //     }
    //     idx++;
    // }
    // }

    Vec<UploadTask> tasks;
    dict::MapStrToInt tsSet;
    StrVec filePaths;
    // dict::MapStrToInt tasksSet;
    // for (const auto& it : *files) {
    for (int i = 0, j = 0; i < len(*files); i++) {
        Str it = files->At(i);
        FileMetadata* f = *files->AtData(i);

        int idx = prep.fileTokens.Find(it);
        if (idx == -1) {
            FileOutcome out;
            out.fileId = f->id;
            out.fileName = f->fileName;
            out.status = FileOutcome::Status::Rejected;
            out.detail = str::Dup(StrL("Not accepted by the recipient"));
            VecAppend(report.files, out);
        } else {
            UploadTask t;
            t.file = f;
            t.token = *prep.fileTokens.AtData(idx);
            // t.outcome = new FileOutcome();
            t.outcome.fileId = f->id;
            t.outcome.fileName = f->fileName;

            i64 sz = f->size;
            uprogress->nTotal += 1;
            auto* fs = new FileUploadState();
            fs->filePath = str::Dup(f->localPath).s;
            fs->totalBytes = sz;
            if (fs->totalBytes > 0) {
                uprogress->totalBytes += sz;
            }
            VecAppend(uprogress->fileStates, fs);

            VecAppend(tasks, t);
            filePaths.Append(f->localPath);
            tsSet.Insert(f->localPath, j);
            j++;
        }
    }

    // Passo 2: upload parallelo. Ogni thread crea la propria connessione.
    // AtomicInt64 sentBytes(0);
    // bool anyError = false;

    int n = len(tasks);
    if (n == 0) return report;

    if (workerCount > n) {
        workerCount = n;
    }

    UploadWorkerCtx ctx{};
    ctx.queue = queue;
    ctx.host = host;
    ctx.port = port;
    ctx.chunkSize = chunkSize;
    ctx.uprogress = uprogress;
    ctx.sessionId = prep.sessionId;
    ctx.uts = &tasks;
    ctx.tasksSet = &tsSet;

    Vec<HANDLE> hThreads;
    for (int i = 0; i < workerCount; i++) {
        auto fn = MkFunc0(UploadWorkerThread, &ctx);
        HANDLE h = StartThread(fn, StrL("HttpUploadWorker"));
        if (h) {
            VecAppend(hThreads, h);
        }
    }

    // 生产者：把所有文件路径写入队列
    for (int i = 0; i < n; i++) {
        queue->append(filePaths.At(i));
    }
    if (!queue->IsFinished()) {
        queue->MarkFinished();
    }

    // 等待所有工作线程退出
    for (int i = 0; i < len(hThreads); i++) {
        HANDLE h = hThreads[i];
        WaitForSingleObject(h, INFINITE);
        SafeCloseHandle(&h);
    }

    // return (int)progress->nFailed;

    if (uprogress->cancelled) {
        for (auto& t : tasks) {
            if (t.outcome.status != FileOutcome::Status::Sent && len(t.outcome.detail) == 0) {
                t.outcome.detail = str::Dup(StrL("cancelled"));
            }
        }
        // 通知接收端结束这个会话
        Cancel(host, port, prep.sessionId);
    }

    for (auto& t : tasks) VecAppend(report.files, t.outcome);

    // Passo 3: in caso di errore a meta' sessione, annulla.
    // if (!str::IsEmpty(prep.sessionId))
    // Cancel(host, port, prep.sessionId);

    return report;
}

} // namespace LocalSend
