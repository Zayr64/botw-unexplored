#include "Legend.h"
#include "MapCompletion.h"

#include "Graphics/ShapeBatch.h"
#include "Graphics/Texture2D.h"
#include "Map.h"
#include "MapObject.hpp"
#include "MapLocation.h"
#include "SavefileIO.h"
#include "Localization.h"
#include "Ui.h"

namespace
{
    constexpr float RowsTop = 152.0f;  // from the top of the screen to the first row
    constexpr float RowHeight = 56.0f; // eight rows end just above the bottom bar
    constexpr float RowGap = 8.0f;
    constexpr float RowRadius = 8.0f;

    Localization::Text LabelOf(IconButton::ButtonTypes type)
    {
        switch (type)
        {
        case IconButton::Chests: return Localization::Text::Chests;
        case IconButton::SpecialChests: return Localization::Text::SpecialChests;
        default:
            // Koroks to Locations, in the order of the categories
            return static_cast<Localization::Text>(static_cast<int>(Localization::Text::Koroks) + static_cast<int>(type));
        }
    }

    // Opened and existing chests of one of the two kinds (those of the other mode or of the DLC do not count).
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
    const float top = Map::m_ScreenTop;

    for (int i = 0; i < IconButton::ButtonTypes::Count; i++)
    {
        const float rowTop = top - RowsTop - (RowHeight + RowGap) * i;
        m_Buttons.push_back(new IconButton((IconButton::ButtonTypes)i, glm::vec2(left, rowTop), Ui::PanelInner(), RowHeight));
        m_Show[i] = false;
    }

    // Show koroks and shrines by default
    m_Buttons[0]->Click(this, true);
    m_Buttons[1]->Click(this, true);

    // Set first highlighted button
    UpdateSelectedButton();
}

void Legend::Update()
{
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
                glm::vec2 touchPosition = glm::vec2(state.touches[0].x - Map::m_CameraWidth / 2, -(state.touches[0].y - Map::m_CameraHeight / 2));

                for (unsigned int i = 0; i < m_Buttons.size(); i++)
                {
                    IconButton* button = m_Buttons[i];

                    float left = button->m_Position.x;
                    float right = button->m_Position.x + button->m_Width;
                    float top = button->m_Position.y;
                    float bottom = button->m_Position.y - button->m_Height;

                    if (touchPosition.x > left && touchPosition.x < right)
                    {
                        if (touchPosition.y > bottom && touchPosition.y < top)
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

    // Up and down go around
    const int count = static_cast<int>(m_Buttons.size());
    u64 buttonsPressed = padGetButtonsDown(Map::m_Pad);
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
    const float top = Map::m_ScreenTop;

    Ui::PanelBackground();

    Ui::Text(Localization::Get(Localization::Text::Legend), glm::vec2(left, top - 52.0f), 0.60f, Ui::Colors::Gold);

    // What the game calls map completion: Koroks, shrines and locations.
    const int found = MapEntriesFound();
    Ui::Text(Localization::Get(Localization::Text::MapCompletionLong), glm::vec2(left, top - 88.0f), 0.36f, Ui::Colors::TextDim);
    Ui::Text(MapCompletion::Format(MapCompletion::Hundredths(found), '.'), glm::vec2(right, top - 88.0f), 0.40f,
             Ui::Colors::Text, ALIGN_RIGHT);
    Ui::ProgressBar(left, top - 106.0f, Ui::PanelInner(), 6.0f,
                    static_cast<float>(found) / static_cast<float>(MapCompletion::TotalEntries), Ui::Colors::Gold);

    glm::vec3 dotColor;
    bool hasDot = false;
    Ui::Text(Map::SourceText(dotColor, hasDot), glm::vec2(left, top - 132.0f), 0.32f, Ui::Colors::TextDim);

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
    float left = -Map::m_CameraWidth / 2;
    if (position.x > left && position.x < left + m_Width) {
        if (position.y > -Map::m_CameraHeight / 2 && position.y < Map::m_CameraHeight / 2) {
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
    case Locations: iconPath = "romfs:/village.png"; break;
    case Chests: iconPath = "romfs:/chest.png"; break;
    case SpecialChests: iconPath = "romfs:/chestspecial.png"; break;
    default: break;
    }

    m_Icon.Create(iconPath);
    // Every icon about 30 pixels tall, whatever the size of its image.
    if (m_Icon.m_Texture && m_Icon.m_Texture->m_Height > 0)
        m_Icon.m_Scale = (type == Locations ? 25.0f : 30.0f) / static_cast<float>(m_Icon.m_Texture->m_Height);
    m_Icon.m_Position = glm::vec2(position.x + 34.0f, position.y - height / 2.0f);
    m_Icon.m_ProjectionMatrix = &Map::m_ProjectionMatrix;
}

void IconButton::Count(int& found, int& total) const
{
    found = 0;
    total = 0;
    switch (m_Type)
    {
    case Koroks:
        total = Data::KoroksCount;
        for (int i = 0; i < Data::KoroksCount; ++i) found += Map::m_Koroks[i].m_Found;
        break;
    case Shrines:
        total = Data::ShrineCount;
        for (int i = 0; i < Data::ShrineCount; ++i) found += Map::m_Shrines[i].m_Found;
        if (SavefileIO::HasDLC)
        {
            total += Data::DLCShrineCount;
            for (int i = 0; i < Data::DLCShrineCount; ++i) found += Map::m_DLCShrines[i].m_Found;
        }
        break;
    case Hinoxes:
        total = Data::HinoxesCount;
        for (int i = 0; i < Data::HinoxesCount; ++i) found += Map::m_Hinoxes[i].m_Found;
        break;
    case Taluses:
        total = Data::TalusesCount;
        for (int i = 0; i < Data::TalusesCount; ++i) found += Map::m_Taluses[i].m_Found;
        break;
    case Moldugas:
        total = Data::MoldugasCount;
        for (int i = 0; i < Data::MoldugasCount; ++i) found += Map::m_Moldugas[i].m_Found;
        break;
    case Locations:
        total = Data::LocationsCount;
        for (int i = 0; i < Data::LocationsCount; ++i) found += Map::m_Locations[i].m_Found;
        break;
    case Chests:
        CountChests(Map::m_Chests, Data::ChestsCount, found, total);
        break;
    case SpecialChests:
        CountChests(Map::m_SpecialChests, Data::SpecialChestsCount, found, total);
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

    int found = 0, total = 0;
    Count(found, total);
    Ui::ProgressBar(left + 64.0f, bottom + 9.0f, m_Width - 64.0f - 14.0f, 4.0f,
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
    const float baseline = m_Position.y - 30.0f;
    const std::string& label = Localization::Get(LabelOf(m_Type));
    Ui::Text(label, glm::vec2(m_Position.x + 64.0f, baseline), 0.40f, color);
    Ui::Text(std::to_string(found) + "/" + std::to_string(total), glm::vec2(m_Position.x + m_Width - 14.0f, baseline),
             0.40f, color, ALIGN_RIGHT);
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
