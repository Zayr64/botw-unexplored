#include "ObjectInfo.h"

#include "Graphics/ShapeBatch.h"
#include "Localization.h"
#include "LocationState.h"
#include "Legend.h"
#include "Map.h"
#include "ObjectModel.h"
#include "SavefileIO.h"
#include "Ui.h"

ObjectInfo::ObjectInfo()
{
}

void ObjectInfo::SetObject(Data::ObjectType type, uint32_t completionHash, const glm::vec2& mapPosition,
                           const std::string& name, bool* found, const Data::Chest* chest)
{
    m_Chest = chest;
    m_Reveal = 0;
    m_Type = type;
    m_CompletionHash = completionHash;
    m_MapPosition = mapPosition;
    m_Name = name;
    m_Found = found;

    delete m_Icon;
    m_Icon = nullptr;
    const ObjectModel::TypeMetadata& metadata = ObjectModel::GetMetadata(type);
    if (metadata.iconPath[0] != '\0')
    {
        m_Icon = new TexturedQuad();
        m_Icon->Create(metadata.iconPath);
    }
}

void ObjectInfo::SetOpen(bool open)
{
    if (open)
        Map::CloseInfoPanels();
    m_IsOpen = open;
    if (open)
        Map::m_Legend->m_IsOpen = false;
}

void ObjectInfo::Render()
{
    if (!m_IsOpen)
        return;

    using Localization::Text;
    const bool found = m_Found && *m_Found;
    const bool isShrine = m_Type == Data::ObjectType::Shrine || m_Type == Data::ObjectType::DLCShrine;

    Ui::PanelBackground();
    float top = Ui::CardHeader(m_Icon, m_Type == Data::ObjectType::Location ? 34.0f : 40.0f,
                               Localization::GetObjectTypeName(m_Type), Map::NearestRankText());

    if (!m_Name.empty())
        top = Ui::Paragraph(top, m_Name, 0.50f, Ui::Colors::Gold) - 6.0f;

    // What was done and what is left, one line each
    if (found && Map::IsManuallyMarked(m_Type, m_CompletionHash))
        top = Ui::StatusRow(top, Localization::Get(Text::MarkedByYou), Ui::Colors::Good);
    else if (isShrine)
    {
        top = found ? Ui::StatusRow(top, Localization::Get(Text::Discovered), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(Text::NotDiscovered), Ui::Colors::Warn, false);
        if (found)
        {
            top = SavefileIO::IsShrineCleared(m_CompletionHash)
                ? Ui::StatusRow(top, Localization::Get(Text::ShrineCompleted), Ui::Colors::Good)
                : Ui::StatusRow(top, Localization::Get(Text::NotCompleted), Ui::Colors::Warn, false);
            // How many chests the shrine has, from the flag of each one; the game's own flag tells when none is left.
            int opened = 0, total = 0;
            const bool counted = SavefileIO::GetShrineChests(m_CompletionHash, opened, total) && total > 0;
            const bool noneLeft = SavefileIO::HasAllShrineChests(m_CompletionHash) || (counted && opened == total);
            if (counted)
                top = Ui::StatusRow(top, Localization::Get(Text::Chests) + " " + std::to_string(noneLeft ? total : opened) +
                                             "/" + std::to_string(total),
                                    noneLeft ? Ui::Colors::Good : Ui::Colors::Warn, noneLeft);
            else
                top = noneLeft ? Ui::StatusRow(top, Localization::Get(Text::AllChestsOpened), Ui::Colors::Good)
                               : Ui::StatusRow(top, Localization::Get(Text::ChestsLeft), Ui::Colors::Warn, false);
        }
    }
    else if (m_Type == Data::ObjectType::Location)
    {
        top = found ? Ui::StatusRow(top, Localization::Get(Text::Visited), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(Text::NotVisited), Ui::Colors::Warn, false);
        // A tower is also activated and a great fairy awakened; told only once the place is known.
        const LocationState::Entry* state = LocationState::ForLocation(m_CompletionHash);
        if (found && state != nullptr)
        {
            const bool tower = state->kind == LocationState::Kind::Tower;
            top = SavefileIO::IsLocationStateDone(m_CompletionHash)
                ? Ui::StatusRow(top, Localization::Get(tower ? Text::Activated : Text::Awakened), Ui::Colors::Good)
                : Ui::StatusRow(top, Localization::Get(tower ? Text::NotActivated : Text::NotAwakened), Ui::Colors::Warn, false);
        }
    }
    else if (m_Chest != nullptr)
        top = found ? Ui::StatusRow(top, Localization::Get(Text::Opened), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(Text::NotOpened), Ui::Colors::Warn, false);
    else if (m_Type == Data::ObjectType::BreakableWall)
        top = found ? Ui::StatusRow(top, Localization::Get(Text::Broken), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(Text::NotBroken), Ui::Colors::Warn, false);
    else
        top = found ? Ui::StatusRow(top, Localization::Get(Text::Defeated), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(Text::NotDefeated), Ui::Colors::Warn, false);

    const std::string distance = Ui::DistanceText(m_MapPosition);
    if (!distance.empty())
        top = Ui::Paragraph(top - 2.0f, distance, 0.40f, Ui::Colors::TextDim);

    if (m_Chest != nullptr)
    {
        if (m_Chest->flags & Data::ChestEnemyCamp)
            top = Ui::Paragraph(top, Localization::Get(Text::ChestEnemyCamp), 0.36f, Ui::Colors::TextDim);
        else if (m_Chest->flags & Data::ChestSideQuest)
            top = Ui::Paragraph(top, Localization::Get(Text::ChestSideQuest), 0.36f, Ui::Colors::TextDim);

        // What is inside, under a line of its own: nothing until asked, then the kind of thing, then the item.
        top -= 8.0f;
        Ui::Shapes().AddRect(Ui::PanelLeft(), top - 1.0f, Ui::PanelLeft() + Ui::PanelInner(), top, Ui::Colors::Hairline);
        top = Ui::Paragraph(top - 16.0f, Localization::Get(Text::Contents), 0.30f, Ui::Colors::TextDim) - 3.0f;
        const bool validKind = m_Chest->contents < static_cast<uint8_t>(Data::ChestContents::Count);
        const bool validItem = m_Chest->item < Data::ChestItemNamesCount;
        if (m_Reveal >= 2 && validItem)
            top = Ui::Paragraph(top, Data::ChestItemNames[m_Chest->item], 0.44f, Ui::Colors::Gold);
        else if (m_Reveal >= 1 && validKind)
            top = Ui::Paragraph(top, Localization::Get(static_cast<Text>(static_cast<int>(Text::ContentsWeapon) + m_Chest->contents)),
                                0.44f, Ui::Colors::Text);
        else
            top = Ui::Paragraph(top, Localization::Get(Text::ContentsHidden), 0.44f, Ui::Colors::TextDim);
    }

    Ui::Coordinates(m_MapPosition);

    Ui::FlushShapes();
    if (m_Icon && m_Icon->m_Texture)
        m_Icon->Render();
}

bool ObjectInfo::IsPositionOn(const glm::vec2& position) const
{
    return position.x >= Map::m_ScreenLeft && position.x <= Map::m_ScreenLeft + Ui::PanelWidth &&
           position.y >= Map::m_ScreenBottom && position.y <= Map::m_ScreenTop;
}

glm::vec2 ObjectInfo::GetMapPosition() const
{
    return m_MapPosition;
}

ObjectInfo::~ObjectInfo()
{
    delete m_Icon;
}
