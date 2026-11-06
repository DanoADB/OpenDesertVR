// SPDX-License-Identifier: MIT
#include "odv/openxr/host.hpp"
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
    try {
        std::string config_path, output = "headset-report.json";
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "odv-testhost [--config config.json] [--report headset-report.json]\nDefault duration: 60 "
                             "seconds. Ctrl+C requests orderly exit.\n";
                return 0;
            }
            if ((arg == "--config" || arg == "--report") && i + 1 < argc) {
                if (arg == "--config")
                    config_path = argv[++i];
                else
                    output = argv[++i];
            } else {
                std::cerr << "Invalid arguments; use --help\n";
                return 2;
            }
        }
        const auto config = odv::load_config(config_path);
        std::cout << "Synthetic stereo test only. Red: 1 m, green: 3 m, blue: 8 m. LOCAL eye-height origin.\n";
        auto report = odv::xr::run(config);
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
