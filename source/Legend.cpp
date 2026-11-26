#include "Legend.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "MapCompletion.h"

#include "Graphics/ShapeBatch.h"
#include "Graphics/Texture2D.h"
#include "Map.h"
#include "MapObject.hpp"
#include "SavefileIO.h"
#include "Localization.h"
#include "QuestMap.h"
#include "SwitchStyle.h"
#include "Ui.h"

namespace
{
    // The panel's header (from the top of the screen) and the ten rows of the legend: as up to zayr21 when they fit
    // above the bottom bar, else a tighter header and shorter rows (a bigger zoom leaves less height).
    struct PanelLayout
    {
        float tabs, completion, bar, source, rowsTop, rowHeight, rowGap;
    };

    // zayr28: the categories in two columns of seven in the panel's width, in this order. The first column what the
    // map completion asks for (Korok seeds, shrines, locations) and the world's places, the Hero's Path last; the
    // second the loot (the three chest groups, the walls that hide more) and then the mini-bosses, looked up less
    // (zayr29, chosen by the user over the mini-bosses first and over the towers first). Up and down go through them
    // in this order.
    constexpr int Columns = 2;
    constexpr int RowsPerColumn = 7;
    const IconButton::ButtonTypes DisplayOrder[] = {
        IconButton::Koroks, IconButton::Shrines, IconButton::Locations, IconButton::Towers, IconButton::GreatFairies,
        IconButton::Quests, IconButton::HeroPath,
        IconButton::ArmorChests, IconButton::WeaponChests, IconButton::MaterialChests, IconButton::Walls,
        IconButton::Hinoxes, IconButton::Taluses, IconButton::Moldugas
    };
    static_assert(sizeof(DisplayOrder) / sizeof(DisplayOrder[0]) == LegendCategories::ButtonCount,
                  "Every legend category has a place in the columns");
    static_assert(Columns * RowsPerColumn == LegendCategories::ButtonCount, "Two full columns");
    // Each a cell: icon and count on top, the name under them (chosen by the user over a panel twice as wide with
    // rows as before, 26/11)
    constexpr float ColumnGap = 12.0f;

    int DisplayIndexOf(int type)
    {
        for (int i = 0; i < static_cast<int>(LegendCategories::ButtonCount); ++i)
            if (static_cast<int>(DisplayOrder[i]) == type)
                return i;
        return 0;
    }

    PanelLayout Layout()
    {
        const float height = Ui::Top() - (Ui::Bottom() + Ui::FooterHeight + 8.0f);
        PanelLayout layout = {52.0f, 88.0f, 106.0f, 132.0f, 152.0f, 58.0f, 6.0f};
        if (layout.rowsTop + RowsPerColumn * (layout.rowHeight + layout.rowGap) <= height)
            return layout;
        layout = {46.0f, 78.0f, 94.0f, 116.0f, 132.0f, 0.0f, 5.0f};
        layout.rowHeight = std::max(30.0f, std::floor((height - layout.rowsTop) / RowsPerColumn) - layout.rowGap);
        return layout;
    }

    // zayr33: the panel is the legend alone (the tabs Progress, Regions and Collection went out at the user's word); its
    // title stays where the tabs were, marked as the tab in use of BotW Compendium.
    constexpr float TitleScale = 0.38f;

    constexpr float RowRadius = 8.0f;

    // zayr30: the cells as the Switch's own menus draw them. Neutral tiles, darker when the category is off; the one
    // selected in a pulsing cyan frame, a gap away, as on the HOME menu; whether the category shows, by a toggle as in
    // System Settings (chosen by the user over a check as when picking in the Album, and over the blue cells of zayr28).
    // zayr31: those pieces are in SwitchStyle, which BotW Compendium uses too.

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
        case IconButton::Towers: return Localization::Text::Towers;
        case IconButton::GreatFairies: return Localization::Text::GreatFairies;
        default:
            // Koroks to Locations, in the order of the categories (as in Localization::Text)
            return static_cast<Localization::Text>(static_cast<int>(Localization::Text::Koroks) + static_cast<int>(type));
        }
    }

    // Opened and existing chests of one of the groups (those of the other mode or of the DLC do not count).
    template <typename T>
    void CountChests(const MapObject<T>* chests, int count, int& found, int& total)
    {
        for (int i = 0; i < count; ++i)
        {
            if (chests[i].m_Excluded)
                continue;
            ++total;
            found += chests[i].m_Found;
        }
    }

    int MapEntriesFound()
    {
        // Visited or not, whatever the legend shows: the number is the game's.
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
    const float columnWidth = (Ui::PanelInner() - ColumnGap * (Columns - 1)) / Columns;

    m_Buttons.resize(IconButton::ButtonTypes::Count, nullptr);
    for (int place = 0; place < IconButton::ButtonTypes::Count; place++)
    {
        const int column = place / RowsPerColumn, row = place % RowsPerColumn;
        const float rowTop = top - layout.rowsTop - (layout.rowHeight + layout.rowGap) * row;
        const IconButton::ButtonTypes type = DisplayOrder[place];
        IconButton* button = new IconButton(type, glm::vec2(left + (columnWidth + ColumnGap) * column, rowTop), columnWidth,
                                            layout.rowHeight, true);
        m_Buttons[type] = button;
        m_Show[type] = false;
    }

    // Show koroks, shrines and the Hero's Path by default (as SettingsIO::Settings does)
    m_Buttons[0]->Click(this, true);
    m_Buttons[1]->Click(this, true);
    m_Buttons[IconButton::HeroPath]->Click(this, true);
    m_Buttons[IconButton::Quests]->Click(this, true);
    m_Buttons[IconButton::Locations]->Click(this, true);
    m_Buttons[IconButton::Towers]->Click(this, true);
    m_Buttons[IconButton::GreatFairies]->Click(this, true);

    // Set first highlighted button
    UpdateSelectedButton();
}

