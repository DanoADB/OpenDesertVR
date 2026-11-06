// SPDX-License-Identifier: MIT
#pragma once
#include "odv/core/core.hpp"
namespace odv::game {
struct IGameAdapter {
    virtual ~IGameAdapter() = default;
    virtual Json probe_capabilities() const = 0;
};
class CrimsonDesertAdapter final : public IGameAdapter {
  public:
    Json probe_capabilities() const override;
};
} // namespace odv::game
