// SPDX-License-Identifier: MIT
#include "odv/core/core.hpp"
#include "odv/game/adapter.hpp"
#ifdef _WIN32
#include "odv/openxr/host.hpp"
#endif
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc < 2 || (std::string(argv[1]) != "doctor" && std::string(argv[1]) != "export-report")) {
            std::cerr << "Usage: odv-cli doctor | export-report [--output report.json]\n";
            return 2;
        }
        std::string output;
        if (argc > 2) {
            if (std::string(argv[1]) != "export-report" || argc != 4 || std::string(argv[2]) != "--output")
                return 2;
            output = argv[3];
        }
#ifdef _WIN32
        auto report = odv::xr::doctor();
#else
        auto report = odv::empty_report();
        report["os"] = "non-Windows";
        report["ready"] = false;
        report["checks"].push_back(odv::check("platform", "FAIL", "Windows x64 required for D3D12/OpenXR host"));
#endif
        report["game_adapter"] = odv::game::CrimsonDesertAdapter{}.probe_capabilities();
        const auto text = report.dump(2) + "\n";
        if (output.empty())
            std::cout << text;
        else {
            std::ofstream file(output);
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file << text;
        }
        // Diagnostics return 3 for missing prerequisites; producing a report is not readiness.
        return report.value("ready", false) ? 0 : 3;
    } catch (const std::exception &) {
        std::cerr << "Diagnostics failed; cannot write report\n";
        return 1;
    }
}
