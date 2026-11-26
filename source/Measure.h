#pragma once

#include <string>

// Phase 0 of the single app (26/11): how long each step of opening takes and how much memory is left after it, in
// log.txt, to size the app that will hold the map and the Compendium together. One line per step:
//   MEASURE <step>: <ms since Start> ms (+<ms since the last step>), heap free <MB>, images <MB>
// "images" is the video memory of the Texture2D images alive at that moment (the map, its HD chunks, the icons);
// the glyphs of the font are not in it.
namespace Measure
{
    void Start();
    void Step(const std::string& step);
}
