#pragma once

#include <string>

#include <glm/vec3.hpp>
#include <switch.h>

// BotW Companion (the single app, 26/11; the study is docs/estudo-app-unico.md in the project): the Compendium's four
// tabs are sections of the map's app, which owns the window, the buttons and the map. Here the header of every section
// (the map first, then Dishes, Reserve, Sell, EXP), the four tabs and their footer. L and R change the section.
// Built without BOTW_COMPANION, CompendiumMain.cpp keeps its own main() and is the BotW Compendium as before.
namespace CompendiumSection
{
    constexpr int Sections = 5; // 0 the map, 1 to 4 the Compendium's tabs

    // After EGL: the font, the shapes and the icons of the tabs. Nothing is read yet: the save and the cooking wait for
    // the first visit to a tab, so the map opens as fast as before.
    void Init();
    void Destroy();

    // The section on the screen, and L or R pressed this frame. Returns the section to show from now on.
    int Switch(int section, u64 down);

    // One frame, drawn over what is already on the screen: the header always, and for the sections 1 to 4 the tab and
    // its footer, with the buttons of this frame. On the map (0) the header shows the map's own source of the data.
    void Frame(int section, u64 down, u64 held, const std::string& mapSource, const glm::vec3& mapDot, bool mapHasDot);
    // After the frame is on the screen: the slow work it asked for (reading the save, cooking).
    void AfterFrame(int section);

    // "-": the save or the running game is read again, the next time a tab is shown (or now, on a tab).
    void Reload(int section);

    // The height of the header on the screen, in pixels: the map's panels start under it.
    float HeaderHeight();
}
