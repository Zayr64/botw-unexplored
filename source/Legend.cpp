#include "Legend.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "MapCompletion.h"

#include "Graphics/ShapeBatch.h"
#include "Graphics/Texture2D.h"
#include "Map.h"
#include "MapObject.hpp"
#include "MapLocation.h"
#include "SavefileIO.h"
#include "Localization.h"
#include "ProgressPanel.h"
#include "QuestMap.h"
#include "Regions.h"
#include "RegionsPanel.h"
#include "Ui.h"

namespace
{
    // The panel's header (from the top of the screen) and the ten rows of the legend: as up to zayr21 when they fit
    // above the bottom bar, else a tighter header and shorter rows (a bigger zoom leaves less height).
    struct PanelLayout
    {
        float tabs, completion, bar, source, rowsTop, rowHeight, rowGap;
    };

    PanelLayout Layout()
    {
        const float height = Ui::Top() - (Ui::Bottom() + Ui::FooterHeight + 8.0f);
        PanelLayout layout = {52.0f, 88.0f, 106.0f, 132.0f, 152.0f, 44.0f, 6.0f};
        if (layout.rowsTop + IconButton::ButtonTypes::Count * (layout.rowHeight + layout.rowGap) <= height)
            return layout;
        layout = {46.0f, 78.0f, 94.0f, 116.0f, 132.0f, 0.0f, 5.0f};
        layout.rowHeight = std::max(30.0f, std::floor((height - layout.rowsTop) / IconButton::ButtonTypes::Count) - layout.rowGap);
        return layout;
    }

    constexpr float TabScale = 0.38f; // the four titles fit the width of the panel
    constexpr float TabGap = 14.0f;
    const Localization::Text tabTitles[Legend::TabCount] = {
        Localization::Text::Legend, Localization::Text::TabProgress, Localization::Text::TabRegions,
        Localization::Text::TabCollection
    };

    // Left and right edge of the title of each tab.
    void TabEdges(float edges[Legend::TabCount][2])
    {
        float x = Ui::PanelLeft();
        for (int tab = 0; tab < Legend::TabCount; ++tab)
        {
            edges[tab][0] = x;
            x += Ui::TextWidth(Localization::Get(tabTitles[tab]), TabScale);
            edges[tab][1] = x;
            x += TabGap;
        }
    }

    // The tab whose title is under this point of the screen, or -1.
    int TabAt(const glm::vec2& position)
    {
        const float baseline = Ui::Top() - Layout().tabs;
        if (position.y < baseline - 14.0f || position.y > baseline + 30.0f)
            return -1;
        float edges[Legend::TabCount][2];
        TabEdges(edges);
        for (int tab = 0; tab < Legend::TabCount; ++tab)
            if (position.x >= edges[tab][0] - TabGap * 0.5f && position.x <= edges[tab][1] + TabGap * 0.5f)
                return tab;
        return -1;
    }

    constexpr float RowRadius = 8.0f;

    Localization::Text LabelOf(IconButton::ButtonTypes type)
    {
        switch (type)
        {
        case IconButton::ArmorChests: return Localization::Text::ArmorChests;
        case IconButton::WeaponChests: return Localization::Text::WeaponChests;
        case IconButton::MaterialChests: return Localization::Text::MaterialChests;
        case IconButton::Walls: return Localization::Text::Walls;
        case IconButton::HeroPath: return Localization::Text::HeroPath;
        case IconButton::Quests: return Localization::Text::Quests;
        default:
            // Koroks to Moldugas, in the order of the categories
            return static_cast<Localization::Text>(static_cast<int>(Localization::Text::Koroks) + static_cast<int>(type));
        }
    }

    // Opened and existing chests of one of the groups (those of the other mode or of the DLC do not count).
    template <typename T>
    void CountChests(const MapObject<T>* chests, int count, int& found, int& total)
    {
        for (int i = 0; i < count; ++i)
        {
            if (chests[i].m_Excluded || !Map::InRegionFilter(chests[i].m_Region))
                continue;
            ++total;
            found += chests[i].m_Found;
        }
    }

