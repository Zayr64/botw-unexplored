#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

class Font;
class ShapeBatch;

// The look BotW Unexplored and BotW Compendium share (zayr31 and 0.1.15, asked for by the user so the two apps look
// and work alike): one palette, and the pieces of the Switch's own menus. Coordinates are those of the ShapeBatch and
// the Font (y up); the Compendium turns its top-down ones into them.
namespace SwitchStyle
{
    // Text
    const glm::vec3 TextColor(1.0f, 1.0f, 1.0f);
    const glm::vec3 TextDim(0.70f, 0.75f, 0.78f);
    const glm::vec3 Accent(0.38f, 0.82f, 0.96f);
    const glm::vec3 Gold(0.86f, 0.73f, 0.40f);
    const glm::vec3 Good(0.47f, 0.86f, 0.56f);
    const glm::vec3 Warn(0.99f, 0.76f, 0.33f);
    // Surfaces: the bars (each app sets how much of what is under them shows through), a row that is on, thin lines
    const glm::vec3 BarRgb(0.03f, 0.045f, 0.055f);
    const glm::vec4 RowOn(0.17f, 0.36f, 0.48f, 0.92f);
    const glm::vec4 Hairline(1.0f, 1.0f, 1.0f, 0.10f);
    // The Switch's: its cyan for what is selected, neutral tiles, the track of a toggle that is off
    const glm::vec3 Cyan(0.10f, 0.80f, 0.98f);
    const glm::vec4 TileOn(0.24f, 0.25f, 0.27f, 1.0f);
    const glm::vec4 TileOff(0.15f, 0.155f, 0.165f, 1.0f);
    const glm::vec4 TrackOff(0.36f, 0.37f, 0.39f, 1.0f);
    // Inside the frame of a selected row of a list
    const glm::vec4 RowSelected(1.0f, 1.0f, 1.0f, 0.07f);

    // The frame of the selected item, as on the HOME menu: cyan, pulsing, around the item and a gap away from it.
    // Drawn before the item; `gap` is the colour behind it. The legend's cells stand apart (outset 6, thickness 3);
    // the rows of a table touch each other, so theirs is drawn inside the row (inset the row by `outset`).
    void SelectionFrame(ShapeBatch& shapes, float left, float bottom, float right, float top, float radius,
                        unsigned int frame, const glm::vec3& gap, float outset = 6.0f, float thickness = 3.0f);

    // A toggle as in System Settings (30 x 16), its top right corner given: cyan with the knob right when on, grey
    // with it left when off.
    constexpr float ToggleWidth = 30.0f;
    constexpr float ToggleHeight = 16.0f;
    void Toggle(ShapeBatch& shapes, float right, float top, bool on);

    // The mark of the tab in use under its content (icon and title: left to right, the title's baseline), and the
    // colour of a tab's title: gold, with a gold line under it, as in BotW Unexplored up to zayr30 (chosen by the user
    // on 26/11 over BotW Compendium's blue pill and over a line in the Switch's cyan).
    void TabMark(ShapeBatch& shapes, float left, float right, float baseline);
    glm::vec3 TabTitleColor(bool active);

    // The Switch's own symbol of a button ("A", "ZL", "+", "<", "Up/Down"...; filled, as the HOME menu's bar draws
    // them) from the console's Nintendo Extension font, as UTF-8; "" when there is none or the font is missing.
    std::string ButtonGlyphs(Font& font, const std::string& name);
    // The scale that draws those symbols `height` tall, and the baseline that centres them on `centerY`.
    float GlyphScale(Font& font, float height);
    float GlyphBaseline(Font& font, float scale, float centerY);
    // The height of a capital letter of the font at a scale.
    float CapHeight(Font& font, float scale);

    // A small label with a rounded background and, optionally, a coloured dot (where the data comes from). `top`:
    // its top edge; `anchorRight`: x is its right edge. Returns its width.
    constexpr float ChipHeight = 30.0f;
    float ChipWidth(Font& font, const std::string& text, bool dot);
    float Chip(ShapeBatch& shapes, Font& font, const std::string& text, float x, float top, bool anchorRight,
               const glm::vec4& background, const glm::vec3* dotColor = nullptr);

    // When a save was made, as the game keeps it (its local time written as if it were UTC): "24/11 16:11"; "" for 0.
    std::string SaveDate(uint32_t seconds);
    // Where the data comes from, in the same words in both apps: "Live from the game" (green dot), "Save copy · 24/11
    // 16:11" (BotW is open and a copy is shown: orange dot), "Save · 24/11 16:11". `brief`: the date alone, for
    // little room.
    enum class Source { Save, Copy, Live };
    std::string SourceText(Source source, uint32_t saveTime, bool brief, glm::vec3& dotColor, bool& hasDot);
}
