#pragma once

#include <string>

class Config {
public:
    static Config& getInstance();

    bool load(const std::string& filepath);
    bool save(const std::string& filepath);

    // Getters and Setters for configuration variables
    std::string getWorkspacePath() const;
    void setWorkspacePath(const std::string& path);

private:
    Config() = default;
    ~Config() = default;

    // Delete copy constructor and assignment operator
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::string m_workspacePath = "workspace/";
};