    int MapEntriesFound()
    {
        // The locations count although the app does not draw them: the number is the game's.
        static_assert(Data::KoroksCount + Data::ShrineCount + Data::LocationsCount == MapCompletion::TotalEntries,
                      "Map completion total must match the tracked data");

        int found = 0;
        for (int i = 0; i < Data::KoroksCount; ++i) found += Map::m_Koroks[i].m_Found;
        for (int i = 0; i < Data::ShrineCount; ++i) found += Map::m_Shrines[i].m_Found;
        for (int i = 0; i < Data::LocationsCount; ++i) found += Map::m_Locations[i].m_Found;
        return found;
    }
}

Legend::Legend()
{
    m_Width = Ui::PanelWidth;
    const float left = Ui::PanelLeft();
    const float top = Ui::Top();
    const PanelLayout layout = Layout();

    for (int i = 0; i < IconButton::ButtonTypes::Count; i++)
    {
        const float rowTop = top - layout.rowsTop - (layout.rowHeight + layout.rowGap) * i;
        m_Buttons.push_back(new IconButton((IconButton::ButtonTypes)i, glm::vec2(left, rowTop), Ui::PanelInner(),
                                           layout.rowHeight));
        m_Show[i] = false;
    }

    // Show koroks, shrines and the Hero's Path by default (as SettingsIO::Settings does)
    m_Buttons[0]->Click(this, true);
    m_Buttons[1]->Click(this, true);
    m_Buttons[IconButton::HeroPath]->Click(this, true);
    m_Buttons[IconButton::Quests]->Click(this, true);

    // Set first highlighted button
    UpdateSelectedButton();
}

void Legend::Update()
{
    const u64 buttonsPressed = padGetButtonsDown(Map::m_Pad);

    // Left and right change the tab
    if (buttonsPressed & HidNpadButton_Right)
        m_Tab = (m_Tab + 1) % TabCount;
    if (buttonsPressed & HidNpadButton_Left)
        m_Tab = (m_Tab + TabCount - 1) % TabCount;

    HidTouchScreenState state={0};
    if (hidGetTouchScreenStates(&state, 1))
    {
        // A new touch
        if (state.count != m_PrevTouchCount)
        {
            m_PrevTouchCount = state.count;

            if (state.count == 1)
            {
                // Convert to more suitable coords
                glm::vec2 touchPosition = Ui::FromScreen(glm::vec2(state.touches[0].x - Map::m_CameraWidth / 2,
                                                                   -(state.touches[0].y - Map::m_CameraHeight / 2)));

                const int tab = TabAt(touchPosition);
                if (tab >= 0)
                    m_Tab = tab;
                else if (m_Tab == TabRegions)
                    RegionsPanel::Touch(touchPosition);
                else if (m_Tab == TabLegend)
                {
                    for (unsigned int i = 0; i < m_Buttons.size(); i++)
                    {
                        IconButton* button = m_Buttons[i];

                        float left = button->m_Position.x;
                        float right = button->m_Position.x + button->m_Width;
                        float top = button->m_Position.y;
                        float bottom = button->m_Position.y - button->m_Height;

                        if (touchPosition.x > left && touchPosition.x < right &&
                            touchPosition.y > bottom && touchPosition.y < top)
                        {
                            button->Click(this);

                            m_HighlightedButton = i;
                            UpdateSelectedButton();

                            break;
                        }
                    }
                }
            }
        }
    }

    if (m_Tab == TabRegions)
        RegionsPanel::Update(buttonsPressed);
    // Progress and Collection scroll when their rows do not fit
    if (m_Tab == TabProgress || m_Tab == TabCollection)
    {
        const bool collection = m_Tab == TabCollection;
        if (buttonsPressed & HidNpadButton_Down)
            ProgressPanel::Scroll(collection, 1);
        if (buttonsPressed & HidNpadButton_Up)
            ProgressPanel::Scroll(collection, -1);
    }
    if (m_Tab != TabLegend)
        return;

    // Up and down go around
    const int count = static_cast<int>(m_Buttons.size());
    if (buttonsPressed & HidNpadButton_Down)
    {
        m_HighlightedButton = (m_HighlightedButton + 1) % count;
        UpdateSelectedButton();
    }
    if (buttonsPressed & HidNpadButton_Up)
    {
        m_HighlightedButton = (m_HighlightedButton + count - 1) % count;
        UpdateSelectedButton();
    }
    if (buttonsPressed & HidNpadButton_A)
    {
        // Bounds check
        if (m_HighlightedButton >= 0 && m_HighlightedButton < count)
            m_Buttons[m_HighlightedButton]->Click(this);
    }
}

