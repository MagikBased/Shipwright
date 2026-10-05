#pragma once

#include <functional>
#include <string>

namespace JPAssist {

// The portable JP Assist core asks the host for paths through this deliberately
// small interface. Shipwright installs its resolver during initialization;
// an external .o2r adapter, another recompilation project, or a standalone
// test can provide a different implementation without pulling Ship::Context
// into repositories and persistence code.
enum class HostPathKind {
    Data,
    Writable,
};

using HostPathResolver = std::function<std::string(HostPathKind kind, const std::string& relativePath)>;
using HostDataReader = std::function<bool(const std::string& relativePath, std::string& contents)>;

void JPAssistHost_SetPathResolver(HostPathResolver resolver);
void JPAssistHost_SetDataReader(HostDataReader reader);
void JPAssistHost_ResetPathResolver();
std::string JPAssistHost_LocateDataFile(const std::string& relativePath);
std::string JPAssistHost_GetWritableFile(const std::string& relativePath);
bool JPAssistHost_ReadDataFile(const std::string& relativePath, std::string& contents);

} // namespace JPAssist
