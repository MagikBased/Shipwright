#include "CodeModLoader.h"

#ifdef ENABLE_SCRIPTING
#include <exception>
#include <string>

#include <ship/Context.h>
#include <ship/scripting/ScriptLoader.h>
#include <spdlog/spdlog.h>
#endif

namespace SOH {

void LoadCodeModArchives(const std::vector<std::shared_ptr<Ship::Archive>>& archives) {
#ifdef ENABLE_SCRIPTING
    if (archives.empty()) {
        return;
    }

    try {
        Ship::Context* context = Ship::Context::GetRawInstance();
        const std::vector<std::string> libraryPaths = { Ship::Context::GetAppBundlePath() };
        if (!context->InitScriptLoader({}, 1, "-g -Wl", {}, libraryPaths, {})) {
            SPDLOG_ERROR("Failed to initialize the .o2r code-mod loader");
            return;
        }

        for (const auto& archive : archives) {
            context->GetScriptLoader()->Compile(archive);
        }
        context->GetScriptLoader()->LoadAll();
    } catch (const std::exception& exception) {
        SPDLOG_ERROR("Failed to load an .o2r code mod: {}", exception.what());
    }
#else
    (void)archives;
#endif
}

} // namespace SOH
