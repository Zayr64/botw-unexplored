#include "SwitchStyle.h"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "Graphics/Font.h"
#include "Graphics/ShapeBatch.h"

namespace
{
    std::string Utf8(uint32_t cp)
    {
        return std::string{static_cast<char>(0xE0 | (cp >> 12)), static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
                           static_cast<char>(0x80 | (cp & 0x3F))};
    }

    constexpr float ChipScale = 0.34f;
    constexpr float ChipDotSpace = 16.0f;
}

void SwitchStyle::SelectionFrame(ShapeBatch& shapes, float left, float bottom, float right, float top, float radius,
                                 unsigned int frame, const glm::vec3& gap, float outset, float thickness)
{
    const float pulse = 0.80f + 0.20f * std::sin(static_cast<float>(frame) * 0.10f);
    const float inner = outset - thickness;
    shapes.AddRoundedRect(left - outset, bottom - outset, right + outset, top + outset, radius + outset,
                          glm::vec4(Cyan * pulse, 1.0f));
    shapes.AddRoundedRect(left - inner, bottom - inner, right + inner, top + inner, radius + inner, glm::vec4(gap, 1.0f));
}

void SwitchStyle::Toggle(ShapeBatch& shapes, float right, float top, bool on)
{
    const float w = ToggleWidth, h = ToggleHeight;
    shapes.AddRoundedRect(right - w, top - h, right, top, h * 0.5f, on ? glm::vec4(Cyan, 1.0f) : TrackOff);
    shapes.AddCircle(glm::vec2(on ? right - h * 0.5f : right - w + h * 0.5f, top - h * 0.5f), h * 0.5f - 2.5f,
                     glm::vec4(1.0f));
}

void SwitchStyle::TabMark(ShapeBatch& shapes, float left, float right, float baseline)
{
    shapes.AddRect(left, baseline - 10.0f, right, baseline - 8.0f, glm::vec4(Gold, 1.0f));
}

glm::vec3 SwitchStyle::TabTitleColor(bool active)
{
    return active ? Gold : TextDim;
}

std::string SwitchStyle::ButtonGlyphs(Font& font, const std::string& name)
{
    static const struct { const char* name; uint32_t first, second; } symbols[] = {
        {"A", 0xE0E0, 0}, {"B", 0xE0E1, 0}, {"X", 0xE0E2, 0}, {"Y", 0xE0E3, 0}, {"L", 0xE0E4, 0}, {"R", 0xE0E5, 0},
        {"ZL", 0xE0E6, 0}, {"ZR", 0xE0E7, 0}, {"+", 0xE0EF, 0}, {"-", 0xE0F0, 0}, {"<", 0xE0ED, 0}, {">", 0xE0EE, 0},
        {"Up/Down", 0xE0EB, 0xE0EC}, {"Left/Right", 0xE0ED, 0xE0EE}
    };
    for (const auto& symbol : symbols)
    {
        if (name != symbol.name)
            continue;
        if (!font.HasGlyph(symbol.first) || (symbol.second && !font.HasGlyph(symbol.second)))
            return std::string();
        return Utf8(symbol.first) + (symbol.second ? Utf8(symbol.second) : std::string());
    }
    return std::string();
}

float SwitchStyle::GlyphScale(Font& font, float height)
{
    const glm::vec2 extent = font.GlyphTopBottom(0xE0E0, 1.0f); // the A
    return extent.x - extent.y > 0.0f ? height / (extent.x - extent.y) : 0.5f;
}

float SwitchStyle::GlyphBaseline(Font& font, float scale, float centerY)
{
    const glm::vec2 extent = font.GlyphTopBottom(0xE0E0, scale);
    return centerY - (extent.x + extent.y) * 0.5f;
}

float SwitchStyle::CapHeight(Font& font, float scale)
{
    return font.GlyphTopBottom('H', scale).x;
}

float SwitchStyle::ChipWidth(Font& font, const std::string& text, bool dot)
{
    return font.MeasureText(text, ChipScale) + 28.0f + (dot ? ChipDotSpace : 0.0f);
}

float SwitchStyle::Chip(ShapeBatch& shapes, Font& font, const std::string& text, float x, float top, bool anchorRight,
                        const glm::vec4& background, const glm::vec3* dotColor)
{
    const float dotSpace = dotColor ? ChipDotSpace : 0.0f;
    const float width = ChipWidth(font, text, dotColor != nullptr);
    const float left = anchorRight ? x - width : x;
    const float centerY = top - ChipHeight * 0.5f;
    shapes.AddRoundedRect(left, top - ChipHeight, left + width, top, ChipHeight * 0.5f, background);
    if (dotColor)
        shapes.AddCircle(glm::vec2(left + 18.0f, centerY), 5.0f, glm::vec4(*dotColor, 1.0f));
    font.AddTextToBatch(text, glm::vec2(left + 14.0f + dotSpace, centerY - CapHeight(font, ChipScale) * 0.5f), ChipScale,
                        TextColor);
    return width;
}

std::string SwitchStyle::SaveDate(uint32_t seconds)
{
    if (seconds == 0)
        return std::string();
    // The game writes its local time as if it were UTC
    const time_t t = static_cast<time_t>(seconds);
    struct tm parts;
    if (!gmtime_r(&t, &parts))
        return std::string();
    char text[32];
    std::snprintf(text, sizeof(text), "%02d/%02d %02d:%02d", parts.tm_mday, parts.tm_mon + 1, parts.tm_hour, parts.tm_min);
    return text;
}

std::string SwitchStyle::SourceText(Source source, uint32_t saveTime, bool brief, glm::vec3& dotColor, bool& hasDot)
{
    hasDot = source != Source::Save;
    if (source == Source::Live)
    {
        dotColor = Good;
        return brief ? "Live" : "Live from the game";
    }
    if (source == Source::Copy)
        dotColor = Warn;
    const std::string date = SaveDate(saveTime);
    const std::string label = source == Source::Copy ? "Save copy" : "Save";
    if (date.empty())
        return label;
    return brief ? date : label + " \xC2\xB7 " + date;
}