void Legend::UpdateSelectedButton()
{
    for (unsigned int i = 0; i < m_Buttons.size(); i++)
        m_Buttons[i]->m_IsSelected = false;

    m_Buttons[m_HighlightedButton]->m_IsSelected = true;
}

void Legend::Render()
{
    const float left = Ui::PanelLeft();
    const float right = left + Ui::PanelInner();
    const float top = Ui::Top();
    const PanelLayout layout = Layout();
    const float TabsBaseline = layout.tabs;
    const float RowsTop = layout.rowsTop;

    Ui::PanelBackground();

    // The tabs: the one in use in gold, with a line under it
    float edges[TabCount][2];
    TabEdges(edges);
    for (int tab = 0; tab < TabCount; ++tab)
    {
        const bool active = tab == m_Tab;
        Ui::Text(Localization::Get(tabTitles[tab]), glm::vec2(edges[tab][0], top - TabsBaseline), TabScale,
                 active ? Ui::Colors::Gold : Ui::Colors::TextDim);
        if (active)
            Ui::Shapes().AddRect(edges[tab][0], top - TabsBaseline - 10.0f, edges[tab][1], top - TabsBaseline - 8.0f,
                                 glm::vec4(Ui::Colors::Gold, 1.0f));
    }

    // What the game calls map completion: Koroks, shrines and locations.
    const int found = MapEntriesFound();
    Ui::Text(Localization::Get(Localization::Text::MapCompletionLong), glm::vec2(left, top - layout.completion), 0.36f,
             Ui::Colors::TextDim);
    Ui::Text(MapCompletion::Format(MapCompletion::Hundredths(found), '.'), glm::vec2(right, top - layout.completion), 0.40f,
             Ui::Colors::Text, ALIGN_RIGHT);
    Ui::ProgressBar(left, top - layout.bar, Ui::PanelInner(), 6.0f,
                    static_cast<float>(found) / static_cast<float>(MapCompletion::TotalEntries), Ui::Colors::Gold);

    // Where the progress comes from and, on the right, the region the map is showing alone
    glm::vec3 dotColor;
    bool hasDot = false;
    Ui::Text(Map::SourceText(dotColor, hasDot), glm::vec2(left, top - layout.source), 0.32f, Ui::Colors::TextDim);
    if (Map::m_RegionFilter >= 0)
        Ui::Text(RegionsPanel::Label(Map::m_RegionFilter), glm::vec2(right, top - layout.source), 0.32f,
                 Ui::Colors::Accent, ALIGN_RIGHT);

    if (m_Tab == TabProgress)
    {
        ProgressPanel::Render(top - RowsTop);
        return;
    }
    if (m_Tab == TabRegions)
    {
        RegionsPanel::Render(top - RowsTop);
        return;
    }
    if (m_Tab == TabCollection)
    {
        ProgressPanel::RenderCollection(top - RowsTop);
        return;
    }

    for (unsigned int i = 0; i < m_Buttons.size(); i++)
        m_Buttons[i]->DrawShapes();
    Ui::FlushShapes();

    for (unsigned int i = 0; i < m_Buttons.size(); i++)
    {
        m_Buttons[i]->DrawIcon();
        m_Buttons[i]->DrawText();
    }
}

