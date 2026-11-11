// SPDX-License-Identifier: MIT
#pragma once
#include "odv/core/core.hpp"
#include <stop_token>
namespace odv::xr {
Json doctor();
Json run(const Config &config, std::stop_token stop = {});
Json run_stream(const Config &config, const std::wstring &channel, std::stop_token stop = {});
Json run_depth_preview(const Config &config, std::stop_token stop = {});
} // namespace odv::xr
