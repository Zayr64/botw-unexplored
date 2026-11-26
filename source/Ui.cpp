#include "Ui.h"

#include <cmath>
#include <sstream>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Graphics/Font.h"
#include "Graphics/Quad.h"
#include "Graphics/ShapeBatch.h"
#include "Graphics/Texture2D.h"
#include "Localization.h"
#include "Map.h"
#include "SavefileIO.h"
#include "SwitchStyle.h"

namespace Ui
{
    // zayr31: the colours BotW Compendium uses too (SwitchStyle); the panel and the bars let the map show through.
    namespace Colors
    {
        const glm::vec4 Panel(0.045f, 0.065f, 0.08f, 0.90f);
        const glm::vec4 Bar(SwitchStyle::BarRgb, 0.84f);
        const glm::vec4 Hairline(SwitchStyle::Hairline);
        const glm::vec4 RowOn(SwitchStyle::RowOn);
        const glm::vec4 RowOff(1.0f, 1.0f, 1.0f, 0.07f);
        const glm::vec4 Track(1.0f, 1.0f, 1.0f, 0.16f);
        const glm::vec3 Text(SwitchStyle::TextColor);
        const glm::vec3 TextDim(SwitchStyle::TextDim);
        const glm::vec3 Accent(SwitchStyle::Accent);
        const glm::vec3 Gold(SwitchStyle::Gold);
        const glm::vec3 Good(SwitchStyle::Good);
        const glm::vec3 Warn(SwitchStyle::Warn);
    }
}

namespace
{
    constexpr float HintScale = 0.36f;
    constexpr float CapHeightPerScale = 34.4f; // height of a capital letter of the 48 px font
    constexpr int ToastFrames = 210;           // about 3.5 s

    ShapeBatch* s_Shapes = nullptr;
    glm::mat4 s_Projection(1.0f);
    unsigned int s_Frame = 0;
    std::string s_Toast;
    bool s_ToastWarning = false;
    int s_ToastFramesLeft = 0;

    const glm::vec4 CapColor(0.93f, 0.95f, 0.96f, 1.0f);
    const glm::vec3 CapTextColor(0.05f, 0.07f, 0.09f);

    glm::vec4 WithAlpha(const glm::vec3& color, float alpha)
    {
        return glm::vec4(color, alpha);
    }

    std::vector<std::string> ButtonNames(const std::string& buttons)
    {
        std::vector<std::string> names;
        std::istringstream stream(buttons);
        for (std::string name; stream >> name;)
            names.push_back(name);
        return names;
    }

    // The Switch's own symbol of a button (SwitchStyle); "" without the Nintendo Extension font (the caps below are
    // drawn then).
    std::string ButtonGlyphs(const std::string& button)
    {
        return SwitchStyle::ButtonGlyphs(Map::m_Font, button);
    }

    // The scale that draws the symbols as tall as the caps (25): measured on the A once.
    float GlyphScale()
    {
        static float scale = 0.0f;
        if (scale == 0.0f)
            scale = SwitchStyle::GlyphScale(Map::m_Font, 25.0f);
        return scale;
    }

    float CapWidth(const std::string& button)
    {
        const std::string glyphs = ButtonGlyphs(button);
        if (!glyphs.empty())
            return Ui::TextWidth(glyphs, GlyphScale());
        if (button.size() == 1)
            return 24.0f;
        return Ui::TextWidth(button, 0.28f) + 14.0f;
    }

