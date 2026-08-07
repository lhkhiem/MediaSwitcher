#pragma once

#include <string>

class WorkspaceManager {
public:
    WorkspaceManager();
    ~WorkspaceManager();

    bool initialize();

private:
    bool createDirectoryIfNotExists(const std::string& path);
};
