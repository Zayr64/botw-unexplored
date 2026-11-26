#include "ObjectInfo.h"

#include "Graphics/ShapeBatch.h"
#include "Localization.h"
#include "Legend.h"
#include "Map.h"
#include "ObjectModel.h"
#include "SavefileIO.h"
#include "ShrineFlags.h"
#include "Ui.h"

ObjectInfo::ObjectInfo()
{
}

void ObjectInfo::SetObject(Data::ObjectType type, uint32_t completionHash, const glm::vec2& mapPosition,
                           const std::string& name, bool* found, const Data::Chest* chest, const QuestMap::Entry* quest)
{
    m_Chest = chest;
    m_Quest = quest;
    m_Type = type;
    m_CompletionHash = completionHash;
    m_MapPosition = mapPosition;
    m_Name = name;
    m_Found = found;

    delete m_Icon;
    m_Icon = nullptr;
    // A chest shows the icon of its group; a quest in progress, the icon of those
    const bool questActive = quest != nullptr &&
        QuestMap::CurrentStep(*quest, [](uint32_t hash) { return SavefileIO::IsProgressFlagSet(hash); }) >= 0;
    const char* iconPath = chest != nullptr ? Data::IconOf(Data::GroupOf(chest->contents))
                           : questActive    ? "romfs:/questactive.png"
                                            : ObjectModel::GetMetadata(type).iconPath;
    if (iconPath[0] != '\0')
    {
        m_Icon = new TexturedQuad();
        m_Icon->Create(iconPath);
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
    // The title of a chest is its group, as in the legend; that of a quest, its kind
    const std::string questTitle = m_Quest == nullptr ? std::string()
        : Localization::Get(m_Quest->main ? Text::MainQuestTitle
                            : m_Quest->shrine ? Text::ShrineQuestTitle : Text::SideQuestTitle) +
              (m_Quest->dlc ? " (DLC)" : "");
    const auto isSet = [](uint32_t hash) { return SavefileIO::IsProgressFlagSet(hash); };
    const int questStep = m_Quest != nullptr ? QuestMap::CurrentStep(*m_Quest, isSet) : -1;
    const std::string& title = m_Quest != nullptr ? questTitle
        : m_Chest != nullptr
        ? Localization::Get(static_cast<Text>(static_cast<int>(Text::ArmorChests) +
                                              static_cast<int>(Data::GroupOf(m_Chest->contents))))
        : Localization::GetObjectTypeName(m_Type);
    float top = Ui::CardHeader(m_Icon, 40.0f, title, Map::NearestRankText());

    // A chest's name is what it holds ("Bomb Arrow x10"); until zayr18 it was hidden behind two presses of A.
    const std::string name = m_Chest != nullptr && m_Chest->item < Data::ChestItemNamesCount
        ? std::string(Data::ChestItemNames[m_Chest->item]) : m_Name;
    if (!name.empty())
        top = Ui::Paragraph(top, name, 0.50f, Ui::Colors::Gold) - 6.0f;

    // What was done and what is left, one line each
    if (m_Quest != nullptr && questStep >= 0)
    {
        // In progress: the quest log's text for the step it is at, and whether the map shows the game's own marker
        // for it or, with none, where the quest was taken
        top = Ui::StatusRow(top, Localization::Get(Text::QuestInProgress), Ui::Colors::Warn, false);
        const std::string& stepText = m_Quest->steps[questStep].text;
        if (!stepText.empty())
            top = Ui::Paragraph(top - 2.0f, stepText, 0.40f, Ui::Colors::Text);
        const bool marked = !QuestMap::ActiveMarkers(*m_Quest, questStep, isSet).empty();
        top = Ui::Paragraph(top - 2.0f, Localization::Get(marked ? Text::QuestMarkerHere : Text::QuestNoMarker), 0.36f,
                            Ui::Colors::TextDim);
        if (!m_Quest->location.empty())
            top = Ui::Paragraph(top - 2.0f, m_Quest->location, 0.40f, Ui::Colors::Text);
    }
    else if (m_Quest != nullptr)
    {
        // Not taken yet (the map only shows those); whether it can be taken now or waits for something first
        const bool available = m_Quest->requiresHash == 0 || SavefileIO::IsProgressFlagSet(m_Quest->requiresHash);
        top = available ? Ui::StatusRow(top, Localization::Get(Text::QuestNotTaken), Ui::Colors::Warn, false)
                        : Ui::StatusRow(top, Localization::Get(Text::QuestNotYet), Ui::Colors::TextDim, false);
        if (!m_Quest->location.empty())
            top = Ui::Paragraph(top - 2.0f, m_Quest->location, 0.40f, Ui::Colors::Text);
    }
    else if (found && Map::IsManuallyMarked(m_Type, m_CompletionHash))
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
    else if (m_Chest != nullptr)
    {
        // Not opened, but its item is had already (an armor-group chest): nothing left in it
        const bool opened = SavefileIO::IsChestOpened(m_Chest->hash);
        const bool had = !opened && Data::GroupOf(m_Chest->contents) == Data::ChestGroup::Armor &&
                         SavefileIO::HasChestItem(m_Chest->item);
        top = had      ? Ui::StatusRow(top, Localization::Get(Text::ChestItemHad), Ui::Colors::Good)
            : found    ? Ui::StatusRow(top, Localization::Get(Text::Opened), Ui::Colors::Good)
                       : Ui::StatusRow(top, Localization::Get(Text::NotOpened), Ui::Colors::Warn, false);
    }
    else if (m_Type == Data::ObjectType::Tower)
        top = found ? Ui::StatusRow(top, Localization::Get(Text::TowerActivated), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(Text::TowerNotActivated), Ui::Colors::Warn, false);
    else if (m_Type == Data::ObjectType::GreatFairy)
    {
        // The spring of the horse god says so (it is not a fairy)
        static const uint32_t spring = ShrineFlags::Crc32("Location_MaronSpring");
        const bool horseGod = m_CompletionHash == spring;
        top = found ? Ui::StatusRow(top, Localization::Get(horseGod ? Text::HorseGodAwake : Text::FairyAwake), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(horseGod ? Text::HorseGodAsleep : Text::FairyAsleep),
                                    Ui::Colors::Warn, false);
    }
    else if (m_Type == Data::ObjectType::Location)
        top = found ? Ui::StatusRow(top, Localization::Get(Text::Discovered), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(Text::NotDiscovered), Ui::Colors::Warn, false);
    else if (m_Type == Data::ObjectType::BreakableWall)
        top = found ? Ui::StatusRow(top, Localization::Get(Text::Broken), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(Text::NotBroken), Ui::Colors::Warn, false);
    else
        top = found ? Ui::StatusRow(top, Localization::Get(Text::Defeated), Ui::Colors::Good)
                    : Ui::StatusRow(top, Localization::Get(Text::NotDefeated), Ui::Colors::Warn, false);

    const std::string distance = Ui::DistanceText(m_MapPosition);
    if (!distance.empty())
        top = Ui::Paragraph(top - 2.0f, distance, 0.40f, Ui::Colors::TextDim);

    if (m_Quest != nullptr && questStep < 0 && !m_Quest->description.empty())
        top = Ui::Paragraph(top - 4.0f, m_Quest->description, 0.36f, Ui::Colors::TextDim);

    if (m_Chest != nullptr)
    {
        if (m_Chest->flags & Data::ChestEnemyCamp)
            top = Ui::Paragraph(top, Localization::Get(Text::ChestEnemyCamp), 0.36f, Ui::Colors::TextDim);
        else if (m_Chest->flags & Data::ChestSideQuest)
            top = Ui::Paragraph(top, Localization::Get(Text::ChestSideQuest), 0.36f, Ui::Colors::TextDim);
    }

    Ui::Coordinates(m_MapPosition);

    Ui::FlushShapes();
    if (m_Icon && m_Icon->m_Texture)
        m_Icon->Render();
}

bool ObjectInfo::IsPositionOn(const glm::vec2& position) const
{
    return position.x >= Ui::Left() && position.x <= Ui::Left() + Ui::PanelWidth && position.y >= Ui::Bottom() &&
           position.y <= Ui::Top();
}

glm::vec2 ObjectInfo::GetMapPosition() const
{
    return m_MapPosition;
}

ObjectInfo::~ObjectInfo()
{
    delete m_Icon;
}
