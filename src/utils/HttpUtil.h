/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */
#include "utils/WinUtil.h"

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

// -----------------------------------------------------------------------
// 有界并发字节块队列(信号量实现)
// -----------------------------------------------------------------------
#if 0
struct BoundedByteQueueOld {
    struct Chunk {
        char* data = nullptr;
        int   len  = 0;
    };

    CRITICAL_SECTION cs;
    HANDLE hSemSlots;   // 信号量：可用槽位数（初始 = maxChunks）
    HANDLE hEventData;  // 自动重置事件：队列非空时触发
    Vec<Chunk> chunks;
    bool finished = false;

    explicit BoundedByteQueueOld(int maxChunks) {
        InitializeCriticalSection(&cs);
        hSemSlots  = CreateSemaphoreW(nullptr, maxChunks, maxChunks, nullptr);
        hEventData = CreateEventW(nullptr, /*manualReset=*/FALSE, /*initial=*/FALSE, nullptr);
    }

    ~BoundedByteQueueOld() {
        DeleteCriticalSection(&cs);
        CloseHandle(hSemSlots);
        CloseHandle(hEventData);
    }

    // 生产者：阻塞直到有空槽，然后入队（复制数据）
    // 返回 false 表示队列已被标记结束，不应再 Push
    bool Push(const char* src, int len) {
        WaitForSingleObject(hSemSlots, INFINITE); // 等待空槽
        EnterCriticalSection(&cs);
        if (finished) {
            LeaveCriticalSection(&cs);
            ReleaseSemaphore(hSemSlots, 1, nullptr);
            return false;
        }
        Chunk c;
        c.data = (char*)malloc(len);
        memcpy(c.data, src, len);
        c.len = len;
        chunks.Append(c);
        LeaveCriticalSection(&cs);
        SetEvent(hEventData); // 通知消费者
        return true;
    }

    // 生产者：标记生产结束
    void MarkFinished() {
        EnterCriticalSection(&cs);
        finished = true;
        LeaveCriticalSection(&cs);
        SetEvent(hEventData); // 唤醒可能阻塞的消费者
    }

    // 消费者：阻塞直到有数据或结束
    // 返回 false 表示队列已空且已结束
    bool Pop(Chunk& out) {
    again:
        EnterCriticalSection(&cs);
        if (chunks.Size() > 0) {
            out = chunks.PopAt(0);
            LeaveCriticalSection(&cs);
            ReleaseSemaphore(hSemSlots, 1, nullptr); // 释放一个槽位
            return true;
        }
        bool done = finished;
        LeaveCriticalSection(&cs);
        if (done) {
            return false;
        }
        WaitForSingleObject(hEventData, INFINITE);
        goto again;
    }
};
#endif

// ---- 有界字节块队列（生产者-消费者，用于单文件流式上传）----

struct ByteChunk {
    char* data = nullptr;
    DWORD len = 0;
};

struct BoundedByteQueue {
    int maxChunks;
    CRITICAL_SECTION cs;
    HANDLE hNotEmpty; // 消费者等待：队列非空（自动重置）
    HANDLE hNotFull;  // 生产者等待：队列未满（信号量）
    Vec<ByteChunk> chunks;
    bool finished = false;

    explicit BoundedByteQueue(int maxChunks) : maxChunks(maxChunks) {
        InitializeCriticalSection(&cs);
        hNotEmpty = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        hNotFull = CreateSemaphoreW(nullptr, maxChunks, maxChunks, nullptr);
    }
    ~BoundedByteQueue() {
        DeleteCriticalSection(&cs);
        SafeCloseHandle(&hNotEmpty);
        SafeCloseHandle(&hNotFull);
        for (int i = 0; i < chunks.Size(); i++) {
            free(chunks[i].data);
        }
    }
    // 生产者：阻塞直到有空位
    bool Push(const char* data, DWORD len) {
        WaitForSingleObject(hNotFull, INFINITE);
        EnterCriticalSection(&cs);
        if (finished) {
            LeaveCriticalSection(&cs);
            ReleaseSemaphore(hNotFull, 1, nullptr);
            return false;
        }
        ByteChunk c;
        c.data = (char*)memdup(data, len);
        c.len = len;
        chunks.Append(c);
        LeaveCriticalSection(&cs);
        SetEvent(hNotEmpty);
        return true;
    }

