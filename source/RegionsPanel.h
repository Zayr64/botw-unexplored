#pragma once

#include <string>

#include <glm/vec2.hpp>
#include <switch.h>

// The "Regions" tab of the left panel: the fifteen regions of the map, how much of each is found, and the
// choice of one to show alone on the map.
namespace RegionsPanel
{
    // The name of a region, or "Unknown region" until its tower is activated; "Whole map" for Regions::None.
    std::string Label(int region);
    // The Koroks, shrines and locations of a region (what the map completion counts); all of them for
    // Regions::None.
    void Count(int region, int& found, int& total);

    void Update(u64 buttonsPressed);          // Up, Down and A
    void Touch(const glm::vec2& position);    // a tap on a row chooses it
    void Render(float top);                   // draws the rows from this y down (screen pixels)

    // The highlighted row is the whole map (what A would choose).
    bool HighlightIsWholeMap();
}
