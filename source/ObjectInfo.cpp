#include "ObjectInfo.h"

#include "Localization.h"
#include "Legend.h"
#include "Map.h"
#include "ObjectModel.h"
#include "SavefileIO.h"
#include "Ui.h"

ObjectInfo::ObjectInfo()
{
}

void ObjectInfo::SetObject(Data::ObjectType type, uint32_t completionHash, const glm::vec2& mapPosition,
                           const std::string& name, bool* found)
{
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
            top = SavefileIO::HasAllShrineChests(m_CompletionHash)
                ? Ui::StatusRow(top, Localization::Get(Text::AllChestsOpened), Ui::Colors::Good)
                : Ui::StatusRow(top, Localization::Get(Text::ChestsLeft), Ui::Colors::Warn, false);
        }
    }
    else if (m_Type == Data::ObjectType::Location)
        top = found ? Ui::StatusRow(top, Localization::Get(Text::Visited), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(Text::NotVisited), Ui::Colors::Warn, false);
    else
        top = found ? Ui::StatusRow(top, Localization::Get(Text::Defeated), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(Text::NotDefeated), Ui::Colors::Warn, false);

    const std::string distance = Ui::DistanceText(m_MapPosition);
    if (!distance.empty())
        top = Ui::Paragraph(top - 2.0f, distance, 0.40f, Ui::Colors::TextDim);

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