    void MarkFinished() {
        // 在 MarkFinished() 中：
        // logf("MarkFinished called at %llu\n", GetTickCount64());
        EnterCriticalSection(&cs);
        finished = true;
        // 加上这行日志
        // logf("debug: MarkFinished called. hNotEmpty handle = %p\n", hNotEmpty);
        SetEvent(hNotEmpty);
        LeaveCriticalSection(&cs);
    }

    // 消费者：阻塞直到有数据，返回 false 表示结束
    bool Pop(ByteChunk& out) {
        // again:
        while (true) {
            // logf("Pop entered at %llu\n", GetTickCount64());
            WaitForSingleObject(hNotEmpty, INFINITE);
            EnterCriticalSection(&cs);
            // if (chunks.Size() == 0) {
            //     bool done = finished;
            //     LeaveCriticalSection(&cs);
            //     if (done) return false;
            //     // goto again;
            // }

            if (chunks.Size() > 0) {
                out = chunks.PopAt(0);
                // bool needRelease = (chunks.Size() < maxChunks);
                LeaveCriticalSection(&cs);
                // if (needRelease)
                ReleaseSemaphore(hNotFull, 1, nullptr);
                return true;
            }

            if (finished) {
                LeaveCriticalSection(&cs);
                return false;
            }

            LeaveCriticalSection(&cs);
            // WaitForSingleObject(hNotEmpty, INFINITE);
        }
    }
};

// -----------------------------------------------------------------------
// 生产者线程：读取文件，分块写入有界队列
// -----------------------------------------------------------------------
struct FileReaderData {
    BoundedByteQueue* queue;
    char* filePath;
    int chunkSize;
    bool* errorOut;
};

// 单个文件的上传状态
struct FileUploadState {
    char* filePath = nullptr;       // 文件路径（不拥有，指向 StrVec 中的字符串）
    i64 totalBytes = 0;             // 文件总大小（-1 表示未知）
    AtomicInt64 uploadedBytes;      // 已上传字节数（原子，工作线程直接更新）
    volatile bool isActive = false; // 正在上传中
    volatile bool isDone = false;   // 已完成（成功或失败）
    volatile bool isFailed = false; // 是否失败
};

// 全局进度（扩展之前的 UploadProgress）
struct UploadProgress {
    // --- 全局统计 ---
    int nTotal = 0;            // 总文件数
    AtomicInt nCompleted = 0;  // 已完成（成功+失败）
    AtomicInt nFailed = 0;     // 失败数
    i64 totalBytes = 0;        // 所有文件总字节数（预先统计）
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

struct PerChunkCbArgs {
    FileUploadState* fstate;   // 指向当前文件的上传状态
    UploadProgress* gProgress; // 指向全局上传进度状态
};

void FileReaderThread(FileReaderData* td);

// 以流的形式将文件 POST 到 WebAPI（有界并发队列驱动）
// server:         主机名，如 "api.example.com"
// port:           端口，如 80 或 443
// urlPath:        请求路径，如 "/upload"
// filePath:       本地文件路径
// maxQueueChunks: 队列最大块数（有界限制，控制内存占用）
// chunkSize:      每块字节数
// cbProgress:     进度回调（可传空 Func1）
bool HttpPostFileStream(const char* server, int port, const char* urlPath, const char* filePath,
                        int maxQueueChunks, //= 4, // 4 - 8
                        int chunkSize,      //= 64 * 1024, // 64kb
                        Func1<HttpUploadProgress*>& cbProgress);

// 固定线程池多文件并发上传
// filePaths: 文件路径列表
// workerCount: 工作线程数（并发数），默认4
// 返回：失败的文件数（0 表示全部成功）
int HttpPostFilesStreamPool(const char* serverA, int port, const char* urlA, const StrVec& filePaths,
                            int workerCount = 4, int chunkSize = 64 * 1024,
                            // UploadProgress* progress = nullptr,
                            const Func1<UploadProgress*>& cbProgress = {}, StrQueue* stopQueue = nullptr);
