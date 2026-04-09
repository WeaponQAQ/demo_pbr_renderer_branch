#include "rhi/render_thread.h"
#include <cassert>
#include <utility>

// ============================================================
//  Lifecycle
// ============================================================

RenderThread::RenderThread(RHIDevice* device)
{
    for (auto& cl : cmdLists_)
        cl = device->createCommandList();
    worker_ = std::thread(&RenderThread::workerLoop, this);
}

RenderThread::~RenderThread()
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        quit_ = true;
    }
    cvWork_.notify_one();
    if (worker_.joinable())
        worker_.join();
}

// ============================================================
//  Internal dispatch (shared by kick + kickAsync)
// ============================================================

void RenderThread::enqueue(RecordFunc func)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        pendingFunc_     = std::move(func);
        pendingWriteIdx_ = writeIdx_;
        hasWork_         = true;
        recordDone_      = false;
    }
    cvWork_.notify_one();
}

// ============================================================
//  Mode A — Single-flight (synchronized)
// ============================================================

void RenderThread::kick(RecordFunc func)
{
    assert(!inFlight_ && "kick() called before the previous submit() returned");
    inFlight_ = true;
    enqueue(std::move(func));
}

void RenderThread::submit(RHIContext* ctx)
{
    assert(inFlight_ && "submit() called without a preceding kick()");

    {
        std::unique_lock<std::mutex> lk(mutex_);
        cvDone_.wait(lk, [this] { return recordDone_ || quit_; });
        if (quit_) return;
    }
    ctx->submit(cmdLists_[writeIdx_].get());

    // Promote the just-recorded buffer so replayPrevious() can use it next frame.
    std::swap(writeIdx_, readIdx_);
    ++completedFrames_;
    inFlight_ = false;
}

// ============================================================
//  Mode B — Double-buffered (recording/replay overlap)
// ============================================================

void RenderThread::kickAsync(RecordFunc func)
{
    assert(!inFlight_ && "kickAsync() called before swapBuffers() of previous frame");
    inFlight_ = true;
    enqueue(std::move(func));
}

bool RenderThread::replayPrevious(RHIContext* ctx)
{
    assert(inFlight_ && "replayPrevious() called without a preceding kickAsync()");
    if (completedFrames_ == 0) return false;

    // readIdx_ is the PREVIOUS completed frame — safe to replay while
    // the worker records into writeIdx_ (different buffer).
    ctx->submit(cmdLists_[readIdx_].get());
    return true;
}

void RenderThread::sync()
{
    assert(inFlight_ && "sync() called without a preceding kickAsync()");
    std::unique_lock<std::mutex> lk(mutex_);
    cvDone_.wait(lk, [this] { return recordDone_ || quit_; });
}

void RenderThread::swapBuffers()
{
    assert(inFlight_ && "swapBuffers() called without a preceding kickAsync()");
    // writeIdx_ now holds the just-recorded frame; promote it to "previous".
    std::swap(writeIdx_, readIdx_);
    ++completedFrames_;
    inFlight_ = false;
}

// ============================================================
//  Worker loop
// ============================================================

void RenderThread::workerLoop()
{
    while (true) {
        RecordFunc func;
        int wIdx;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cvWork_.wait(lk, [this] { return hasWork_ || quit_; });
            if (quit_) return;
            func     = std::move(pendingFunc_);
            wIdx     = pendingWriteIdx_;
            hasWork_ = false;
        }

        cmdLists_[wIdx]->reset();
        func(cmdLists_[wIdx].get());

        {
            std::lock_guard<std::mutex> lk(mutex_);
            recordDone_ = true;
        }
        cvDone_.notify_one();
    }
}
