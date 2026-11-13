#include "RegionsPanel.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "Data.h"
#include "Graphics/Font.h"
#include "Graphics/ShapeBatch.h"
#include "Legend.h"
#include "Localization.h"
#include "Map.h"
#include "MapLocation.h"
#include "MapObject.hpp"
#include "Regions.h"
#include "SavefileIO.h"
#include "Ui.h"

namespace
{
    constexpr float RowHeight = 27.0f;
    constexpr float RowGap = 3.0f;
    constexpr float RowRadius = 6.0f;
    constexpr float LabelScale = 0.36f;
    constexpr float CapHeight = 34.4f * LabelScale; // height of a capital letter at that scale

    int s_Highlighted = 0;  // index into Rows()
    float s_Top = 0.0f;     // where the rows were last drawn, for the touch

    // The name of a region is that of its tower, which the game only tells once the tower is activated.
    bool IsKnown(int region)
    {
        return SavefileIO::IsLocationStateDone(Regions::TowerLocationHash(region));
    }

    // The whole map first, then the regions known by name in alphabetical order, then the others.
    std::vector<int> Rows()
    {
        std::vector<int> known, unknown;
        for (int region = 0; region < Regions::Count; ++region)
            (IsKnown(region) ? known : unknown).push_back(region);
        std::sort(known.begin(), known.end(), [](int a, int b)
        {
            return std::strcmp(Regions::Name(a), Regions::Name(b)) < 0;
        });
        std::vector<int> rows;
        rows.push_back(Regions::None);
        rows.insert(rows.end(), known.begin(), known.end());
        rows.insert(rows.end(), unknown.begin(), unknown.end());
        return rows;
    }

    void Choose(int row, const std::vector<int>& rows)
    {
        if (row < 0 || row >= static_cast<int>(rows.size()))
            return;
        s_Highlighted = row;
        Map::SetRegionFilter(rows[row]);
    }
}

std::string RegionsPanel::Label(int region)
{
    if (region < 0 || region >= Regions::Count)
        return Localization::Get(Localization::Text::WholeMap);
    return IsKnown(region) ? std::string(Regions::Name(region)) : Localization::Get(Localization::Text::UnknownRegion);
}

void RegionsPanel::Count(int region, int& found, int& total)
{
    found = 0;
    total = 0;
    if (Map::m_Koroks == nullptr || Map::m_Shrines == nullptr || Map::m_Locations == nullptr)
        return;
    const auto add = [&](int objectRegion, bool objectFound)
    {
        if (region != Regions::None && objectRegion != region)
            return;
        ++total;
        found += objectFound ? 1 : 0;
    };
    for (int i = 0; i < Data::KoroksCount; ++i)
        add(Map::m_Koroks[i].m_Region, Map::m_Koroks[i].m_Found);
    for (int i = 0; i < Data::ShrineCount; ++i)
        add(Map::m_Shrines[i].m_Region, Map::m_Shrines[i].m_Found);
    for (int i = 0; i < Data::LocationsCount; ++i)
        add(Map::m_Locations[i].m_Region, Map::m_Locations[i].m_Found);
}

void RegionsPanel::Update(u64 buttonsPressed)
{
    const std::vector<int> rows = Rows();
    const int count = static_cast<int>(rows.size());
    if (s_Highlighted >= count)
        s_Highlighted = 0;
    if (buttonsPressed & HidNpadButton_Down)
        s_Highlighted = (s_Highlighted + 1) % count;
    if (buttonsPressed & HidNpadButton_Up)
        s_Highlighted = (s_Highlighted + count - 1) % count;
    if (buttonsPressed & HidNpadButton_A)
        Choose(s_Highlighted, rows);
}

void RegionsPanel::Touch(const glm::vec2& position)
{
    const float left = Ui::PanelLeft();
    if (position.x < left || position.x > left + Ui::PanelInner() || position.y > s_Top)
        return;
    const float offset = s_Top - position.y;
    const int row = static_cast<int>(offset / (RowHeight + RowGap));
    if (offset - row * (RowHeight + RowGap) <= RowHeight)
        Choose(row, Rows());
}

bool RegionsPanel::HighlightIsWholeMap()
{
    return s_Highlighted == 0;
}

void RegionsPanel::Render(float top)
{
    s_Top = top;
    const float left = Ui::PanelLeft();
    const float right = left + Ui::PanelInner();
    const std::vector<int> rows = Rows();
    if (s_Highlighted >= static_cast<int>(rows.size()))
        s_Highlighted = 0;
    ShapeBatch& shapes = Ui::Shapes();

    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
    {
        const int region = rows[i];
        const float rowTop = top - (RowHeight + RowGap) * i;
        const float rowBottom = rowTop - RowHeight;
        const bool shown = region == Map::m_RegionFilter; // the one the map shows (the whole map included)
        const bool highlighted = i == s_Highlighted;

        if (highlighted)
        {
            shapes.AddRoundedRect(left - 2.0f, rowBottom - 2.0f, right + 2.0f, rowTop + 2.0f, RowRadius + 2.0f,
                                  glm::vec4(Ui::Colors::Accent, 1.0f));
            shapes.AddRoundedRect(left, rowBottom, right, rowTop, RowRadius,
                                  shown ? glm::vec4(0.17f, 0.36f, 0.48f, 1.0f) : glm::vec4(0.10f, 0.13f, 0.15f, 1.0f));
        }
        else
            shapes.AddRoundedRect(left, rowBottom, right, rowTop, RowRadius, shown ? Ui::Colors::RowOn : Ui::Colors::RowOff);

        int found = 0, total = 0;
        Count(region, found, total);
        const bool known = region == Regions::None || IsKnown(region);
        const float baseline = rowTop - (RowHeight + CapHeight) * 0.5f;
        Ui::Text(Label(region), glm::vec2(left + 12.0f, baseline), LabelScale,
                 known ? Ui::Colors::Text : Ui::Colors::TextDim);
        Ui::Text(std::to_string(found) + "/" + std::to_string(total), glm::vec2(right - 12.0f, baseline), LabelScale,
                 total > 0 && found >= total ? Ui::Colors::Good : (known ? Ui::Colors::Text : Ui::Colors::TextDim), ALIGN_RIGHT);
    }
    Ui::FlushShapes();
}
