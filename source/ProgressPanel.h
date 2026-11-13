#pragma once

// Two tabs of the left panel with counters of what the map does not show, numbers only.
namespace ProgressPanel
{
    // "Progress": quests and what is done in the world. Draws the rows from this y down (screen pixels).
    void Render(float top);
    // "Collection": the Hyrule Compendium by category and the armor.
    void RenderCollection(float top);
}
