// SPDX-License-Identifier: MIT
#include "odv/core/core.hpp"
#include "odv/core/metrics.hpp"
#include "odv/game/adapter.hpp"
#include "odv/openxr/lifecycle.hpp"
#include "odv/stereo/scene.hpp"
#include <iostream>
#include <limits>
using namespace odv;
int failures = 0;
void check_test(bool condition, const char *label) {
    if (!condition) {
        ++failures;
        std::cerr << label << '\n';
    }
}
template <class F> void fails(F f, ErrorCode code) {
    try {
        f();
        check_test(false, "expected failure");
    } catch (const Failure &e) {
        check_test(e.code == code, "wrong error category");
    }
}
struct MockFrame : xr::IXrFrame {
    std::string calls;
    bool render = true, valid = true, fail_eye = false;
    bool wait_begin() override {
        calls += "WB";
        return render;
    }
    bool locate() override {
        calls += "L";
        return valid;
    }
    void eye(unsigned i) override {
        calls += i ? 'R' : 'A';
        if (fail_eye)
            throw Failure(ErrorCode::device, "injected");
    }
    void end(bool submit) override { calls += submit ? 'E' : 'Z'; }
};
int main() {
    TimingWindow timing;
    check_test(timing.summary()["status"] == "NOT_RUN", "no timing observation must remain NOT_RUN");
    for (int i = 1; i <= 20; ++i)
        timing.add(i);
    check_test(timing.summary()["median"] == 10 && timing.summary()["p95"] == 19, "nearest-rank timing percentiles");
    for (std::size_t i = 0; i < TimingWindow::capacity; ++i)
        timing.add(2);
    check_test(timing.summary()["samples"] == 4096 && timing.summary()["total_samples"] == 4116 &&
                   timing.summary()["max"] == 2,
               "timing retention bound and lifetime count");
    try {
        timing.add(std::numeric_limits<double>::quiet_NaN());
        check_test(false, "NaN timing accepted");
    } catch (const std::invalid_argument &) {
    }
    try {
        timing.add(-1);
        check_test(false, "negative timing accepted");
    } catch (const std::invalid_argument &) {
    }
    check_test(std::holds_alternative<Config>(parse_config(R"({"schema_version":1,"duration_seconds":2})")),
               "valid config");
    for (const std::string text :
         {"null", "{}", R"({"schema_version":2})", R"({"schema_version":1,"unknown":0})",
          R"({"schema_version":1,"duration_seconds":1.5})",
          R"({"schema_version":1,"duration_seconds":99999999999999999})", R"({"schema_version":1,"near_z":0})",
          R"({"schema_version":1,"far_z":0.01})", R"({"schema_version":1,"debug_layer":"yes"})",
          R"({"schema_version":1,"schema_version":1})"})
        check_test(std::holds_alternative<Error>(parse_config(text)), "invalid config accepted");
    check_test(std::holds_alternative<Error>(parse_config(std::string(65537, ' '))), "oversized config");
    Log log;
    for (int i = 0; i < 300; ++i)
        log.record("TEST", {{"value", i}, {"path", "C:/Users/private"}, {"serial", "1234"}});
    const auto events = log.snapshot();
    check_test(events.size() == 256 && events[0]["fields"]["value"] == 44, "log bound");
    check_test(events.dump().find("private") == std::string::npos && events.dump().find("1234") == std::string::npos,
               "log redaction");
    auto report = empty_report();
    check_test(report["headset_validation"] == "NOT_RUN" && report["game_validation"] == "NOT_RUN", "evidence grades");
    check_test(game::CrimsonDesertAdapter{}.probe_capabilities()["active"] == false, "game must stay inactive");
    xr::Lifecycle state;
    fails([&] { state.begin(); }, ErrorCode::session);
    state.ready();
    state.begin();
    check_test(state.running(), "READY begin");
    state.stop();
    state.end();
    state.ready();
    state.begin();
    state.lose();
    check_test(state.terminal() && !state.running(), "loss is terminal");
    fails([&] { state.ready(); }, ErrorCode::session);
    for (int i = 0; i < 6; ++i) {
        xr::Requirements r{true, true, true, true, true, true};
        const ErrorCode codes[] = {ErrorCode::runtime, ErrorCode::headset,    ErrorCode::extension,
                                   ErrorCode::adapter, ErrorCode::allocation, ErrorCode::device};
        switch (i) {
        case 0:
            r.runtime = false;
            break;
        case 1:
            r.headset = false;
            break;
        case 2:
            r.extension = false;
            break;
        case 3:
            r.adapter = false;
            break;
        case 4:
            r.allocation = false;
            break;
        case 5:
            r.device = false;
            break;
        }
        fails([&] { xr::validate(r); }, codes[i]);
    }
    MockFrame f;
    xr::frame(f);
    check_test(f.calls == "WBLARE", "stereo ordering");
    f = {};
    f.render = false;
    xr::frame(f);
    check_test(f.calls == "WBZ", "shouldRender false");
    f = {};
    f.valid = false;
    xr::frame(f);
    check_test(f.calls == "WBLZ", "invalid tracking");
    f = {};
    f.fail_eye = true;
    fails([&] { xr::frame(f); }, ErrorCode::device);
    check_test(f.calls == "WBLAZ", "failure ends frame with no layer");
    const math::Fov fov{-0.7, 0.7, 0.7, -0.7};
    const auto left = stereo::vertices(math::eye_pose({}, -0.032), fov, 0.05, 100);
    const auto right = stereo::vertices(math::eye_pose({}, 0.032), fov, 0.05, 100);
    check_test(left.size() == 108 && right.size() == 108, "three cubes rendered");
    auto disparity = [&](int i) { return left[i].clip[0] / left[i].clip[3] - right[i].clip[0] / right[i].clip[3]; };
    check_test(disparity(0) > disparity(36) && disparity(36) > disparity(72) && disparity(72) > 0,
               "rendered geometry has calibrated stereo disparity");
    return failures ? 1 : 0;
}