bool Legend::IsPositionOnLegend(glm::vec2 position)
{
    const float left = Ui::Left();
    if (position.x > left && position.x < left + m_Width) {
        if (position.y > Ui::Bottom() && position.y < Ui::Top()) {
            return true;
        }
    }

    return false;
}

// The map shows only what is still missing.
bool Legend::ShouldShow(bool found) const
{
    return !found;
}

Legend::~Legend()
{
    for (unsigned int i = 0; i < m_Buttons.size(); i++)
        delete m_Buttons[i];
}

IconButton::IconButton(ButtonTypes type, glm::vec2 position, float width, float height)
{
    m_Width = width;
    m_Height = height;
    m_Position = position;
    m_Type = type;

    std::string iconPath;
    switch (type)
    {
    case Koroks: iconPath = "romfs:/korokseed.png"; break;
    case Shrines: iconPath = "romfs:/shrine.png"; break;
    case Hinoxes: iconPath = "romfs:/hinox.png"; break;
    case Taluses: iconPath = "romfs:/talus.png"; break;
    case Moldugas: iconPath = "romfs:/molduga.png"; break;
    case ArmorChests: iconPath = Data::IconOf(Data::ChestGroup::Armor); break;
    case WeaponChests: iconPath = Data::IconOf(Data::ChestGroup::Weapon); break;
    case MaterialChests: iconPath = Data::IconOf(Data::ChestGroup::Material); break;
    case Walls: iconPath = "romfs:/wall.png"; break;
    case HeroPath: iconPath = "romfs:/heropath.png"; break;
    case Quests: iconPath = "romfs:/quest.png"; break;
    default: break;
    }

    m_Icon.Create(iconPath);
    // Every icon about 28 pixels tall, whatever the size of its image.
    if (m_Icon.m_Texture && m_Icon.m_Texture->m_Height > 0)
        m_Icon.m_Scale = 28.0f / static_cast<float>(m_Icon.m_Texture->m_Height);
    m_Icon.m_Position = glm::vec2(position.x + 34.0f, position.y - height / 2.0f);
    m_Icon.m_ProjectionMatrix = Ui::Projection();
}

void IconButton::Count(int& found, int& total) const
{
    found = 0;
    total = 0;
    // Only what is in the region the map is showing, when it shows one alone
    const auto add = [&](int region, bool objectFound)
    {
        if (!Map::InRegionFilter(region))
            return;
        ++total;
        found += objectFound ? 1 : 0;
    };
    switch (m_Type)
    {
    case Koroks:
        for (int i = 0; i < Data::KoroksCount; ++i) add(Map::m_Koroks[i].m_Region, Map::m_Koroks[i].m_Found);
        break;
    case Shrines:
        for (int i = 0; i < Data::ShrineCount; ++i) add(Map::m_Shrines[i].m_Region, Map::m_Shrines[i].m_Found);
        if (SavefileIO::DlcShrinesAppear())
            for (int i = 0; i < Data::DLCShrineCount; ++i) add(Map::m_DLCShrines[i].m_Region, Map::m_DLCShrines[i].m_Found);
        break;
    case Hinoxes:
        for (int i = 0; i < Data::HinoxesCount; ++i) add(Map::m_Hinoxes[i].m_Region, Map::m_Hinoxes[i].m_Found);
        break;
    case Taluses:
        for (int i = 0; i < Data::TalusesCount; ++i) add(Map::m_Taluses[i].m_Region, Map::m_Taluses[i].m_Found);
        break;
    case Moldugas:
        for (int i = 0; i < Data::MoldugasCount; ++i) add(Map::m_Moldugas[i].m_Region, Map::m_Moldugas[i].m_Found);
        break;
    case ArmorChests:
        CountChests(Map::m_ArmorChests, Data::ArmorChestsCount, found, total);
        break;
    case WeaponChests:
        CountChests(Map::m_WeaponChests, Data::WeaponChestsCount, found, total);
        break;
    case MaterialChests:
        CountChests(Map::m_MaterialChests, Data::MaterialChestsCount, found, total);
        break;
    case Walls:
        for (int i = 0; i < Data::WallsCount; ++i) add(Map::m_Walls[i].m_Region, Map::m_Walls[i].m_Found);
        break;
    case Quests:
        // Taken (or done) out of those of this game: the Expansion Pass ones only with it
        for (size_t i = 0; i < QuestMap::Entries().size(); ++i)
            if (!QuestMap::Entries()[i].dlc || SavefileIO::HasExpansionPass())
                add(Map::m_Quests[i].m_Region, Map::m_Quests[i].m_Found);
        break;
    default:
        break;
    }
}

