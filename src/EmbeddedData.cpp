#include "EmbeddedData.h"

#include "Resource.h"

#include <Windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace VariousDialogueTags::EmbeddedData
{
    std::string_view GetInternalData() noexcept
    {
        constexpr WORD rawDataResourceType = 10;
        const auto module = reinterpret_cast<HMODULE>(&__ImageBase);
        const auto resource = FindResourceW(
            module, MAKEINTRESOURCEW(IDR_VDT_INTERNAL_DATA),
            MAKEINTRESOURCEW(rawDataResourceType));
        if (!resource) {
            return {};
        }

        const auto size = SizeofResource(module, resource);
        const auto loadedResource = LoadResource(module, resource);
        if (size == 0 || !loadedResource) {
            return {};
        }

        const auto* data = static_cast<const char*>(LockResource(loadedResource));
        return data ? std::string_view(data, size) : std::string_view{};
    }
}
