// SPDX-License-Identifier: MIT
#include "odv/openxr/host.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
int main(int argc, char **argv) {
    const bool preview = argc == 3 && std::string_view(argv[2]) == "--depth-preview";
    if (argc != 2 && !preview)
        return 2;
    const auto start = std::chrono::steady_clock::now();
    std::stop_source source;
    std::jthread timer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        source.request_stop();
    });
    odv::Config config;
    config.duration_seconds = 60;
    auto report =
        preview ? odv::xr::run_depth_preview(config, source.get_token()) : odv::xr::run(config, source.get_token());
    std::ofstream file(argv[1]);
    file << report.dump(2);
    if (!file || !source.stop_requested() || !report.value("test_runtime", false) ||
        std::chrono::steady_clock::now() - start > std::chrono::seconds(12))
        return 1;
    for (const auto &check : report.at("checks"))
        if (check.value("status", "") == "FAIL")
            return 1;
    std::cout << "Cooperative stop returned before the 60-second deadline\n";
    return 0;
}
