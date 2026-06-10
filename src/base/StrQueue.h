/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// multi-threaded queue of strings
struct StrQueue {
    StrQueue();
    ~StrQueue();

    void Lock();
    void Unlock();
    Str append(Str s);
    Str PopFront();
    bool IsSentinel(Str s);
    void MarkFinished();
    void Stop();
    bool IsStopped();
    bool IsFinished();
    bool Access(const Func1<StrQueue*>& fn);

    StrVec strings;

    volatile bool isFinished = false;
    volatile bool isStopped = false;
    CRITICAL_SECTION cs;
    HANDLE hEvent = nullptr;
    HANDLE hStopEvent = nullptr; //新增：手动重置事件
};

int len(StrQueue& q);
