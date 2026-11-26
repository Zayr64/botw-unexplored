#pragma once

#include <string>
#include <vector>

#include "Graphics/Quad.h"
#include "LegendCategories.h"

class Legend;

// One category of the legend: icon, name, how many were found and a switch to show it on the map.
class IconButton
{
public:
    using ButtonTypes = LegendCategories::ButtonTypes;
    static constexpr ButtonTypes Koroks = ButtonTypes::Koroks;
    static constexpr ButtonTypes Shrines = ButtonTypes::Shrines;
    static constexpr ButtonTypes Hinoxes = ButtonTypes::Hinoxes;
    static constexpr ButtonTypes Taluses = ButtonTypes::Taluses;
    static constexpr ButtonTypes Moldugas = ButtonTypes::Moldugas;
    static constexpr ButtonTypes Locations = ButtonTypes::Locations;
    static constexpr ButtonTypes ArmorChests = ButtonTypes::ArmorChests;
    static constexpr ButtonTypes WeaponChests = ButtonTypes::WeaponChests;
    static constexpr ButtonTypes MaterialChests = ButtonTypes::MaterialChests;
    static constexpr ButtonTypes Walls = ButtonTypes::Walls;
    static constexpr ButtonTypes Quests = ButtonTypes::Quests;
    static constexpr ButtonTypes Towers = ButtonTypes::Towers;
    static constexpr ButtonTypes GreatFairies = ButtonTypes::GreatFairies;
    static constexpr ButtonTypes HeroPath = ButtonTypes::HeroPath;

public:
    // Position is the top-left corner
    IconButton(ButtonTypes type, glm::vec2 position, float width, float height, bool compact = false);

    void DrawShapes();
    void DrawIcon();
    void DrawText();

    bool Click(Legend* legend);
    bool Click(Legend* legend, bool toggled);

    // How many of this category were found, and how many there are.
    void Count(int& found, int& total) const;

public:
    TexturedQuad m_Icon;

    bool m_IsSelected = false;
    bool m_IsToggled = false;

    ButtonTypes m_Type = ButtonTypes::Koroks;
    bool m_Compact = false; // a cell of the narrow two-column legend: icon and count, the name under them

    glm::vec2 m_Position;
    float m_Width = 0.0f;
    float m_Height = 0.0f;
};

class Legend
{
public:
    Legend();

    void Update();
    void Render();

    bool IsPositionOnLegend(glm::vec2 position);
    void UpdateSelectedButton();
    bool ShouldShow(bool found) const;

    ~Legend();

public:
    std::vector<IconButton*> m_Buttons;

    float m_Width = 380.0f;

    int m_PrevTouchCount = 0;

    int m_HighlightedButton = 0; // a place in the order of the columns (Legend.cpp), not a category

    bool m_IsOpen = true;

    bool m_Show[LegendCategories::ButtonCount];
};
