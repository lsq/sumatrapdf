/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "Base.h"
#include "StrQueue.h"
#include <synchapi.h>

StrQueue::StrQueue() {
    InitializeCriticalSection(&cs);
    BOOL manualReset = FALSE;
    BOOL initialState = FALSE;
    // hEvent: Auto-Reset，用于单播唤醒（有新数据）
    hEvent = CreateEventW(nullptr /* SECURITY_ATTRIBUTES* */, manualReset, initialState, nullptr /* name */);
    // hStopEvent: Manual-Reset，用于广播唤醒（停止或完成）
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

// 【修复1】MarkFinished 增加 hStopEvent 广播，确保所有等待的消费者都能被唤醒退出
void StrQueue::MarkFinished() {
    Lock();
    ReportIf(isFinished);
    isFinished = true;
    Unlock();
    SetEvent(hEvent);              // 唤醒第一个消费者（传递 Sentinel）
    SetEvent(hStopEvent);  // 广播唤醒所有剩余等待的消费者
}

bool StrQueue::IsFinished() {
    Lock();
    auto res = isFinished;
    Unlock();
    return res;
}

int len(StrQueue& q) {
    q.Lock();
    int res = len(q.strings);
    q.Unlock();
    return res;
}

Str StrQueue::append(Str s) {
    Lock();
    auto res = strings.Append(s);
    Unlock();
    SetEvent(hEvent);
    return res;
}

constexpr uintptr_t kStrQueueSentinel = (uintptr_t)-2;

static Str StrQueueSentinel() {
    return Str((char*)kStrQueueSentinel, 0);
}

bool StrQueue::IsSentinel(Str s) {
    return s.s == (char*)kStrQueueSentinel;
}

void StrQueue::Stop() {
    Lock();
    isStopped = true;
    Unlock();
    // 手动重置事件：SetEvent 后永久触发，所有等待线程同时被唤醒
    SetEvent(hStopEvent);
    SetEvent(hEvent); // 额外唤醒，防止有线程刚好在 Wait 之前检查了 isStopped 但还未进入 Wait
}

bool StrQueue::IsStopped() {
    return isStopped;
}

// is blocking
// retuns sentinel value if no more strings
// use IsSentinel() to check if returned value is a sentinel
Str StrQueue::PopFront() {
    HANDLE waitHandles[] = {hEvent, hStopEvent};
again:
    Lock();
    if (isStopped) {
        Unlock();
        return nullptr;
    }
    if (len(strings) == 0) {
        bool end = isFinished;
        Unlock();
        if (end) {
            return StrQueueSentinel();
        }

        // 等待 hEvent(新数据) 或 hStopEvent(停止/完成广播)
        DWORD res = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (res == WAIT_OBJECT_0 + 1) {
            // hStopEvent 被触发（可能是 Stop，也可能是 MarkFinished）
            // 必须重新加锁检查具体原因，不能直接返回 nullptr
            // return nullptr;
            goto again;
        }

        // hEvent 被触发，重新加锁检查数据
        // WaitForSingleObject(hEvent, INFINITE);
        goto again;
    }

    if (isStopped) {
        Unlock();
        return nullptr;
    }
    // 有数据，取出
    Str s = strings.RemoveAt(0);
    Unlock();
    return s;
}

// is blocking
// returns false if we finished i.e. there was no more strings to access
// and we finished adding. In that case fn was called
// calls fn() and returns false if there are strings available
// 【修复5】Access 增加对 hStopEvent 的监听，防止 Stop() 时死锁
bool StrQueue::Access(const Func1<StrQueue*>& fn) {
    HANDLE waitHandles[] = {hEvent, hStopEvent};
again:
    Lock();
    if (isStopped) {
        Unlock();
        return false; // 已停止，直接返回
    }

    if (len(strings) == 0) {
        bool end = isFinished;
        Unlock();
        if (end) {
            return false;
        }

        // WaitForSingleObject(hEvent, INFINITE);
        // 同时监听 hEvent 和 hStopEvent
        DWORD res = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (res == WAIT_OBJECT_0 + 1) {
            // hStopEvent 触发，可能是 Stop 或 MarkFinished，重新检查
            goto again;
        }
        goto again;
    }
    fn.Call(this);
    Unlock();
    return true;
}
