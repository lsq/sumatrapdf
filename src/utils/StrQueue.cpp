/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "BaseUtil.h"
#include "StrQueue.h"

StrQueue::StrQueue() {
    InitializeCriticalSection(&cs);
    BOOL manualReset = FALSE;
    BOOL initialState = FALSE;
    hEvent = CreateEventW(nullptr /* SECURITY_ATTRIBUTES* */, manualReset, initialState, nullptr /* name */);
    hStopEvent = CreateEventW(nullptr /* SECURITY_ATTRIBUTES* */, TRUE, initialState, nullptr /* name */);
}

StrQueue::~StrQueue() {
    DeleteCriticalSection(&cs);
    CloseHandle(hEvent);
    CloseHandle(hStopEvent);
}

void StrQueue::Lock() {
    EnterCriticalSection(&cs);
}

void StrQueue::Unlock() {
    LeaveCriticalSection(&cs);
}

void StrQueue::MarkFinished() {
    Lock();
    ReportIf(isFinished);
    isFinished = true;
    Unlock();
    SetEvent(hEvent);
}

bool StrQueue::IsFinished() {
    Lock();
    auto res = isFinished;
    Unlock();
    SetEvent(hStopEvent);
    return res;
}

int StrQueue::Size() {
    Lock();
    auto res = strings.Size();
    Unlock();
    return res;
}

char* StrQueue::Append(const char* s, int len) {
    Lock();
    auto res = strings.Append(s, len);
    Unlock();
    SetEvent(hEvent);
    return res;
}

constexpr uintptr_t kStrQueueSentinel = (uintptr_t)-2;

bool StrQueue::IsSentinel(char* s) {
    return s == (char*)kStrQueueSentinel;
}

void StrQueue::Stop() {
    isStopped = true;
    // 手动重置事件：SetEvent 后永久触发，所有等待线程同时被唤醒
    SetEvent(hStopEvent);
    SetEvent(hEvent); // 额外唤醒一次，防止有线程刚进入等待
}

bool StrQueue::IsStopped() {
    return isStopped;
}

// is blocking
// retuns sentinel value if no more strings
// use IsSentinel() to check if returned value is a sentinel
char* StrQueue::PopFront() {
    HANDLE waitHandles[] = {hEvent, hStopEvent};
again:
    if (isStopped) {
        return nullptr;
    }
    Lock();
    if (strings.Size() == 0) {
        bool end = isFinished;
        Unlock();
        if (end) {
            return (char*)kStrQueueSentinel;
        }
        DWORD res = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (res == WAIT_OBJECT_0 + 1) {
            return nullptr;
        }
        // WaitForSingleObject(hEvent, INFINITE);
        goto again;
    }
    if (isStopped) {
        Unlock();
        return nullptr;
    }
    char* s = strings.RemoveAt(0);
    Unlock();
    return s;
}

// is blocking
// returns false if we finished i.e. there was no more strings to access
// and we finished adding. In that case fn was called
// calls fn() and returns false if there are strings available
bool StrQueue::Access(const Func1<StrQueue*>& fn) {
again:
    Lock();
    if (strings.Size() == 0) {
        bool end = isFinished;
        Unlock();
        if (end) {
            return false;
        }
        WaitForSingleObject(hEvent, INFINITE);
        goto again;
    }
    fn.Call(this);
    Unlock();
    return true;
}