void Legend::Update()
{
    const u64 buttonsPressed = padGetButtonsDown(Map::m_Pad);

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

                        m_HighlightedButton = DisplayIndexOf(static_cast<int>(i));
                        UpdateSelectedButton();

                        break;
                    }
                }
            }
        }
    }

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
    // Left and right go to the same row of the other column
    if ((buttonsPressed & HidNpadButton_Right) && m_HighlightedButton < RowsPerColumn)
    {
        m_HighlightedButton += RowsPerColumn;
        UpdateSelectedButton();
    }
    if ((buttonsPressed & HidNpadButton_Left) && m_HighlightedButton >= RowsPerColumn)
    {
        m_HighlightedButton -= RowsPerColumn;
        UpdateSelectedButton();
    }
    if (buttonsPressed & HidNpadButton_A)
    {
        // Bounds check
        if (m_HighlightedButton >= 0 && m_HighlightedButton < count)
            m_Buttons[DisplayOrder[m_HighlightedButton]]->Click(this);
    }
}

void Legend::UpdateSelectedButton()
{
    for (unsigned int i = 0; i < m_Buttons.size(); i++)
        m_Buttons[i]->m_IsSelected = false;

    m_Buttons[DisplayOrder[m_HighlightedButton]]->m_IsSelected = true;
}


void Legend::Render()
{
    const float left = Ui::PanelLeft();
    const float inner = Ui::PanelInner();
    const float right = left + inner;
    const float top = Ui::Top();
    const PanelLayout layout = Layout();
    Ui::PanelBackground();

    // The title, marked as the tab in use of BotW Compendium (SwitchStyle)
    const std::string& title = Localization::Get(Localization::Text::Legend);
    SwitchStyle::TabMark(Ui::Shapes(), left, left + Ui::TextWidth(title, TitleScale), top - layout.tabs);
    Ui::Text(title, glm::vec2(left, top - layout.tabs), TitleScale, SwitchStyle::TabTitleColor(true));

    // What the game calls map completion: Koroks, shrines and locations.
    const int found = MapEntriesFound();
    Ui::Text(Localization::Get(Localization::Text::MapCompletionLong), glm::vec2(left, top - layout.completion), 0.36f,
             Ui::Colors::TextDim);
    Ui::Text(MapCompletion::Format(MapCompletion::Hundredths(found), '.'), glm::vec2(right, top - layout.completion), 0.40f,
             Ui::Colors::Text, ALIGN_RIGHT);
    Ui::ProgressBar(left, top - layout.bar, inner, 6.0f,
                    static_cast<float>(found) / static_cast<float>(MapCompletion::TotalEntries), Ui::Colors::Gold);

    // Where the progress comes from
    glm::vec3 dotColor;
    bool hasDot = false;
    Ui::Text(Map::SourceText(dotColor, hasDot), glm::vec2(left, top - layout.source), 0.32f, Ui::Colors::TextDim);

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

IconButton::IconButton(ButtonTypes type, glm::vec2 position, float width, float height, bool compact)
{
    m_Compact = compact;
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
    case Locations: iconPath = "romfs:/location.png"; break;
    case ArmorChests: iconPath = Data::IconOf(Data::ChestGroup::Armor); break;
    case WeaponChests: iconPath = Data::IconOf(Data::ChestGroup::Weapon); break;
    case MaterialChests: iconPath = Data::IconOf(Data::ChestGroup::Material); break;
    case Walls: iconPath = "romfs:/wall.png"; break;
    case HeroPath: iconPath = "romfs:/heropath.png"; break;
    case Quests: iconPath = "romfs:/quest.png"; break;
    case Towers: iconPath = "romfs:/tower.png"; break;
    case GreatFairies: iconPath = "romfs:/fairy.png"; break;
    default: break;
    }

    m_Icon.Create(iconPath);
    // Every icon about 28 pixels tall, whatever the size of its image.
    if (m_Icon.m_Texture && m_Icon.m_Texture->m_Height > 0)
        m_Icon.m_Scale = 28.0f / static_cast<float>(m_Icon.m_Texture->m_Height);
    m_Icon.m_Position = glm::vec2(position.x + 34.0f, position.y - height / 2.0f);
    if (compact)
    {
        // Icon and count on the first line, the name under them
        if (m_Icon.m_Texture && m_Icon.m_Texture->m_Height > 0)
            m_Icon.m_Scale = 24.0f / static_cast<float>(m_Icon.m_Texture->m_Height);
        m_Icon.m_Position = glm::vec2(position.x + 22.0f, position.y - 19.0f);
    }
    m_Icon.m_ProjectionMatrix = Ui::Projection();
}

