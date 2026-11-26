#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

class Texture2D;

// The game's icons, as PNGs on the SD card (sdmc:/switch/botw-compendium/icons/): items/<actor>.png and
// ui/<name>.png, made on the PC from the user's dump (compendium/tools/gen_icons.py). They are never in the app or
// the repository. A missing icon is simply not drawn.
namespace Icons
{
    void Init();
    void Destroy();

    // Queue an icon `size` pixels wide at x, top (screen pixels from the top left), multiplied by `tint`.
    void Item(const std::string& actor, float x, float top, float size, const glm::vec4& tint = glm::vec4(1.0f));
    void Ui(const std::string& name, float x, float top, float size, const glm::vec4& tint = glm::vec4(1.0f));
    // Whether ui/<name>.png is on the SD (to make room for it).
    bool HasUi(const std::string& name);
    // The enemy's picture from the Hyrule Compendium (enemies/<actor>.jpg).
    void Enemy(const std::string& actor, float x, float top, float size);

    // Draws what was queued (after the shapes, before the text) and empties the queue.
    void Flush(const glm::mat4& projection, float screenHeight);
}
