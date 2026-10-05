#include "ShipwrightJPAssistHost.h"

#include "JPAssistHost.h"

#include <fstream>
#include <sstream>
#include <ship/Context.h>

namespace JPAssist {

void ShipwrightJPAssistHost_Install() {
    JPAssistHost_SetPathResolver([](HostPathKind kind, const std::string& relativePath) {
        if (kind == HostPathKind::Writable) {
            return Ship::Context::GetPathRelativeToAppDirectory(relativePath);
        }
        return Ship::Context::LocateFileAcrossAppDirs(relativePath);
    });
    JPAssistHost_SetDataReader([](const std::string& relativePath, std::string& contents) {
        std::ifstream stream(Ship::Context::LocateFileAcrossAppDirs(relativePath), std::ios::binary);
        if (!stream.is_open()) {
            return false;
        }
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        contents = buffer.str();
        return stream.good() || stream.eof();
    });
}

} // namespace JPAssist
