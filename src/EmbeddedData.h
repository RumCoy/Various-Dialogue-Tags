#pragma once

#include <string_view>

namespace VariousDialogueTags::EmbeddedData
{
    [[nodiscard]] std::string_view GetInternalData() noexcept;
}