    // One button cap with its left edge at x, centred on centerY.
    void DrawCap(const std::string& button, float x, float centerY)
    {
        ShapeBatch& shapes = Ui::Shapes();
        const float width = CapWidth(button);
        const std::string glyphs = ButtonGlyphs(button);
        if (!glyphs.empty())
        {
            // The symbol centred on the bar's middle, in the colour the caps had
            const float scale = GlyphScale();
            Ui::Text(glyphs, glm::vec2(x, SwitchStyle::GlyphBaseline(Map::m_Font, scale, centerY)), scale,
                     glm::vec3(CapColor));
            return;
        }
        if (button.size() == 1)
        {
            const glm::vec2 center(x + 12.0f, centerY);
            shapes.AddCircle(center, 12.0f, CapColor);
            const glm::vec4 ink(CapTextColor, 1.0f);
            if (button == "+" || button == "-")
            {
                // The font's signs are not centred like letters: draw them.
                shapes.AddRect(center.x - 6.0f, center.y - 1.25f, center.x + 6.0f, center.y + 1.25f, ink);
                if (button == "+")
                    shapes.AddRect(center.x - 1.25f, center.y - 6.0f, center.x + 1.25f, center.y + 6.0f, ink);
                return;
            }
            Ui::Text(button, glm::vec2(center.x, centerY - CapHeightPerScale * 0.33f * 0.5f), 0.33f, CapTextColor, ALIGN_CENTER);
            return;
        }
        shapes.AddRoundedRect(x, centerY - 11.0f, x + width, centerY + 11.0f, 7.0f, CapColor);
        Ui::Text(button, glm::vec2(x + width * 0.5f, centerY - CapHeightPerScale * 0.28f * 0.5f), 0.28f, CapTextColor, ALIGN_CENTER);
    }
}

void Ui::Init()
{
    if (!s_Shapes)
        s_Shapes = new ShapeBatch();
    s_Projection = glm::ortho(Left(), Right(), Bottom(), Top(), -1.0f, 1.0f);
}

float Ui::Left()
{
    return Map::m_ScreenLeft / Zoom;
}

float Ui::Right()
{
    return Map::m_ScreenRight / Zoom;
}

float Ui::Top()
{
    return Map::m_ScreenTop / Zoom;
}

float Ui::Bottom()
{
    return Map::m_ScreenBottom / Zoom;
}

glm::mat4* Ui::Projection()
{
    return &s_Projection;
}

glm::vec2 Ui::FromScreen(const glm::vec2& centred)
{
    return centred / Zoom;
}

int Ui::LineCount(const std::string& text, float scale, float width)
{
    int lines = 0;
    std::string line;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find(' ', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string word = text.substr(start, end - start);
        const std::string longer = line.empty() ? word : line + " " + word;
        if (!line.empty() && TextWidth(longer, scale) > width)
        {
            ++lines;
            line = word;
        }
        else
            line = longer;
        start = end + 1;
    }
    return line.empty() ? lines : lines + 1;
}

void Ui::Destroy()
{
    delete s_Shapes;
    s_Shapes = nullptr;
}

void Ui::Tick()
{
    ++s_Frame;
    if (s_ToastFramesLeft > 0)
        --s_ToastFramesLeft;
}

unsigned int Ui::Frame()
{
    return s_Frame;
}

ShapeBatch& Ui::Shapes()
{
    return *s_Shapes;
}

void Ui::FlushShapes()
{
    s_Shapes->Render(s_Projection);
}

void Ui::FlushShapes(const glm::mat4& projection)
{
    s_Shapes->Render(projection);
}

float Ui::TextWidth(const std::string& text, float scale)
{
    return Map::m_Font.MeasureText(text, scale);
}

glm::vec2 Ui::Text(const std::string& text, const glm::vec2& position, float scale, const glm::vec3& color, int align,
                   float maxWidth)
{
    return Map::m_Font.AddTextToBatch(text, position, scale, color, align, maxWidth);
}

