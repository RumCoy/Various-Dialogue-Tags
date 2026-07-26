#pragma once

#include <cstdint>
#include <filesystem>
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

        [[nodiscard]] bool Allows(std::uint32_t localFormID) const;
    };

    class Config
    {
    public:
        static Config& GetSingleton();

        bool Load(const std::filesystem::path& internalDataPath,
            const std::filesystem::path& userConfigPath);
        [[nodiscard]] bool Enabled() const noexcept;
        [[nodiscard]] bool GlobalPluginNameFallback() const noexcept;
        [[nodiscard]] const Rule* FindRule(std::string_view pluginName) const;

    private:
        bool LoadFile(const std::filesystem::path& path, bool optional);

        bool enabled_{ true };
        bool globalPluginNameFallback_{ false };
        std::unordered_map<std::string, Rule> rules_;
    };
}
