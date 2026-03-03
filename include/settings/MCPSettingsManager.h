#ifndef MCPSETTINGSMANAGER_H
#define MCPSETTINGSMANAGER_H

class MCPSettingsManager
{
public:
    static MCPSettingsManager& instance();

    bool isEnabled() const;
    void setEnabled(bool enabled);

#ifdef SNAPTRAY_ENABLE_MCP
    static constexpr bool kDefaultEnabled = true;
#else
    static constexpr bool kDefaultEnabled = false;
#endif

private:
    MCPSettingsManager() = default;
    ~MCPSettingsManager() = default;
    MCPSettingsManager(const MCPSettingsManager&) = delete;
    MCPSettingsManager& operator=(const MCPSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyEnabled = "mcp/enabled";
};

#endif // MCPSETTINGSMANAGER_H
