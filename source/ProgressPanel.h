#pragma once

// Two tabs of the left panel with counters of what the map does not show, numbers only.
namespace ProgressPanel
{
    // "Progress": quests and what is done in the world. Draws the rows from this y down (screen pixels).
    void Render(float top);
    // "Collection": the Hyrule Compendium by category and the armor.
    void RenderCollection(float top);
    // When the rows do not fit above the bottom bar, Up and Down move them a row at a time.
    void Scroll(bool collection, int rows);
    bool Scrolls(bool collection);
}
