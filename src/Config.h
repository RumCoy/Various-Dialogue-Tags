#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace VariousDialogueTags
{
    struct Rule
    {
        std::string tag;
        std::unordered_set<std::uint32_t> includeForms;
        std::unordered_set<std::uint32_t> excludeForms;
        bool modNameTags{ true };

        [[nodiscard]] bool Allows(std::uint32_t localFormID) const;
    };

    class Config
    {
    public:
        static Config& GetSingleton();

        bool Load(std::string_view embeddedInternalData,
            const std::filesystem::path& userConfigPath,
            const std::filesystem::path& tempCachePath);

        [[nodiscard]] bool Enabled() const noexcept;
        [[nodiscard]] bool SetEnabledFromMenu(bool enabled);

        [[nodiscard]] bool GlobalPluginNameFallback() const noexcept;
        [[nodiscard]] bool SetGlobalPluginNameFallbackFromMenu(bool enabled);
        [[nodiscard]] bool ImmersiveMode() const noexcept;
        [[nodiscard]] bool SetImmersiveModeFromMenu(bool enabled);
        [[nodiscard]] const Rule* FindRule(std::string_view pluginName) const;

    private:
        bool LoadUserFile(const std::filesystem::path& path);
        bool LoadTempCache(const std::filesystem::path& path);
        bool LoadStream(std::istream& input, std::string sourceName, bool userOverride);
        [[nodiscard]] bool SaveTempCache() const;
        [[nodiscard]] bool PersistMenuSetting(
            std::string_view key, std::string_view value) const;
        [[nodiscard]] bool UpdateUserConfigSetting(
            std::string_view key, std::string_view value) const;

        bool enabled_{ true };
        bool globalPluginNameFallback_{ false };
        bool immersiveMode_{ false };
        std::filesystem::path userConfigPath_;
        std::filesystem::path tempCachePath_;
        std::unordered_map<std::string, Rule> rules_;
    };
}