void Ui::Footer(const std::vector<Hint>& allHints, const std::string& allContext)
{
    const float bottom = Bottom();
    const float top = bottom + FooterHeight;
    const float centerY = bottom + FooterHeight * 0.5f;
    const float baseline = centerY - CapHeightPerScale * HintScale * 0.5f;

    Shapes().AddRect(Left(), bottom, Right(), top, Colors::Bar);
    Shapes().AddRect(Left(), top, Right(), top + 1.0f, Colors::Hairline);

    // With a big zoom not everything fits: the name on the left goes first, then the hints that matter least.
    std::vector<Hint> hints = allHints;
    std::string context = allContext;
    const auto hintsWidth = [&]()
    {
        float width = 0.0f;
        for (const Hint& hint : hints)
        {
            width += TextWidth(hint.label, HintScale) + 7.0f + 16.0f;
            for (const std::string& button : ButtonNames(hint.buttons))
                width += CapWidth(button) + 4.0f;
        }
        return width;
    };
    const float room = Right() - Left() - 44.0f;
    if (!context.empty() && hintsWidth() + TextWidth(context, HintScale) + 24.0f > room)
        context.clear();
    while (hints.size() > 1 && hintsWidth() > room)
    {
        size_t drop = 0;
        for (size_t i = 1; i < hints.size(); ++i)
            if (hints[i].keep < hints[drop].keep)
                drop = i;
        hints.erase(hints.begin() + drop);
    }

    float x = Right() - 22.0f;
    for (std::vector<Hint>::const_reverse_iterator hint = hints.rbegin(); hint != hints.rend(); ++hint)
    {
        x -= TextWidth(hint->label, HintScale);
        Text(hint->label, glm::vec2(x, baseline), HintScale, Colors::Text);
        x -= 7.0f;

        const std::vector<std::string> buttons = ButtonNames(hint->buttons);
        for (std::vector<std::string>::const_reverse_iterator button = buttons.rbegin(); button != buttons.rend(); ++button)
        {
            x -= CapWidth(*button);
            DrawCap(*button, x, centerY);
            x -= 4.0f;
        }
        x -= 16.0f;
    }

    if (!context.empty())
        Text(context, glm::vec2(Left() + 22.0f, baseline), HintScale, Colors::TextDim);
}

float Ui::Chip(const std::string& text, float x, float top, bool anchorRight, const glm::vec3* dotColor)
{
    return SwitchStyle::Chip(Shapes(), Map::m_Font, text, x, top, anchorRight, Colors::Bar, dotColor);
}

void Ui::Toast(const std::string& text, bool warning)
{
    s_Toast = text;
    s_ToastWarning = warning;
    s_ToastFramesLeft = ToastFrames;
}

void Ui::DrawToast()
{
    if (s_ToastFramesLeft <= 0 || s_Toast.empty())
        return;
    constexpr float Height = 36.0f;
    constexpr float Scale = 0.38f;
    const float width = TextWidth(s_Toast, Scale) + 58.0f;
    const float bottom = Bottom() + FooterHeight + 14.0f;
    const float centerY = bottom + Height * 0.5f;
    // Centred on the part of the screen the panel does not cover (the shift is in screen pixels).
    const float centerX = Map::m_ViewShift / Zoom;

    Shapes().AddRoundedRect(centerX - width * 0.5f, bottom, centerX + width * 0.5f, bottom + Height, Height * 0.5f,
                            glm::vec4(0.02f, 0.03f, 0.04f, 0.92f));
    Shapes().AddCircle(glm::vec2(centerX - width * 0.5f + 22.0f, centerY), 5.0f,
                       WithAlpha(s_ToastWarning ? Colors::Warn : Colors::Good, 1.0f));
    Text(s_Toast, glm::vec2(centerX - width * 0.5f + 36.0f, centerY - CapHeightPerScale * Scale * 0.5f), Scale, Colors::Text);
}

float Ui::PanelLeft()
{
    return Left() + PanelMargin;
}

float Ui::PanelInner()
{
    return PanelWidth - PanelMargin * 2.0f;
}

void Ui::PanelBackground()
{
    Shapes().AddRect(Left(), Bottom(), Left() + PanelWidth, Top(), Colors::Panel);
    Shapes().AddRect(Left() + PanelWidth, Bottom(), Left() + PanelWidth + 1.0f, Top(), Colors::Hairline);
}

void Ui::ProgressBar(float left, float bottom, float width, float height, float fraction, const glm::vec3& color)
{
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    const float radius = height * 0.5f;
    Shapes().AddRoundedRect(left, bottom, left + width, bottom + height, radius, Colors::Track);
    // Something found always shows, even one in nine hundred.
    float filled = width * fraction;
    if (fraction > 0.0f && filled < height)
        filled = height;
    if (filled > 0.0f)
        Shapes().AddRoundedRect(left, bottom, left + filled, bottom + height, radius, WithAlpha(color, 1.0f));
}

