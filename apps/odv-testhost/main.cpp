// SPDX-License-Identifier: MIT
#include "odv/openxr/host.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
int main(int argc, char **argv) {
    try {
        std::string config_path, output = "headset-report.json", stream_channel, stream_file;
        bool depth_preview = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "odv-testhost [--config config.json] [--report headset-report.json]\nDefault duration: 60 "
                             "seconds. Ctrl+C requests orderly exit.\n"
                             "Stereo images: --stereo-stream CHANNEL or --stereo-stream-file FILE.\n"
                             "Owned scene through depth reconstruction and transport: --depth-preview\n";
                return 0;
            }
            if (arg == "--depth-preview") {
                depth_preview = true;
                continue;
            }
            if ((arg == "--config" || arg == "--report" || arg == "--stereo-stream" || arg == "--stereo-stream-file") &&
                i + 1 < argc) {
                if ((arg == "--stereo-stream" || arg == "--stereo-stream-file") && !argv[i + 1][0])
                    throw odv::Failure(odv::ErrorCode::configuration, "Stereo source must not be empty");
                if (arg == "--config")
                    config_path = argv[++i];
                else if (arg == "--report")
                    output = argv[++i];
                else if (arg == "--stereo-stream")
                    stream_channel = argv[++i];
                else
                    stream_file = argv[++i];
            } else {
                std::cerr << "Invalid arguments; use --help\n";
                return 2;
            }
        }
        const auto config = odv::load_config(config_path);
        if (depth_preview && (!stream_channel.empty() || !stream_file.empty()))
            throw odv::Failure(odv::ErrorCode::configuration, "Choose preview or external stream");
        if (!stream_file.empty()) {
            if (!stream_channel.empty())
                throw odv::Failure(odv::ErrorCode::configuration, "Choose one stereo stream source");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (stream_channel.empty() && std::chrono::steady_clock::now() < deadline) {
                std::ifstream input(stream_file);
                char line[128]{};
                if (input.getline(line, sizeof(line)))
                    stream_channel = line;
                if (stream_channel.empty())
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (stream_channel.empty())
                throw odv::Failure(odv::ErrorCode::configuration, "Stereo channel file did not become ready");
        }
        if (depth_preview)
            std::cout << "Owned courtyard: depth-reconstructed stereo, static camera, no game source or game head "
                         "tracking.\n";
        else if (stream_channel.empty())
            std::cout << "Synthetic stereo test only. Red: 1 m, green: 3 m, blue: 8 m. LOCAL eye-height origin.\n";
        else
            std::cout << "Static stereo image stream. Source camera validity and game head tracking are unverified.\n";
        auto report = depth_preview ? odv::xr::run_depth_preview(config)
                      : stream_channel.empty()
                          ? odv::xr::run(config)
                          : odv::xr::run_stream(config, std::wstring(stream_channel.begin(), stream_channel.end()));
        std::ofstream file(output);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file << report.dump(2) << '\n';
        return report.value("ready", false) ? 0 : 3;
    } catch (const odv::Failure &e) {
        std::cerr << e.what() << '\n';
        return 2;
    } catch (const std::exception &) {
        std::cerr << "Host failed or report could not be written\n";
        return 1;
    }
}