void IconButton::DrawShapes()
{
    const float left = m_Position.x, right = m_Position.x + m_Width;
    const float top = m_Position.y, bottom = m_Position.y - m_Height;
    ShapeBatch& shapes = Ui::Shapes();

    if (m_IsSelected)
    {
        // An outline in the accent colour around an opaque row
        shapes.AddRoundedRect(left - 2.0f, bottom - 2.0f, right + 2.0f, top + 2.0f, RowRadius + 2.0f,
                              glm::vec4(Ui::Colors::Accent, 1.0f));
        shapes.AddRoundedRect(left, bottom, right, top, RowRadius,
                              m_IsToggled ? glm::vec4(0.17f, 0.36f, 0.48f, 1.0f) : glm::vec4(0.10f, 0.13f, 0.15f, 1.0f));
    }
    else
        shapes.AddRoundedRect(left, bottom, right, top, RowRadius, m_IsToggled ? Ui::Colors::RowOn : Ui::Colors::RowOff);

    if (m_Type == HeroPath)
        return; // a line, not things to find: no bar
    int found = 0, total = 0;
    Count(found, total);
    // 8 px from the bottom in rows of 44, less in the shorter rows of a big zoom.
    Ui::ProgressBar(left + 64.0f, bottom + std::min(8.0f, (m_Height - 24.0f) * 0.5f), m_Width - 64.0f - 14.0f, 4.0f,
                    total > 0 ? static_cast<float>(found) / static_cast<float>(total) : 0.0f,
                    m_IsToggled ? Ui::Colors::Accent : Ui::Colors::TextDim);
}

void IconButton::DrawIcon()
{
    // A category that is off looks faded
    m_Icon.m_Tint = glm::vec4(1.0f, 1.0f, 1.0f, m_IsToggled ? 1.0f : 0.4f);
    m_Icon.Render();
}

void IconButton::DrawText()
{
    int found = 0, total = 0;
    Count(found, total);

    const glm::vec3& color = m_IsToggled ? Ui::Colors::Text : Ui::Colors::TextDim;
    const float baseline = m_Position.y - std::max(19.0f, m_Height - 20.0f);
    const std::string& label = Localization::Get(LabelOf(m_Type));
    Ui::Text(label, glm::vec2(m_Position.x + 64.0f, baseline), 0.40f, color);
    std::string value = std::to_string(found) + "/" + std::to_string(total);
    if (m_Type == HeroPath)
    {
        // How far Link has walked, as the path tells (in the region shown or not: the path is one).
        const double km = SavefileIO::HeroPathMeters() / 1000.0;
        char text[32];
        std::snprintf(text, sizeof(text), km >= 100.0 ? "%.0f km" : "%.1f km", km);
        value = SavefileIO::GetHeroPath().segments.empty() ? std::string("-") : std::string(text);
    }
    Ui::Text(value, glm::vec2(m_Position.x + m_Width - 14.0f, baseline), 0.40f, color, ALIGN_RIGHT);
}

bool IconButton::Click(Legend* legend)
{
    return Click(legend, !m_IsToggled);
}

bool IconButton::Click(Legend* legend, bool toggled)
{
    m_IsToggled = toggled;
    legend->m_Show[m_Type] = m_IsToggled;
    return m_IsToggled;
}