void IconButton::Count(int& found, int& total) const
{
    found = 0;
    total = 0;
    const auto add = [&](bool objectFound)
    {
        ++total;
        found += objectFound ? 1 : 0;
    };
    switch (m_Type)
    {
    case Koroks:
        for (int i = 0; i < Data::KoroksCount; ++i) add(Map::m_Koroks[i].m_Found);
        break;
    case Shrines:
        for (int i = 0; i < Data::ShrineCount; ++i) add(Map::m_Shrines[i].m_Found);
        if (SavefileIO::DlcShrinesAppear())
            for (int i = 0; i < Data::DLCShrineCount; ++i) add(Map::m_DLCShrines[i].m_Found);
        break;
    case Hinoxes:
        for (int i = 0; i < Data::HinoxesCount; ++i) add(Map::m_Hinoxes[i].m_Found);
        break;
    case Taluses:
        for (int i = 0; i < Data::TalusesCount; ++i) add(Map::m_Taluses[i].m_Found);
        break;
    case Moldugas:
        for (int i = 0; i < Data::MoldugasCount; ++i) add(Map::m_Moldugas[i].m_Found);
        break;
    case Locations:
        for (int i = 0; i < Data::LocationsCount; ++i) add(Map::m_Locations[i].m_Found);
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
        for (int i = 0; i < Data::WallsCount; ++i) add(Map::m_Walls[i].m_Found);
        break;
    case Quests:
        // Done out of the side and shrine quests of this game (the Expansion Pass ones only with it); those in
        // progress are on the map until done (zayr27; until zayr26 taken counted as found)
        for (size_t i = 0; i < QuestMap::Entries().size(); ++i)
        {
            const QuestMap::Entry& quest = QuestMap::Entries()[i];
            if (!quest.main && (!quest.dlc || SavefileIO::HasExpansionPass()))
                add(QuestMap::IsDone(quest, [](uint32_t hash) { return SavefileIO::IsProgressFlagSet(hash); }));
        }
        break;
    case Towers:
        for (int i = 0; i < Map::TowerCount; ++i) add(Map::m_Towers[i].m_Found);
        break;
    case GreatFairies:
        for (int i = 0; i < Map::FairyCount; ++i) add(Map::m_Fairies[i].m_Found);
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

    if (m_Compact)
    {
        if (m_IsSelected)
            SwitchStyle::SelectionFrame(shapes, left, bottom, right, top, RowRadius, Ui::Frame(),
                                        glm::vec3(Ui::Colors::Panel));
        shapes.AddRoundedRect(left, bottom, right, top, RowRadius, m_IsToggled ? SwitchStyle::TileOn : SwitchStyle::TileOff);
        SwitchStyle::Toggle(shapes, right - 9.0f, top - 10.0f, m_IsToggled);
        if (m_Type == HeroPath)
            return;
        int switchFound = 0, switchTotal = 0;
        Count(switchFound, switchTotal);
        Ui::ProgressBar(left + 10.0f, bottom + 6.0f, m_Width - 20.0f, 3.0f,
                        switchTotal > 0 ? static_cast<float>(switchFound) / static_cast<float>(switchTotal) : 0.0f,
                        m_IsToggled ? SwitchStyle::Cyan : Ui::Colors::TextDim);
        return;
    }

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
    if (m_Compact)
        Ui::Text(label, glm::vec2(m_Position.x + 10.0f, m_Position.y - 44.0f), 0.30f, color);
    else
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
    if (m_Compact)
        Ui::Text(value, glm::vec2(m_Position.x + 40.0f, m_Position.y - 26.0f), 0.38f, color); // the toggle is on the right
    else
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
