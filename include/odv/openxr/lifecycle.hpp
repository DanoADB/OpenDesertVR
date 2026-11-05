// SPDX-License-Identifier: MIT
#pragma once
#include "odv/core/core.hpp"
namespace odv::xr {
enum class State { idle, ready, running, stopping, lost, exiting };
class Lifecycle {
    State state_ = State::idle;

  public:
    void ready() {
        if (state_ != State::idle)
            throw Failure(ErrorCode::session, "invalid READY transition");
        state_ = State::ready;
    }
    void begin() {
        if (state_ != State::ready)
            throw Failure(ErrorCode::session, "session not READY");
        state_ = State::running;
    }
    void stop() {
        if (state_ != State::running)
            throw Failure(ErrorCode::session, "session not running");
        state_ = State::stopping;
    }
    void end() {
        if (state_ != State::stopping)
            throw Failure(ErrorCode::session, "session not STOPPING");
        state_ = State::idle;
    }
    void lose() noexcept { state_ = State::lost; }
    void exit() noexcept { state_ = State::exiting; }
    bool running() const noexcept { return state_ == State::running; }
    bool lost() const noexcept { return state_ == State::lost; }
    bool terminal() const noexcept { return state_ == State::lost || state_ == State::exiting; }
};
// This protocol is shared by the native session and deterministic failure tests.
struct IXrFrame {
    virtual ~IXrFrame() = default;
    virtual bool wait_begin() = 0;
    virtual bool locate() = 0;
    virtual void eye(unsigned index) = 0; // acquire, wait, render, fence, release
    virtual void end(bool submit) = 0;
};
inline void frame(IXrFrame &f) {
    const bool render = f.wait_begin();
    try {
        const bool valid = render && f.locate();
        if (valid) {
            f.eye(0);
            f.eye(1);
        }
        f.end(valid);
    } catch (...) {
        try {
            f.end(false);
        } catch (...) {
        }
        throw;
    }
}
struct Requirements {
    bool runtime, headset, extension, adapter, allocation, device;
};
inline void validate(const Requirements &r) {
    if (!r.runtime)
        throw Failure(ErrorCode::runtime, "OpenXR runtime unavailable");
    if (!r.extension)
        throw Failure(ErrorCode::extension, "XR_KHR_D3D12_enable unavailable");
    if (!r.headset)
        throw Failure(ErrorCode::headset, "headset unavailable; connect and wake PCVR headset");
    if (!r.adapter)
        throw Failure(ErrorCode::adapter, "OpenXR adapter unavailable");
    if (!r.allocation)
        throw Failure(ErrorCode::allocation, "graphics allocation failed");
    if (!r.device)
        throw Failure(ErrorCode::device, "graphics device lost");
}
} // namespace odv::xr
