#pragma once

#include <cstddef>
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
        std::size_t LoadDynamicFiles(const std::filesystem::path& directory);
        bool LoadStream(std::istream& input, std::string sourceName,
            bool loadGeneralSettings, bool loadPluginRules, bool userOverride);
        [[nodiscard]] bool PersistMenuSetting(
            std::string_view key, std::string_view value) const;
        [[nodiscard]] bool SyncUserGeneralSettingsToTempCache() const;
        [[nodiscard]] bool UpdateGeneralSetting(const std::filesystem::path& path,
            std::string_view key, std::string_view value, bool createIfMissing) const;

        bool enabled_{ true };
        bool globalPluginNameFallback_{ false };
        bool immersiveMode_{ false };
        std::optional<bool> userEnabledOverride_;
        std::optional<bool> userGlobalPluginNameFallbackOverride_;
        std::optional<bool> userImmersiveModeOverride_;
        std::filesystem::path userConfigPath_;
        std::filesystem::path tempCachePath_;
        std::unordered_map<std::string, Rule> rules_;
    };
}
