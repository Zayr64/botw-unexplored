#include "Ui.h"

#include <cmath>
#include <sstream>

#include <glm/geometric.hpp>

#include "Graphics/Font.h"
#include "Graphics/Quad.h"
#include "Graphics/ShapeBatch.h"
#include "Graphics/Texture2D.h"
#include "Localization.h"
#include "Map.h"
#include "SavefileIO.h"

namespace Ui
{
    namespace Colors
    {
        const glm::vec4 Panel(0.045f, 0.065f, 0.08f, 0.90f);
        const glm::vec4 Bar(0.03f, 0.045f, 0.055f, 0.84f);
        const glm::vec4 Hairline(1.0f, 1.0f, 1.0f, 0.10f);
        const glm::vec4 RowOn(0.17f, 0.36f, 0.48f, 0.92f);
        const glm::vec4 RowOff(1.0f, 1.0f, 1.0f, 0.07f);
        const glm::vec4 Track(1.0f, 1.0f, 1.0f, 0.16f);
        const glm::vec3 Text(1.0f, 1.0f, 1.0f);
        const glm::vec3 TextDim(0.70f, 0.75f, 0.78f);
        const glm::vec3 Accent(0.38f, 0.82f, 0.96f);
        const glm::vec3 Gold(0.86f, 0.73f, 0.40f);
        const glm::vec3 Good(0.47f, 0.86f, 0.56f);
        const glm::vec3 Warn(0.99f, 0.76f, 0.33f);
    }
}

namespace
{
    constexpr float HintScale = 0.36f;
    constexpr float CapHeightPerScale = 34.4f; // height of a capital letter of the 48 px font
    constexpr int ToastFrames = 210;           // about 3.5 s

    ShapeBatch* s_Shapes = nullptr;
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

    float CapWidth(const std::string& button)
    {
        if (button.size() == 1)
            return 24.0f;
        return Ui::TextWidth(button, 0.28f) + 14.0f;
    }

    // One button cap with its left edge at x, centred on centerY.
    void DrawCap(const std::string& button, float x, float centerY)
    {
        ShapeBatch& shapes = Ui::Shapes();
        const float width = CapWidth(button);
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
    s_Shapes->Render(Map::m_ProjectionMatrix);
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

void Ui::Footer(const std::vector<Hint>& hints, const std::string& context)
{
    const float bottom = Map::m_ScreenBottom;
    const float top = bottom + FooterHeight;
    const float centerY = bottom + FooterHeight * 0.5f;
    const float baseline = centerY - CapHeightPerScale * HintScale * 0.5f;

    Shapes().AddRect(Map::m_ScreenLeft, bottom, Map::m_ScreenRight, top, Colors::Bar);
    Shapes().AddRect(Map::m_ScreenLeft, top, Map::m_ScreenRight, top + 1.0f, Colors::Hairline);

    float x = Map::m_ScreenRight - 22.0f;
    for (std::vector<Hint>::const_reverse_iterator hint = hints.rbegin(); hint != hints.rend(); ++hint)
    {
        x -= TextWidth(hint->label, HintScale);
        Text(hint->label, glm::vec2(x, baseline), HintScale, Colors::Text);
        x -= 7.0f;

        std::vector<std::string> buttons;
        std::istringstream names(hint->buttons);
        for (std::string name; names >> name;)
            buttons.push_back(name);
        for (std::vector<std::string>::const_reverse_iterator button = buttons.rbegin(); button != buttons.rend(); ++button)
        {
            x -= CapWidth(*button);
            DrawCap(*button, x, centerY);
            x -= 4.0f;
        }
        x -= 16.0f;
    }

    if (!context.empty())
        Text(context, glm::vec2(Map::m_ScreenLeft + 22.0f, baseline), HintScale, Colors::TextDim);
}

float Ui::Chip(const std::string& text, float x, float top, bool anchorRight, const glm::vec3* dotColor)
{
    constexpr float Height = 30.0f;
    constexpr float Scale = 0.34f;
    const float dotSpace = dotColor ? 16.0f : 0.0f;
    const float width = TextWidth(text, Scale) + 28.0f + dotSpace;
    const float left = anchorRight ? x - width : x;
    const float centerY = top - Height * 0.5f;

    Shapes().AddRoundedRect(left, top - Height, left + width, top, Height * 0.5f, Colors::Bar);
    if (dotColor)
        Shapes().AddCircle(glm::vec2(left + 18.0f, centerY), 5.0f, WithAlpha(*dotColor, 1.0f));
    Text(text, glm::vec2(left + 14.0f + dotSpace, centerY - CapHeightPerScale * Scale * 0.5f), Scale, Colors::Text);
    return width;
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
    const float bottom = Map::m_ScreenBottom + FooterHeight + 14.0f;
    const float centerY = bottom + Height * 0.5f;
    // Centred on the part of the screen the panel does not cover.
    const float centerX = Map::m_ViewShift;

    Shapes().AddRoundedRect(centerX - width * 0.5f, bottom, centerX + width * 0.5f, bottom + Height, Height * 0.5f,
                            glm::vec4(0.02f, 0.03f, 0.04f, 0.92f));
    Shapes().AddCircle(glm::vec2(centerX - width * 0.5f + 22.0f, centerY), 5.0f,
                       WithAlpha(s_ToastWarning ? Colors::Warn : Colors::Good, 1.0f));
    Text(s_Toast, glm::vec2(centerX - width * 0.5f + 36.0f, centerY - CapHeightPerScale * Scale * 0.5f), Scale, Colors::Text);
}

float Ui::PanelLeft()
{
    return Map::m_ScreenLeft + PanelMargin;
}

float Ui::PanelInner()
{
    return PanelWidth - PanelMargin * 2.0f;
}

void Ui::PanelBackground()
{
    Shapes().AddRect(Map::m_ScreenLeft, Map::m_ScreenBottom, Map::m_ScreenLeft + PanelWidth, Map::m_ScreenTop, Colors::Panel);
    Shapes().AddRect(Map::m_ScreenLeft + PanelWidth, Map::m_ScreenBottom, Map::m_ScreenLeft + PanelWidth + 1.0f,
                     Map::m_ScreenTop, Colors::Hairline);
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
    const float top = Map::m_ScreenTop;
    float textLeft = left;
    if (icon && icon->m_Texture && icon->m_Texture->m_Height > 0)
    {
        icon->m_ProjectionMatrix = &Map::m_ProjectionMatrix;
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
         glm::vec2(PanelLeft(), Map::m_ScreenBottom + FooterHeight + 16.0f), 0.30f, Colors::TextDim);
}
