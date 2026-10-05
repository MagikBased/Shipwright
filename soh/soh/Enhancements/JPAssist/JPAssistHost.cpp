#include "JPAssistHost.h"

#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

namespace JPAssist {
namespace {

std::mutex sResolverMutex;
HostPathResolver sResolver;
HostDataReader sDataReader;

std::string Resolve(HostPathKind kind, const std::string& relativePath) {
    HostPathResolver resolver;
    {
        std::lock_guard<std::mutex> lock(sResolverMutex);
        resolver = sResolver;
    }
    // Relative paths are a useful and deterministic fallback for standalone
    // tools and tests that do not install a game host.
    return resolver ? resolver(kind, relativePath) : relativePath;
}

} // namespace

void JPAssistHost_SetPathResolver(HostPathResolver resolver) {
    std::lock_guard<std::mutex> lock(sResolverMutex);
    sResolver = std::move(resolver);
}

void JPAssistHost_SetDataReader(HostDataReader reader) {
    std::lock_guard<std::mutex> lock(sResolverMutex);
    sDataReader = std::move(reader);
}

void JPAssistHost_ResetPathResolver() {
    std::lock_guard<std::mutex> lock(sResolverMutex);
    sResolver = {};
    sDataReader = {};
}

std::string JPAssistHost_LocateDataFile(const std::string& relativePath) {
    return Resolve(HostPathKind::Data, relativePath);
}

std::string JPAssistHost_GetWritableFile(const std::string& relativePath) {
    return Resolve(HostPathKind::Writable, relativePath);
}

bool JPAssistHost_ReadDataFile(const std::string& relativePath, std::string& contents) {
    HostDataReader reader;
    {
        std::lock_guard<std::mutex> lock(sResolverMutex);
        reader = sDataReader;
    }
    if (reader) {
        return reader(relativePath, contents);
    }

    std::ifstream stream(JPAssistHost_LocateDataFile(relativePath), std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    contents = buffer.str();
    return stream.good() || stream.eof();
}

} // namespace JPAssist