float Ui::CardHeader(TexturedQuad* icon, float iconHeight, const std::string& title, const std::string& subtitle)
{
    const float left = PanelLeft();
    const float top = Top();
    float textLeft = left;
    if (icon && icon->m_Texture && icon->m_Texture->m_Height > 0)
    {
        icon->m_ProjectionMatrix = Projection();
        icon->m_ViewMatrix = nullptr;
        icon->m_Scale = iconHeight / static_cast<float>(icon->m_Texture->m_Height);
        icon->m_Position = glm::vec2(left + 22.0f, top - 56.0f);
        textLeft = left + 56.0f;
    }
    const float titleWidth = PanelInner() - (textLeft - left);
    if (subtitle.empty())
        Text(title, glm::vec2(textLeft, top - 66.0f), 0.50f, Colors::Text, ALIGN_LEFT, titleWidth);
    else
    {
        Text(title, glm::vec2(textLeft, top - 56.0f), 0.50f, Colors::Text, ALIGN_LEFT, titleWidth);
        Text(subtitle, glm::vec2(textLeft, top - 80.0f), 0.32f, Colors::Accent);
    }
    Shapes().AddRect(left, top - 101.0f, left + PanelInner(), top - 100.0f, Colors::Hairline);
    return top - 120.0f;
}

float Ui::StatusRow(float top, const std::string& text, const glm::vec3& dotColor, bool filled)
{
    const float left = PanelLeft();
    const float baseline = top - CapHeightPerScale * 0.40f;
    const glm::vec2 center(left + 7.0f, baseline + 6.5f);
    if (filled)
        Shapes().AddCircle(center, 5.5f, WithAlpha(dotColor, 1.0f));
    else
        Shapes().AddRing(center, 3.5f, 5.5f, WithAlpha(dotColor, 1.0f));
    Text(text, glm::vec2(left + 24.0f, baseline), 0.40f, Colors::Text);
    return top - 30.0f;
}

float Ui::Paragraph(float top, const std::string& text, float scale, const glm::vec3& color)
{
    // The font breaks lines every 50 pixels (at scale 1) and reports the height of the extra lines.
    const glm::vec2 size = Text(text, glm::vec2(PanelLeft(), top - CapHeightPerScale * scale), scale, color, ALIGN_LEFT, PanelInner());
    return top - size.y - 50.0f * scale;
}

std::string Ui::DistanceText(const glm::vec2& mapPosition)
{
    if (!SavefileIO::LoadedSavefile || !SavefileIO::CurrentState.hasPlayerPosition)
        return std::string();

    constexpr float MapScale = 0.25f;
    const glm::vec2 player(SavefileIO::CurrentState.playerX * MapScale, -SavefileIO::CurrentState.playerZ * MapScale);
    const glm::vec2 delta = mapPosition - player;
    const float meters = glm::length(delta) / MapScale;

    std::string text;
    if (meters >= 1000.0f)
    {
        const int tenths = static_cast<int>(meters / 100.0f + 0.5f);
        text = std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " km";
    }
    else
        text = std::to_string(static_cast<int>(meters + 0.5f)) + " m";

    if (meters >= 1.0f)
    {
        // North is up on the map; bearings grow clockwise.
        static const char* names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        float degrees = std::atan2(delta.x, delta.y) * 180.0f / 3.14159265f;
        if (degrees < 0.0f)
            degrees += 360.0f;
        text += std::string(" ") + names[static_cast<int>((degrees + 22.5f) / 45.0f) % 8];
    }
    text += " ";
    text += Localization::Get(SavefileIO::CurrentState.playerPositionIsLive ? Localization::Text::OfLink
                                                                             : Localization::Text::OfLastSave);
    return text;
}

void Ui::Coordinates(const glm::vec2& mapPosition)
{
    const int gameX = static_cast<int>(mapPosition.x / 0.25f);
    const int gameZ = static_cast<int>(-mapPosition.y / 0.25f);
    Text("X " + std::to_string(gameX) + "   Z " + std::to_string(gameZ),
         glm::vec2(PanelLeft(), Bottom() + FooterHeight + 16.0f), 0.30f, Colors::TextDim);
}
