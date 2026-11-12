#pragma once

#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

class ShapeBatch;
class TexturedQuad;

// The shared look of the interface: colours, the left panel, the bottom bar with button hints,
// chips, toasts and the pieces of an object card. Everything is in screen pixels, origin at the centre.
namespace Ui
{
    constexpr float FooterHeight = 46.0f;
    constexpr float PanelWidth = 380.0f; // legend and cards
    constexpr float PanelMargin = 24.0f;

    namespace Colors
    {
        extern const glm::vec4 Panel;
        extern const glm::vec4 Bar;
        extern const glm::vec4 Hairline;
        extern const glm::vec4 RowOn;
        extern const glm::vec4 RowOff;
        extern const glm::vec4 Track;
        extern const glm::vec3 Text;
        extern const glm::vec3 TextDim;
        extern const glm::vec3 Accent;
        extern const glm::vec3 Gold;
        extern const glm::vec3 Good;
        extern const glm::vec3 Warn;
    }

    void Init();
    void Destroy();
    void Tick(); // once per frame
    unsigned int Frame();

    ShapeBatch& Shapes();
    void FlushShapes(); // draws the shapes added so far

    float TextWidth(const std::string& text, float scale);
    glm::vec2 Text(const std::string& text, const glm::vec2& position, float scale, const glm::vec3& color,
                   int align = 0, float maxWidth = -1.0f);

    // One hint of the bottom bar: the buttons (names separated by spaces, e.g. "ZL ZR") and what they do.
    struct Hint
    {
        std::string buttons;
        std::string label;
    };
    void Footer(const std::vector<Hint>& hints, const std::string& context);

    // A small label with a rounded background. `anchorRight`: x is its right edge. Returns its width.
    float Chip(const std::string& text, float x, float top, bool anchorRight, const glm::vec3* dotColor = nullptr);

    // A message above the bottom bar for a few seconds.
    void Toast(const std::string& text, bool warning = false);
    void DrawToast();

    // Left panel and the common parts of a card.
    float PanelLeft();   // x of the content
    float PanelInner();  // width of the content
    void PanelBackground();
    void ProgressBar(float left, float bottom, float width, float height, float fraction, const glm::vec3& color);
    // The pieces of a card take the y of their top edge and return the top of what comes next.
    // Icon, title and (optional) subtitle with a line below.
    float CardHeader(TexturedQuad* icon, float iconHeight, const std::string& title, const std::string& subtitle);
    // "(dot) text" line; a hollow dot for something still to do.
    float StatusRow(float top, const std::string& text, const glm::vec3& dotColor, bool filled = true);
    // Text wrapped to the width of the panel.
    float Paragraph(float top, const std::string& text, float scale, const glm::vec3& color);
    // "314 m NE of Link"; empty without a known position.
    std::string DistanceText(const glm::vec2& mapPosition);
    // "X -672  Z 1513" at the bottom of the panel.
    void Coordinates(const glm::vec2& mapPosition);
}
