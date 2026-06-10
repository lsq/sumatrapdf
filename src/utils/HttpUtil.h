/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct StrQueue;
struct HttpRsp {
    AutoFreeStr url;
    StrBuilder data;
    DWORD error = (DWORD)-1;
    DWORD httpStatusCode = (DWORD)-1;

    HttpRsp() = default;
};

struct HttpProgress {
    i64 nDownloaded;
};

bool IsHttpRspOk(const HttpRsp*);

bool HttpPost(const char* server, int port, const char* url, StrBuilder* headers, StrBuilder* data);
bool HttpGet(const char* url, HttpRsp* rspOut);
bool HttpGetToFile(const char* url, const char* destFilePath, const Func1<HttpProgress*>& cbProgress);

struct HttpUploadProgress {
    i64 nUploaded;
    i64 totalSize;
};

// 单个文件的上传状态
struct FileUploadState {
    char* filePath = nullptr;          // 文件路径（不拥有，指向 StrVec 中的字符串）
    i64   totalBytes = 0;        // 文件总大小（-1 表示未知）
    AtomicInt64 uploadedBytes; // 已上传字节数（原子，工作线程直接更新）
    volatile bool isActive = false;  // 正在上传中
    volatile bool isDone = false;    // 已完成（成功或失败）
    volatile bool isFailed = false;  // 是否失败
};

// 全局进度（扩展之前的 UploadProgress）
struct UploadProgress {
    // --- 全局统计 ---
    int   nTotal = 0;            // 总文件数
    AtomicInt nCompleted = 0;    // 已完成（成功+失败）
    AtomicInt nFailed = 0;       // 失败数
    i64   totalBytes = 0;        // 所有文件总字节数（预先统计）
    AtomicInt64 uploadedBytes; // 全局已上传字节数

    // --- 每个文件的进度 ---
    // 由主线程预先分配（files.Size() == nTotal），工作线程只写不增删
    // 无需互斥，因为每个 FileUploadState 只由一个工作线程写入
    Vec<FileUploadState*> fileStates;

    // --- 进度回调（在 UI 线程或调用方线程中调用）---
    Func1<UploadProgress*> cbProgress;

    UploadProgress() = default;
    ~UploadProgress() {
        for (int i = 0; i < fileStates.Size(); i++) {
            delete fileStates[i];
        }
    }
};

// 以流的形式将文件 POST 到 WebAPI（有界并发队列驱动）
// server:         主机名，如 "api.example.com"
// port:           端口，如 80 或 443
// urlPath:        请求路径，如 "/upload"
// filePath:       本地文件路径
// maxQueueChunks: 队列最大块数（有界限制，控制内存占用）
// chunkSize:      每块字节数
// cbProgress:     进度回调（可传空 Func1）
bool HttpPostFileStream(const char* server, int port, const char* urlPath,
                        const char* filePath,
                        int maxQueueChunks,//= 4, // 4 - 8
                        int chunkSize,//= 64 * 1024, // 64kb
                        Func1<HttpUploadProgress*>& cbProgress);

// 固定线程池多文件并发上传
// filePaths: 文件路径列表
// workerCount: 工作线程数（并发数），默认4
// 返回：失败的文件数（0 表示全部成功）
int HttpPostFilesStreamPool(
    const char* serverA,
    int port,
    const char* urlA,
    const StrVec& filePaths,
    int workerCount = 4,
    int chunkSize =  64 * 1024,
    // UploadProgress* progress = nullptr,
    Func1<UploadProgress*> cbProgress = {},
    StrQueue* stopQueue = nullptr);


