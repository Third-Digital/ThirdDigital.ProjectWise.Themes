#include "ActivePalette.h"

#include <atomic>
#include <initializer_list>
#include <new>

namespace PaletteSeams
{
    CreateBrush createSolidBrush = ::CreateSolidBrush;
    DeleteGdiObject deleteObject = ::DeleteObject;
}

namespace
{
    constexpr std::size_t kMaxSchemes = 16;

    std::array<const PaletteResources*, kMaxSchemes> g_prepared{};
    std::atomic<const PaletteResources*> g_active{nullptr};

    void FreeUnpublished(PaletteResources* resources) noexcept
    {
        for (const auto* set : {&resources->brushes, &resources->controlBrushes})
        {
            for (HBRUSH brush : *set)
            {
                if (brush != nullptr)
                {
                    PaletteSeams::deleteObject(brush);
                }
            }
        }
        delete resources;
    }
}

const PaletteResources* PreparePalette(const ColourScheme& scheme) noexcept
{
    const auto table = Schemes();
    if (&scheme < table.data() || &scheme >= table.data() + table.size())
    {
        return nullptr;
    }
    const std::size_t slot = static_cast<std::size_t>(&scheme - table.data());
    if (slot >= kMaxSchemes)
    {
        return nullptr;
    }
    if (g_prepared[slot] != nullptr)
    {
        return g_prepared[slot];
    }

    auto* resources = new (std::nothrow) PaletteResources;
    if (resources == nullptr)
    {
        return nullptr;
    }
    resources->scheme = &scheme;
    resources->palette = BuildPalette(scheme);
    for (int index = 0; index < static_cast<int>(resources->brushes.size()); ++index)
    {
        const auto colour = resources->palette.At(index);
        if (!colour)
        {
            continue;
        }
        for (auto* set : {&resources->brushes, &resources->controlBrushes})
        {
            HBRUSH brush = PaletteSeams::createSolidBrush(*colour);
            if (brush == nullptr)
            {
                FreeUnpublished(resources);
                return nullptr;
            }
            (*set)[static_cast<std::size_t>(index)] = brush;
        }
    }
    g_prepared[slot] = resources;
    return resources;
}

const PaletteResources* ActivePalette() noexcept
{
    return g_active.load(std::memory_order_acquire);
}

void PublishPalette(const PaletteResources* resources) noexcept
{
    g_active.store(resources, std::memory_order_release);
}
