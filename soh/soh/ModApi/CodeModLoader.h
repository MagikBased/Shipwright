#pragma once

#include <memory>
#include <vector>

namespace Ship {
class Archive;
}

namespace SOH {

// Initializes every code-bearing archive as one batch after asset mounting is
// complete. Keeping this policy outside the mod menu limits the upstream
// integration surface to archive collection and a single handoff call.
void LoadCodeModArchives(const std::vector<std::shared_ptr<Ship::Archive>>& archives);

} // namespace SOH
