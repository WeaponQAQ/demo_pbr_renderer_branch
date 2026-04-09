#pragma once

#include "rhi/rhi.h"
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>

// -----------------------------------------------------------------------
//  RenderThread — worker-thread command recording + GL-thread replay
//
//  Two submission modes:
//
//  A. Single-flight (synchronized, frame 0 warmup):
//
//       Main               Worker
//       kick(func) ──────> record into cmdLists_[write]
//       submit(ctx) ─wait─ (recording…)
//                   <────  done
//                   GL replay → swap buffers internally
//
//  B. Double-buffered (frame 1+, true recording/replay overlap):
//
//       Main                           Worker
//       kickAsync(func) ─notify──────> record into cmdLists_[write]
//       replayPrevious(ctx)            (recording…) ← OVERLAP
//       sync()  ─wait if needed──────  done
//       swapBuffers()
//
//  Buffer layout:
//       writeIdx_ : worker always records into cmdLists_[writeIdx_]
//       readIdx_  : main replays from cmdLists_[readIdx_] (previous frame)
//       swapBuffers() / submit() rotate the two indices each frame.
//
//  Constraints (assert-guarded in debug builds):
//    1. kick() / kickAsync() must not be called while a previous
//       recording is still in flight.
//    2. submit() must follow kick(); replayPrevious+sync+swapBuffers
//       must follow kickAsync() in that order.
// -----------------------------------------------------------------------

class RenderThread {
public:
    using RecordFunc = std::function<void(RHICommandList*)>;

    explicit RenderThread(RHIDevice* device);
    ~RenderThread();

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;

    // ── Mode A: Single-flight (synchronized) ──────────────────────────
    // Enqueue recording; blocks in submit() until done, then replays.
    // Also promotes the write buffer to "previous" for Mode B.
    void kick(RecordFunc func);
    void submit(RHIContext* ctx);

    // ── Mode B: Double-buffered ────────────────────────────────────────
    // kickAsync() is non-blocking; the worker records into the write
    // buffer while the main thread replays the previous (read) buffer.
    void kickAsync(RecordFunc func);

    // Replay the previous frame's command list on the GL thread.
    // Safe to call concurrently with the worker (different buffer).
    // Returns false if no completed frame exists yet (skip gracefully).
    bool replayPrevious(RHIContext* ctx);

    // Block until the current kickAsync recording is done.
    void sync();

    // Promote write → read; call after sync().
    // Unlocks the next kickAsync.
    void swapBuffers();

    // Number of frames that have completed kick+submit or kickAsync+sync+swap.
    int completedFrames() const { return completedFrames_; }

private:
    void enqueue(RecordFunc func);  // shared dispatch used by kick + kickAsync
    void workerLoop();

    static constexpr int NUM_BUFS = 2;
    std::unique_ptr<RHICommandList> cmdLists_[NUM_BUFS];
    int writeIdx_  = 0;   // buffer index worker records into
    int readIdx_   = 1;   // buffer index main replays from

    std::thread             worker_;
    std::mutex              mutex_;
    std::condition_variable cvWork_;
    std::condition_variable cvDone_;
    RecordFunc              pendingFunc_;
    int                     pendingWriteIdx_ = 0;
    bool                    hasWork_         = false;
    bool                    recordDone_      = false;
    bool                    quit_            = false;

    // Tracks an in-flight kick/kickAsync. Main-thread only — no mutex needed.
    bool inFlight_        = false;
    int  completedFrames_ = 0;
};
