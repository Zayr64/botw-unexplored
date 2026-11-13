#include "Map.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>
#include <switch.h>

#include "Graphics/BasicVertices.h"
#include "Graphics/LineRenderer.h"
#include "Graphics/Quad.h"
#include "Graphics/ShapeBatch.h"
#include "MapLocation.h"
#include "Legend.h"
#include "Dialog.h"
#include "MapObject.hpp"
#include "KorokDialog.h"
#include "ObjectInfo.h"
#include "ImageViewer.h"
#include "MapHd.h"
#include "MapCompletion.h"
#include "ObjectModel.h"
#include "Log.h"
#include "Regions.h"
#include "RegionsPanel.h"
#include "Localization.h"
#include "LocationState.h"
#include "ManualProgress.h"
#include "Navigation.h"
#include "ModeChangeDecision.h"
#include "Ui.h"

#include "SavefileIO.h"

constexpr float MapScale = 0.25f;

namespace
{
    // "You are here": the position of the last save, and ZR cycling through the nearest missing objects.
    TexturedQuad s_PlayerMarker;
    TexturedQuad s_Crosshair; // zayr7: aim fixed at the screen center
    constexpr int NextMissingCount = 5;
    bool s_NextMissingActive = false;
    std::vector<int> s_NextMissingList; // indices into CollectObjects(), nearest first
    glm::vec2 s_NextMissingOrigin(0.0f);
    glm::vec2 s_NextMissingTarget(0.0f);
    int s_NextMissingRank = 0; // 0-based position in s_NextMissingList

    // The map itself: 3000 x 2500 units centred on the origin.
    constexpr float MapHalfWidth = 1500.0f;
    constexpr float MapHalfHeight = 1250.0f;
    constexpr float MinZoom = 0.2f;
    constexpr float MaxZoom = 10.0f;

    int s_DpadHeldFrames = 0;

    bool HasPlayerPosition()
    {
        return SavefileIO::LoadedSavefile && SavefileIO::CurrentState.hasPlayerPosition;
    }

    glm::vec2 PlayerMapPosition()
    {
        return glm::vec2(SavefileIO::CurrentState.playerX, -SavefileIO::CurrentState.playerZ) * MapScale;
    }

    bool ModalDialogOpen()
    {
        return Map::m_NoSavefileDialog->m_IsOpen || Map::m_GameRunningDialog->m_IsOpen || Map::m_MasterModeDialog->m_IsOpen;
    }

    // The legend or a card covers the left of the screen.
    bool LeftPanelOpen()
    {
        return Map::m_Legend->m_IsOpen || Map::m_KorokDialog->m_IsOpen || Map::m_ObjectInfo->m_IsOpen;
    }

    void UpdateViewMatrix()
    {
        Map::m_ViewMatrix = glm::mat4(1.0f); // Reset (important)
        Map::m_ViewMatrix = glm::translate(Map::m_ViewMatrix, glm::vec3(Map::m_ViewShift, 0.0f, 0.0f));
        Map::m_ViewMatrix = glm::scale(Map::m_ViewMatrix, glm::vec3(Map::m_Zoom, Map::m_Zoom, 0.0f));
        Map::m_ViewMatrix = glm::translate(Map::m_ViewMatrix, glm::vec3(-Map::m_CameraPosition, 1.0));
    }

    void ClampCamera()
    {
        Map::m_CameraPosition.x = std::max(-MapHalfWidth, std::min(MapHalfWidth, Map::m_CameraPosition.x));
        Map::m_CameraPosition.y = std::max(-MapHalfHeight, std::min(MapHalfHeight, Map::m_CameraPosition.y));
        Map::m_Zoom = std::max(MinZoom, std::min(MaxZoom, Map::m_Zoom));
    }
}

namespace
{
    ManualProgress::Context CurrentProgressContext()
    {
        ManualProgress::Context context;
        context.profileId1 = SavefileIO::AccountUid1;
        context.profileId2 = SavefileIO::AccountUid2;
        context.masterMode = Map::m_LoadMasterMode;
        return context;
    }

    // A save read with the game closed and the flags of the running game are the truth. A save backup
    // (game running, memory not readable) may be old: only then the marks made by hand count.
    bool ProgressIsCurrent()
    {
        return !SavefileIO::GameIsRunning || SavefileIO::LiveSynced;
    }

    bool CanMarkByHand()
    {
        return !ProgressIsCurrent() && ManualProgress::CanPersist();
    }

    bool ResolveFound(Data::ObjectType type, uint32_t hash, bool foundInSave, bool& manualProgressChanged)
    {
        const ManualProgress::Context context = CurrentProgressContext();
        if (foundInSave)
        {
            if (ProgressIsCurrent())
            {
                manualProgressChanged = ManualProgress::ConfirmFromSave(context, type, hash) || manualProgressChanged;
            }
            return true;
        }
        return !ProgressIsCurrent() && ManualProgress::IsMarkedFound(context, type, hash);
    }

    // Map position of the object whose card is open.
    bool SelectedObjectPosition(glm::vec2& position)
    {
        if (Map::m_KorokDialog != nullptr && Map::m_KorokDialog->m_IsOpen &&
            Map::m_KorokDialog->m_KorokIndex >= 0 && Map::m_KorokDialog->m_KorokIndex < Data::KoroksCount)
        {
            position = Map::m_Koroks[Map::m_KorokDialog->m_KorokIndex].m_Position;
            return true;
        }
        if (Map::m_ObjectInfo != nullptr && Map::m_ObjectInfo->m_IsOpen)
        {
            position = Map::m_ObjectInfo->GetMapPosition();
            return true;
        }
        return false;
    }

    // Every object on the map, in a fixed order, with what its card needs.
    struct MapEntry
    {
        Data::ObjectType type;
        uint32_t hash;
        glm::vec2 position;
        const std::string* name;
        bool* found;
        bool pending; // a discovered shrine with something left still counts as missing
        bool visible; // its category is on in the legend
        int korokIndex;
        const Data::Chest* chest; // for the two chest types
    };

    void CollectObjects(std::vector<MapEntry>& entries)
    {
        static const std::string noName;
        entries.clear();
        entries.reserve(Data::KoroksCount + Data::ShrineCount + Data::DLCShrineCount + Data::HinoxesCount +
                        Data::TalusesCount + Data::MoldugasCount + Data::LocationsCount + Data::ChestsCount +
                        Data::SpecialChestsCount + Data::WallsCount);
        const auto add = [&](Data::ObjectType type, uint32_t hash, const glm::vec2& position, const std::string& name,
                             bool* found, bool pending = false, int korokIndex = -1, const Data::Chest* chest = nullptr)
        {
            bool visible = Map::m_Legend->m_Show[ObjectModel::GetMetadata(type).legendButtonIndex];
            if (type == Data::ObjectType::DLCShrine)
                visible = visible && SavefileIO::DlcShrinesAppear();
            if (visible && Map::m_RegionFilter >= 0)
                visible = Regions::Of(hash) == Map::m_RegionFilter;
            entries.push_back({type, hash, position, &name, found, pending, visible, korokIndex, chest});
        };

        for (int i = 0; i < Data::KoroksCount; ++i)
            add(Data::ObjectType::Korok, Data::Koroks[i].hash, Map::m_Koroks[i].m_Position, noName, &Map::m_Koroks[i].m_Found, false, i);
        for (int i = 0; i < Data::ShrineCount; ++i)
            add(Data::ObjectType::Shrine, Data::Shrines[i].hash, Map::m_Shrines[i].m_Position, Data::Shrines[i].displayName,
                &Map::m_Shrines[i].m_Found, Map::m_Shrines[i].m_Pending);
        for (int i = 0; i < Data::DLCShrineCount; ++i)
            add(Data::ObjectType::DLCShrine, Data::DLCShrines[i].hash, Map::m_DLCShrines[i].m_Position, noName,
                &Map::m_DLCShrines[i].m_Found, Map::m_DLCShrines[i].m_Pending);
        for (int i = 0; i < Data::HinoxesCount; ++i)
            add(Data::ObjectType::Hinox, Data::Hinoxes[i].hash, Map::m_Hinoxes[i].m_Position, noName, &Map::m_Hinoxes[i].m_Found);
        for (int i = 0; i < Data::TalusesCount; ++i)
            add(Data::ObjectType::Talus, Data::Taluses[i].hash, Map::m_Taluses[i].m_Position, noName, &Map::m_Taluses[i].m_Found);
        for (int i = 0; i < Data::MoldugasCount; ++i)
            add(Data::ObjectType::Molduga, Data::Moldugas[i].hash, Map::m_Moldugas[i].m_Position, noName, &Map::m_Moldugas[i].m_Found);
        for (int i = 0; i < Data::LocationsCount; ++i)
            add(Data::ObjectType::Location, Data::Locations[i].hash, Map::m_Locations[i].m_Position, Data::Locations[i].displayName,
                &Map::m_Locations[i].m_Found, Map::m_Locations[i].m_Pending);
        // Only the chests that exist in this game (mode and DLC)
        for (int i = 0; i < Data::ChestsCount; ++i)
            if (!Map::m_Chests[i].m_Excluded)
                add(Data::ObjectType::Chest, Data::Chests[i].hash, Map::m_Chests[i].m_Position, noName,
                    &Map::m_Chests[i].m_Found, false, -1, &Data::Chests[i]);
        for (int i = 0; i < Data::SpecialChestsCount; ++i)
            if (!Map::m_SpecialChests[i].m_Excluded)
                add(Data::ObjectType::SpecialChest, Data::SpecialChests[i].hash, Map::m_SpecialChests[i].m_Position, noName,
                    &Map::m_SpecialChests[i].m_Found, false, -1, &Data::SpecialChests[i]);
        for (int i = 0; i < Data::WallsCount; ++i)
            add(Data::ObjectType::BreakableWall, Data::Walls[i].hash, Map::m_Walls[i].m_Position, noName, &Map::m_Walls[i].m_Found);
    }

    // The object A would open at this map position: the nearest one shown, within 60 pixels. -1 for none.
    int ObjectAt(const std::vector<MapEntry>& entries, const glm::vec2& mapPosition)
    {
        const float radius = 60.0f / Map::m_Zoom;
        const float maxDistanceSquared = radius * radius;
        int best = -1;
        float bestDistanceSquared = 0.0f;
        for (int i = 0; i < static_cast<int>(entries.size()); ++i)
        {
            const MapEntry& entry = entries[i];
            if (!entry.visible || !Map::m_Legend->ShouldShow(*entry.found && !entry.pending))
                continue;
            const float distanceSquared = glm::dot(entry.position - mapPosition, entry.position - mapPosition);
            if (distanceSquared <= maxDistanceSquared && (best < 0 || distanceSquared < bestDistanceSquared))
            {
                best = i;
                bestDistanceSquared = distanceSquared;
            }
        }
        return best;
    }

    // Opens the Korok hint or the object card.
    void OpenObject(const MapEntry& entry)
    {
        Map::CloseInfoPanels();
        if (entry.type == Data::ObjectType::Korok)
        {
            Map::m_KorokDialog->SetSeed(Map::m_Koroks[entry.korokIndex].m_ObjectData->zeldaDungeonId, entry.korokIndex);
            Map::m_KorokDialog->SetOpen(true);
            return;
        }
        Map::m_ObjectInfo->SetObject(entry.type, entry.hash, entry.position, *entry.name, entry.found, entry.chest);
        Map::m_ObjectInfo->SetOpen(true);
    }

    // A ring that breathes around the object whose card is open.
    void RenderSelectedObjectHighlight()
    {
        glm::vec2 position;
        if (!SelectedObjectPosition(position) || !Map::IsInView(position, 30.0f))
            return;

        const glm::vec2 center = Map::MapToScreen(position);
        const float pulse = std::sin(static_cast<float>(Ui::Frame()) * 0.09f);
        const float radius = 23.0f + pulse * 3.0f;
        Ui::Shapes().AddRing(center, radius + 2.0f, radius + 7.0f, glm::vec4(Ui::Colors::Accent, 0.22f));
        Ui::Shapes().AddRing(center, radius - 1.5f, radius + 1.5f, glm::vec4(Ui::Colors::Accent, 1.0f));
        Ui::FlushShapes();
    }

    int CountFound()
    {
        int found = 0;
        for (int i = 0; i < Data::KoroksCount; ++i) found += Map::m_Koroks[i].m_Found;
        for (int i = 0; i < Data::ShrineCount; ++i) found += Map::m_Shrines[i].m_Found;
        for (int i = 0; i < Data::DLCShrineCount; ++i) found += Map::m_DLCShrines[i].m_Found;
        for (int i = 0; i < Data::HinoxesCount; ++i) found += Map::m_Hinoxes[i].m_Found;
        for (int i = 0; i < Data::TalusesCount; ++i) found += Map::m_Taluses[i].m_Found;
        for (int i = 0; i < Data::MoldugasCount; ++i) found += Map::m_Moldugas[i].m_Found;
        for (int i = 0; i < Data::LocationsCount; ++i) found += Map::m_Locations[i].m_Found;
        for (int i = 0; i < Data::ChestsCount; ++i) found += Map::m_Chests[i].m_Found;
        for (int i = 0; i < Data::SpecialChestsCount; ++i) found += Map::m_SpecialChests[i].m_Found;
        for (int i = 0; i < Data::WallsCount; ++i) found += Map::m_Walls[i].m_Found;
        return found;
    }

    int MapEntriesFound()
    {
        int found = 0;
        for (int i = 0; i < Data::KoroksCount; ++i) found += Map::m_Koroks[i].m_Found;
        for (int i = 0; i < Data::ShrineCount; ++i) found += Map::m_Shrines[i].m_Found;
        for (int i = 0; i < Data::LocationsCount; ++i) found += Map::m_Locations[i].m_Found;
        return found;
    }

    // What each button does right now, for the bar at the bottom. `context` goes on its left.
    void BuildHints(std::vector<Ui::Hint>& hints, std::string& context)
    {
        using Localization::Text;
        const auto add = [&](const char* buttons, Text label) { hints.push_back({buttons, Localization::Get(label)}); };

        if (Map::m_ImageViewer->IsOpen())
        {
            add("B", Text::HintBack);
            return;
        }
        if (ModalDialogOpen())
        {
            add("A", Text::HintSelect);
            add("+", Text::HintExit);
            return;
        }
        if (!SavefileIO::LoadedSavefile)
        {
            add("+", Text::HintExit);
            return;
        }

        const bool korokCard = Map::m_KorokDialog->m_IsOpen;
        const bool objectCard = Map::m_ObjectInfo->m_IsOpen;
        if (Map::m_Legend->m_IsOpen)
        {
            if (Map::m_Legend->m_Tab == Legend::TabLegend)
                add("A", Text::HintShowHide);
            else if (Map::m_Legend->m_Tab == Legend::TabRegions)
                add("A", RegionsPanel::HighlightIsWholeMap() ? Text::WholeMap : Text::HintShowRegion);
            add("< >", Text::HintTab);
            add("B", Text::HintClose);
        }
        else if (korokCard || objectCard)
        {
            if (korokCard && Map::m_KorokDialog->HasImage())
                add("A", Text::HintPhoto);
            const bool found = korokCard ? Map::m_Koroks[Map::m_KorokDialog->m_KorokIndex].m_Found
                                         : (Map::m_ObjectInfo->m_Found && *Map::m_ObjectInfo->m_Found);
            if (objectCard && Map::m_ObjectInfo->CanReveal())
                add("A", Map::m_ObjectInfo->RevealLevel() == 0 ? Text::HintReveal : Text::HintShowItem);
            if (CanMarkByHand() && !found)
                add("Y", Text::HintMarkFound);
            add("ZL ZR", Text::HintPreviousNext);
            add("B", Text::HintBack);
            add("X", Text::HintLegend);
        }
        else
        {
            // The object under the aim, which A opens
            static std::vector<MapEntry> entries;
            CollectObjects(entries);
            const int hovered = ObjectAt(entries, Map::m_CameraPosition);
            if (hovered >= 0)
            {
                const MapEntry& entry = entries[hovered];
                context = !entry.name->empty() ? *entry.name
                        : entry.type == Data::ObjectType::Korok ? Localization::Get(Text::KorokSeed)
                                                                : Localization::GetObjectTypeName(entry.type);
                add("A", Text::HintOpen);
            }
            add("ZL ZR", Text::HintNearest);
            add("R", Text::HintAtAim);
            if (HasPlayerPosition())
                add("L", Text::HintLink);
            if (SavefileIO::MasterModeFileExists)
                add("Y", Map::m_LoadMasterMode ? Text::HintNormalMode : Text::HintMasterMode);
            add("X", Text::HintLegend);
        }
        if (SavefileIO::GameIsRunning)
            add("-", Text::HintSync);
        add("+", Text::HintExit);
    }

    // Chips at the top, the toast and the bar at the bottom. Text goes to the font batch.
    void DrawHud()
    {
        if (SavefileIO::LoadedSavefile && !Map::m_ImageViewer->IsOpen())
        {
            glm::vec3 dotColor;
            bool hasDot = false;
            const std::string source = Map::SourceText(dotColor, hasDot);
            Ui::Chip(source, Map::m_ScreenRight - 16.0f, Map::m_ScreenTop - 16.0f, true, hasDot ? &dotColor : nullptr);
            if (!LeftPanelOpen())
            {
                Ui::Chip(Localization::Get(Localization::Text::MapCompletion) + " " +
                         MapCompletion::Format(MapCompletion::Hundredths(MapEntriesFound()), '.'),
                         Map::m_ScreenLeft + 16.0f, Map::m_ScreenTop - 16.0f, false);
                // The region the map is showing alone, with what is found of it
                if (Map::m_RegionFilter >= 0)
                {
                    int found = 0, total = 0;
                    RegionsPanel::Count(Map::m_RegionFilter, found, total);
                    Ui::Chip(RegionsPanel::Label(Map::m_RegionFilter) + " " + std::to_string(found) + "/" + std::to_string(total),
                             Map::m_ScreenLeft + 16.0f, Map::m_ScreenTop - 52.0f, false);
                }
            }
        }

        Ui::DrawToast();

        std::vector<Ui::Hint> hints;
        std::string context;
        BuildHints(hints, context);
        Ui::Footer(hints, context);
    }
}

bool Map::Init()
{
    Log("MAP: initialization begin");
    Data::LoadPaths();

    m_ProjectionMatrix = glm::ortho(-m_CameraWidth / 2, m_CameraWidth / 2, -m_CameraHeight / 2, m_CameraHeight / 2, -1.0f, 1.0f);

    // Map image
    m_MapBackground.Create("romfs:/map-lowres.png");
    m_MapBackground.m_ProjectionMatrix = &m_ProjectionMatrix;
    m_MapBackground.m_ViewMatrix = &m_ViewMatrix;
    MapHd::Init();

    // Load font
    if (m_Font.Load("romfs:/arial.ttf") != 1)
    {
        Log("MAP: font initialization failed");
        return false;
    }
    m_Font.m_ProjectionMatrix = &m_ProjectionMatrix;
    m_Font.m_ViewMatrix = &m_ViewMatrix;

    m_LineRenderer = new LineRenderer();
    Ui::Init();

    m_KorokDialog = new KorokDialog();
    m_ObjectInfo = new ObjectInfo();
    m_ImageViewer = new ImageViewer();

    // Create UI
    m_Legend = new Legend();
    m_NoSavefileDialog = new Dialog(glm::vec2(0.0f, 0.0f), 700.0f, 400.0f, Dialog::InvalidSavefile);
    m_GameRunningDialog = new Dialog(glm::vec2(0.0f, 0.0f), 700.0f, 400.0f, Dialog::GameIsRunning);
    m_MasterModeDialog = new Dialog(glm::vec2(0.0f, 0.0f), 700.0f, 400.0f, Dialog::MasterModeChoose);

    // Below the chip that tells where the progress comes from
    m_MasterModeIcon.Create("romfs:/mastermodeicon.png");
    m_MasterModeIcon.m_Position = glm::vec2(m_ScreenRight - 44.0f, m_ScreenTop - 88.0f);
    m_MasterModeIcon.m_Scale = 0.1f;
    m_MasterModeIcon.m_ProjectionMatrix = &m_ProjectionMatrix;
    m_MasterModeIcon.m_ViewMatrix = nullptr;

    s_PlayerMarker.Create("romfs:/player.png");
    s_PlayerMarker.m_Scale = 0.5f;
    s_PlayerMarker.m_ProjectionMatrix = &m_ProjectionMatrix;
    s_PlayerMarker.m_ViewMatrix = nullptr;

    s_Crosshair.Create("romfs:/crosshair.png");
    s_Crosshair.m_Scale = 0.5f;
    s_Crosshair.m_ProjectionMatrix = &m_ProjectionMatrix;
    s_Crosshair.m_ViewMatrix = nullptr;

    // Create koroks
    m_Koroks = new MapObject<Data::Korok>[Data::KoroksCount];
    MapObject<Data::Korok>::Init("romfs:/korokseed.png", Data::KoroksCount);

    // Create shrines
    m_Shrines = new MapObject<Data::Shrine>[Data::ShrineCount];
    MapObject<Data::Shrine>::Init("romfs:/shrine.png", Data::ShrineCount);
    m_DLCShrines = new MapObject<Data::DLCShrine>[Data::DLCShrineCount];
    MapObject<Data::DLCShrine>::Init("romfs:/dlcshrine.png", Data::DLCShrineCount);

    // Create hinoxes
    m_Hinoxes = new MapObject<Data::Hinox>[Data::HinoxesCount];
    MapObject<Data::Hinox>::Init("romfs:/hinox.png", Data::HinoxesCount);

    // Create taluses
    m_Taluses = new MapObject<Data::Talus>[Data::TalusesCount];
    MapObject<Data::Talus>::Init("romfs:/talus.png", Data::TalusesCount);

    // Create moldugas
    m_Moldugas = new MapObject<Data::Molduga>[Data::MoldugasCount];
    MapObject<Data::Molduga>::Init("romfs:/molduga.png", Data::MoldugasCount);

    // Create chests. Their images are four times the size they are drawn at.
    m_Chests = new MapObject<Data::ChestIcon>[Data::ChestsCount];
    MapObject<Data::ChestIcon>::Init("romfs:/chest.png", Data::ChestsCount);
    MapObject<Data::ChestIcon>::m_SizeFactor = 0.25f;
    m_SpecialChests = new MapObject<Data::SpecialChestIcon>[Data::SpecialChestsCount];
    MapObject<Data::SpecialChestIcon>::Init("romfs:/chestspecial.png", Data::SpecialChestsCount);
    MapObject<Data::SpecialChestIcon>::m_SizeFactor = 0.25f;

    // Create breakable walls
    m_Walls = new MapObject<Data::WallIcon>[Data::WallsCount];
    MapObject<Data::WallIcon>::Init("romfs:/wall.png", Data::WallsCount);
    MapObject<Data::WallIcon>::m_SizeFactor = 0.25f;

    // Create locations
    m_Locations = new MapLocation[Data::LocationsCount];

    UpdateMapObjects();
    Log("MAP: initialization complete");
    return true;
}

void Map::UpdateMapObjects(bool recenter)
{
    if (!SavefileIO::LoadedSavefile)
        return;

    bool manualProgressChanged = false;

    for (int i = 0; i < Data::KoroksCount; i++) // Korok
    {
        m_Koroks[i].m_Position = glm::vec2(Data::Koroks[i].x, -Data::Koroks[i].y) * MapScale;

        m_Koroks[i].m_ObjectData = &Data::Koroks[i];

        // Check if the korok has been found (if the found vector contains it)
        const bool foundInSave = std::find(
            SavefileIO::foundKoroks.begin(),
            SavefileIO::foundKoroks.end(),
            &Data::Koroks[i]) != SavefileIO::foundKoroks.end();
        m_Koroks[i].m_Found = ResolveFound(Data::ObjectType::Korok, Data::Koroks[i].hash, foundInSave, manualProgressChanged);
    }

    for (int i = 0; i < Data::ShrineCount; i++) // Shrine
    {
        m_Shrines[i].m_Position = glm::vec2(Data::Shrines[i].x, -Data::Shrines[i].y) * MapScale;

        // Check if the korok has been found (if the found vector contains it)
        const bool foundInSave = std::find(
            SavefileIO::foundShrines.begin(),
            SavefileIO::foundShrines.end(),
            &Data::Shrines[i]) != SavefileIO::foundShrines.end();
        m_Shrines[i].m_Found = ResolveFound(Data::ObjectType::Shrine, Data::Shrines[i].hash, foundInSave, manualProgressChanged);
        m_Shrines[i].m_Pending = foundInSave && !(SavefileIO::IsShrineCleared(Data::Shrines[i].hash) &&
                                                  SavefileIO::HasAllShrineChests(Data::Shrines[i].hash));
    }

    for (int i = 0; i < Data::DLCShrineCount; i++) // DLC Shrine
    {
        m_DLCShrines[i].m_Position = glm::vec2(Data::DLCShrines[i].x, -Data::DLCShrines[i].y) * MapScale;

        // Check if the korok has been found (if the found vector contains it)
        const bool foundInSave = std::find(
            SavefileIO::foundDLCShrines.begin(),
            SavefileIO::foundDLCShrines.end(),
            &Data::DLCShrines[i]) != SavefileIO::foundDLCShrines.end();
        m_DLCShrines[i].m_Found = ResolveFound(Data::ObjectType::DLCShrine, Data::DLCShrines[i].hash, foundInSave, manualProgressChanged);
        m_DLCShrines[i].m_Pending = foundInSave && !(SavefileIO::IsShrineCleared(Data::DLCShrines[i].hash) &&
                                                     SavefileIO::HasAllShrineChests(Data::DLCShrines[i].hash));
    }

    for (int i = 0; i < Data::HinoxesCount; i++) // Hinox
    {
        m_Hinoxes[i].m_Position = glm::vec2(Data::Hinoxes[i].x, -Data::Hinoxes[i].y) * MapScale;

        // Check if the korok has been found (if the found vector contains it)
        const bool foundInSave = std::find(
            SavefileIO::defeatedHinoxes.begin(),
            SavefileIO::defeatedHinoxes.end(),
            &Data::Hinoxes[i]) != SavefileIO::defeatedHinoxes.end();
        m_Hinoxes[i].m_Found = ResolveFound(Data::ObjectType::Hinox, Data::Hinoxes[i].hash, foundInSave, manualProgressChanged);
    }

    for (int i = 0; i < Data::TalusesCount; i++) // Talus
    {
        m_Taluses[i].m_Position = glm::vec2(Data::Taluses[i].x, -Data::Taluses[i].y) * MapScale;

        // Check if the korok has been found (if the found vector contains it)
        const bool foundInSave = std::find(
            SavefileIO::defeatedTaluses.begin(),
            SavefileIO::defeatedTaluses.end(),
            &Data::Taluses[i]) != SavefileIO::defeatedTaluses.end();
        m_Taluses[i].m_Found = ResolveFound(Data::ObjectType::Talus, Data::Taluses[i].hash, foundInSave, manualProgressChanged);
    }

    for (int i = 0; i < Data::MoldugasCount; i++) // Molduga
    {
        m_Moldugas[i].m_Position = glm::vec2(Data::Moldugas[i].x, -Data::Moldugas[i].y) * MapScale;

        // Check if the korok has been found (if the found vector contains it)
        const bool foundInSave = std::find(
            SavefileIO::defeatedMoldugas.begin(),
            SavefileIO::defeatedMoldugas.end(),
            &Data::Moldugas[i]) != SavefileIO::defeatedMoldugas.end();
        m_Moldugas[i].m_Found = ResolveFound(Data::ObjectType::Molduga, Data::Moldugas[i].hash, foundInSave, manualProgressChanged);
    }

    for (int i = 0; i < Data::LocationsCount; i++) // Locations
    {
        m_Locations[i].m_Position = glm::vec2(Data::Locations[i].x, -Data::Locations[i].y) * MapScale;
        m_Locations[i].m_LocationData = &Data::Locations[i];

        // Check if the korok has been found (if the found vector contains it)
        const bool foundInSave = std::find(
            SavefileIO::visitedLocations.begin(),
            SavefileIO::visitedLocations.end(),
            &Data::Locations[i]) != SavefileIO::visitedLocations.end();
        m_Locations[i].m_Found = ResolveFound(Data::ObjectType::Location, Data::Locations[i].hash, foundInSave, manualProgressChanged);
        // A visited tower still to be activated, or a fountain whose fairy still sleeps, stays on the map.
        m_Locations[i].m_Pending = foundInSave && LocationState::ForLocation(Data::Locations[i].hash) != nullptr &&
                                   !SavefileIO::IsLocationStateDone(Data::Locations[i].hash);
    }

    // Chests: those of Master Mode only exist there; those of the Expansion Pass need it (an opened one,
    // the Champions' Ballad or a Master Mode save show that the game has it).
    const bool dlcChests = SavefileIO::HasExpansionPass();
    const auto placeChest = [&](auto& object, const Data::Chest& chest, Data::ObjectType type)
    {
        object.m_Position = glm::vec2(chest.x, -chest.y) * MapScale;
        object.m_Excluded = ((chest.flags & Data::ChestMasterModeOnly) && !m_LoadMasterMode) ||
                            ((chest.flags & Data::ChestDlc) && !dlcChests);
        object.m_Found = !object.m_Excluded &&
                         ResolveFound(type, chest.hash, SavefileIO::IsChestOpened(chest.hash), manualProgressChanged);
    };
    for (int i = 0; i < Data::ChestsCount; i++)
        placeChest(m_Chests[i], Data::Chests[i], Data::ObjectType::Chest);
    for (int i = 0; i < Data::SpecialChestsCount; i++)
        placeChest(m_SpecialChests[i], Data::SpecialChests[i], Data::ObjectType::SpecialChest);

    for (int i = 0; i < Data::WallsCount; i++) // Breakable walls
    {
        m_Walls[i].m_Position = glm::vec2(Data::Walls[i].x, -Data::Walls[i].y) * MapScale;
        m_Walls[i].m_Found = ResolveFound(Data::ObjectType::BreakableWall, Data::Walls[i].hash,
                                          SavefileIO::IsWallBroken(Data::Walls[i].hash), manualProgressChanged);
    }

    // The region each object is in, for the map of one region and the counters by region
    for (int i = 0; i < Data::KoroksCount; i++) m_Koroks[i].m_Region = Regions::Of(Data::Koroks[i].hash);
    for (int i = 0; i < Data::ShrineCount; i++) m_Shrines[i].m_Region = Regions::Of(Data::Shrines[i].hash);
    for (int i = 0; i < Data::DLCShrineCount; i++) m_DLCShrines[i].m_Region = Regions::Of(Data::DLCShrines[i].hash);
    for (int i = 0; i < Data::HinoxesCount; i++) m_Hinoxes[i].m_Region = Regions::Of(Data::Hinoxes[i].hash);
    for (int i = 0; i < Data::TalusesCount; i++) m_Taluses[i].m_Region = Regions::Of(Data::Taluses[i].hash);
    for (int i = 0; i < Data::MoldugasCount; i++) m_Moldugas[i].m_Region = Regions::Of(Data::Moldugas[i].hash);
    for (int i = 0; i < Data::LocationsCount; i++) m_Locations[i].m_Region = Regions::Of(Data::Locations[i].hash);
    for (int i = 0; i < Data::ChestsCount; i++) m_Chests[i].m_Region = Regions::Of(Data::Chests[i].hash);
    for (int i = 0; i < Data::SpecialChestsCount; i++) m_SpecialChests[i].m_Region = Regions::Of(Data::SpecialChests[i].hash);
    for (int i = 0; i < Data::WallsCount; i++) m_Walls[i].m_Region = Regions::Of(Data::Walls[i].hash);

    if (manualProgressChanged)
        ManualProgress::Flush();

    // Start where Link is (the last save, or right now when read from the running game).
    s_NextMissingActive = false;
    if (recenter && HasPlayerPosition())
    {
        m_CameraPosition = PlayerMapPosition();
        m_CursorPosition = m_CameraPosition;
        m_HasTargetCameraPosition = false;
    }

    Log("Updated map objects");
}

void Map::Update()
{
    if (m_Pad == nullptr) return;

    Ui::Tick();

    u64 buttonsPressed = padGetButtonsDown(m_Pad);

    const bool imageViewerWasOpen = m_ImageViewer != nullptr && m_ImageViewer->IsOpen();
    if (imageViewerWasOpen)
    {
        if (buttonsPressed & HidNpadButton_B) // zayr7: B goes back everywhere
            m_ImageViewer->Close();
        buttonsPressed = 0;
    }

    float zoomAmount = 0.015f;
    float dragAmont = 0.85f;
    float analogStickMovementSpeed = 10.0f;

    // Handle zooming like BotW
    HidAnalogStickState analog_stick_r = padGetStickPos(m_Pad, 1);

    // Get the stick position between -1.0f and 1.0f, instead of -32767 and 32767
    glm::vec2 stickRPosition = glm::vec2((float)analog_stick_r.x / (float)JOYSTICK_MAX, (float)analog_stick_r.y / (float)JOYSTICK_MAX);

    float deadzone = 0.1f;
    if (!imageViewerWasOpen && fabs(stickRPosition.y) >= deadzone)
        m_Zoom *= 1.0f + zoomAmount * stickRPosition.y;

    // Reset zoom if pressing L or R stick
    if (!imageViewerWasOpen && (buttonsPressed & HidNpadButton_StickL))
    {
        m_Zoom = m_DefaultZoom;
        m_CameraPosition = HasPlayerPosition() ? PlayerMapPosition() : glm::vec2(0.0f, 0.0f);
        m_HasTargetCameraPosition = false;
    }

    // L glides to Link and keeps the zoom
    if (!imageViewerWasOpen && (buttonsPressed & HidNpadButton_L) && HasPlayerPosition())
    {
        m_TargetCameraPosition = PlayerMapPosition();
        m_HasTargetCameraPosition = true;
        m_CursorFollowsTarget = true;
    }

    const bool modalDialogOpen = ModalDialogOpen();

    // zayr7: X only opens and closes the legend; an open card or Korok hint gives way to it
    if (!imageViewerWasOpen && (buttonsPressed & HidNpadButton_X) && !m_NoSavefileDialog->m_IsOpen)
    {
        if (m_Legend->m_IsOpen)
            m_Legend->m_IsOpen = false;
        else
        {
            CloseInfoPanels();
            m_Legend->m_IsOpen = true;
        }
    }

    // zayr7: Y marks the open Korok hint or card as found; with neither open it toggles master mode
    if (!imageViewerWasOpen && (buttonsPressed & HidNpadButton_Y) && m_KorokDialog->m_IsOpen)
    {
        if (CanMarkByHand() && m_KorokDialog->m_KorokIndex >= 0 && !m_Koroks[m_KorokDialog->m_KorokIndex].m_Found)
        {
            m_Koroks[m_KorokDialog->m_KorokIndex].m_Found = true;
            if (ManualProgress::MarkFound(CurrentProgressContext(), Data::ObjectType::Korok,
                                          m_Koroks[m_KorokDialog->m_KorokIndex].m_ObjectData->hash))
                ManualProgress::Flush();
            Ui::Toast(Localization::Get(Localization::Text::ToastMarked));
        }
    }
    else if (!imageViewerWasOpen && (buttonsPressed & HidNpadButton_Y) && m_ObjectInfo->m_IsOpen)
        MarkSelectedObjectFound();
    else if (!imageViewerWasOpen && !modalDialogOpen && (buttonsPressed & HidNpadButton_Y))
    {
        if (SavefileIO::MostRecentMasterModeFile != -1)
        {
            const bool requestedMode = !m_LoadMasterMode;
            if (SavefileIO::LoadGamesave(requestedMode))
            {
                m_LoadMasterMode = ModeChangeDecision::Resolve(m_LoadMasterMode, requestedMode, true);
                UpdateMapObjects();
            }
        }
    }

    // "-" reads the progress again (the running game's, when the app was opened from the Album)
    if (!imageViewerWasOpen && !modalDialogOpen && (buttonsPressed & HidNpadButton_Minus))
        Sync();

    // zayr7: B closes the panel on top (the image viewer is handled above)
    if (!imageViewerWasOpen && (buttonsPressed & HidNpadButton_B))
    {
        if (m_KorokDialog->m_IsOpen)
            m_KorokDialog->SetOpen(false);
        else if (m_ObjectInfo->m_IsOpen)
            m_ObjectInfo->SetOpen(false);
        else if (m_Legend->m_IsOpen)
            m_Legend->m_IsOpen = false;
    }

    // A on a Korok card shows its photo full screen; on a chest card it tells a bit more of what is inside
    if (!imageViewerWasOpen && (buttonsPressed & HidNpadButton_A) && m_KorokDialog->m_IsOpen && m_KorokDialog->HasImage())
        m_ImageViewer->Open(m_KorokDialog->m_Image);
    else if (!imageViewerWasOpen && (buttonsPressed & HidNpadButton_A) && m_ObjectInfo->m_IsOpen && m_ObjectInfo->CanReveal())
        m_ObjectInfo->RevealMore();

    // Analog stick camera movement
    // Read the sticks' position
    HidAnalogStickState analog_stick_l = padGetStickPos(m_Pad, 0);

    // Get the stick position between -1.0f and 1.0f, instead of -32767 and 32767
    glm::vec2 stickLPosition = glm::vec2((float)analog_stick_l.x / (float)JOYSTICK_MAX, (float)analog_stick_l.y / (float)JOYSTICK_MAX);

    float distanceToCenter = glm::distance(stickLPosition, glm::vec2(0.0f, 0.0f));
    if (!imageViewerWasOpen && distanceToCenter >= deadzone)
    {
        m_CameraPosition += stickLPosition * (analogStickMovementSpeed / m_Zoom);
        m_HasTargetCameraPosition = false;
        m_CursorFollowsTarget = false;
    }

    if (!imageViewerWasOpen && !m_Legend->m_IsOpen && !m_KorokDialog->m_IsOpen && !m_ObjectInfo->m_IsOpen && !modalDialogOpen)
    {
        // zayr7: the cursor is the crosshair at the screen center; the D-Pad moves the map under it:
        // a step for each press, then it keeps going while held.
        const u64 dpad = HidNpadButton_Left | HidNpadButton_Right | HidNpadButton_Up | HidNpadButton_Down;
        const u64 held = padGetButtons(m_Pad);
        s_DpadHeldFrames = (held & dpad) ? s_DpadHeldFrames + 1 : 0;
        const u64 moving = (buttonsPressed & dpad) ? (buttonsPressed & dpad) : (s_DpadHeldFrames > 18 ? (held & dpad) : 0);
        const float cursorStep = ((buttonsPressed & dpad) ? 12.0f : 5.0f) / m_Zoom;
        glm::vec2 step(0.0f, 0.0f);
        if (moving & HidNpadButton_Left) step.x -= cursorStep;
        if (moving & HidNpadButton_Right) step.x += cursorStep;
        if (moving & HidNpadButton_Up) step.y += cursorStep;
        if (moving & HidNpadButton_Down) step.y -= cursorStep;
        if (step.x != 0.0f || step.y != 0.0f)
        {
            m_CameraPosition += step;
            m_HasTargetCameraPosition = false;
            m_CursorFollowsTarget = false;
        }
        if ((buttonsPressed & HidNpadButton_A) && SavefileIO::LoadedSavefile && !OpenNearestObject(m_CameraPosition))
            Ui::Toast(Localization::Get(Localization::Text::ToastNothingHere), true);
    }
    else
        s_DpadHeldFrames = 0;
    // ZR/ZL/R open the card of the object they pick, so they also work with a card or the legend open.
    if (!imageViewerWasOpen && !modalDialogOpen && (buttonsPressed & HidNpadButton_ZR))
        FocusMissing(1);
    else if (!imageViewerWasOpen && !modalDialogOpen && (buttonsPressed & HidNpadButton_ZL))
        FocusMissing(-1);
    else if (!imageViewerWasOpen && !modalDialogOpen && (buttonsPressed & HidNpadButton_R))
        FocusNearestToCenter();

    if (!imageViewerWasOpen && m_HasTargetCameraPosition)
    {
        const glm::vec2 delta = m_TargetCameraPosition - m_CameraPosition;
        if (glm::dot(delta, delta) < 1.0f)
        {
            m_CameraPosition = m_TargetCameraPosition;
            m_HasTargetCameraPosition = false;
            if (m_CursorFollowsTarget)
            {
                m_CursorPosition = m_TargetCameraPosition;
                m_CursorFollowsTarget = false;
            }
        }
        else
            m_CameraPosition += delta * 0.12f;
    }
    ClampCamera();
    ClampCursorToViewport();

    // With the legend or a card open the map slides to the right, so that what is under the aim
    // stays in the middle of the part of the screen that is free.
    const float shiftTarget = LeftPanelOpen() && !ModalDialogOpen() ? Ui::PanelWidth * 0.5f : 0.0f;
    m_ViewShift += (shiftTarget - m_ViewShift) * 0.25f;
    if (std::fabs(shiftTarget - m_ViewShift) < 0.5f)
        m_ViewShift = shiftTarget;

    UpdateViewMatrix();

    // Touch routing keeps card controls local while allowing selection and dragging on the free map area.
    HidTouchScreenState state={0};
    if (hidGetTouchScreenStates(&state, 1)) {
        const bool hasTouch = state.count >= 1;
        const glm::vec2 touchPosition = hasTouch
            ? glm::vec2(state.touches[0].x - m_CameraWidth / 2, -(state.touches[0].y - m_CameraHeight / 2))
            : glm::vec2(0.0f);

        if (imageViewerWasOpen)
        {
            m_IsDragging = false;
            if (state.count != m_PrevTouchCount)
            {
                if (state.count == 1)
                    m_ImageViewer->Close();
                m_PrevTouchCount = state.count;
            }
        }
        else
        {
            if (state.count != m_PrevTouchCount)
            {
                m_PrevTouchCount = state.count;
                const bool onDialog = (m_Legend->m_IsOpen && m_Legend->IsPositionOnLegend(touchPosition)) ||
                    (m_NoSavefileDialog->m_IsOpen && m_NoSavefileDialog->IsPositionOn(touchPosition)) ||
                    (m_GameRunningDialog->m_IsOpen && m_GameRunningDialog->IsPositionOn(touchPosition)) ||
                    (m_MasterModeDialog->m_IsOpen && m_MasterModeDialog->IsPositionOn(touchPosition));
                const bool onKorokCard = m_KorokDialog->m_IsOpen && m_KorokDialog->IsPositionOn(touchPosition);
                const bool onObjectCard = m_ObjectInfo->m_IsOpen && m_ObjectInfo->IsPositionOn(touchPosition);
                const bool onFooter = touchPosition.y < m_ScreenBottom + Ui::FooterHeight;

                if (state.count == 1 && !onDialog && !onFooter)
                {
                    m_HasTargetCameraPosition = false;
                    m_CursorFollowsTarget = false;
                    if (onKorokCard || onObjectCard)
                    {
                        m_IsDragging = false;
                        if (onKorokCard && m_KorokDialog->IsImagePositionOn(touchPosition))
                            m_ImageViewer->Open(m_KorokDialog->m_Image);
                    }
                    else
                    {
                        const bool selected = SavefileIO::LoadedSavefile && OpenNearestObject(ScreenToMap(touchPosition));
                        m_IsDragging = !selected;
                        m_PrevTouchPosition = touchPosition;
                    }
                }
                else if (state.count == 0)
                    m_IsDragging = false;
            }

            if (hasTouch && m_IsDragging)
            {
                const glm::vec2 delta = m_PrevTouchPosition - touchPosition;
                m_CameraPosition += (delta * dragAmont) / m_Zoom;
                m_PrevTouchPosition = touchPosition;
            }
        }
    }

    ClampCamera();
    UpdateViewMatrix();

    if (m_Legend->m_IsOpen)
        m_Legend->Update();

    if (m_NoSavefileDialog->m_IsOpen)
        m_NoSavefileDialog->Update();
    if (m_GameRunningDialog->m_IsOpen)
        m_GameRunningDialog->Update();
    if (m_MasterModeDialog->m_IsOpen)
        m_MasterModeDialog->Update();

    // Update objects
    if (SavefileIO::LoadedSavefile)
    {
        // Clear the meshes during the first iteration of the loop (i == 0) == true
        for (int i = 0; i < Data::KoroksCount; i++)
            m_Koroks[i].Update(i == 0);
        for (int i = 0; i < Data::ShrineCount; i++)
            m_Shrines[i].Update(i == 0);
        for (int i = 0; i < Data::DLCShrineCount; i++)
            m_DLCShrines[i].Update(i == 0);
        for (int i = 0; i < Data::HinoxesCount; i++)
            m_Hinoxes[i].Update(i == 0);
        for (int i = 0; i < Data::TalusesCount; i++)
            m_Taluses[i].Update(i == 0);
        for (int i = 0; i < Data::MoldugasCount; i++)
            m_Moldugas[i].Update(i == 0);
        // The chests are many and start switched off: their meshes are only built while they show.
        if (m_Legend->m_Show[IconButton::ButtonTypes::Chests])
            for (int i = 0; i < Data::ChestsCount; i++)
                m_Chests[i].Update(i == 0);
        if (m_Legend->m_Show[IconButton::ButtonTypes::SpecialChests])
            for (int i = 0; i < Data::SpecialChestsCount; i++)
                m_SpecialChests[i].Update(i == 0);
        if (m_Legend->m_Show[IconButton::ButtonTypes::Walls])
            for (int i = 0; i < Data::WallsCount; i++)
                m_Walls[i].Update(i == 0);
        for (int i = 0; i < Data::LocationsCount; i++)
            m_Locations[i].Update();

    }

    // The middle of what is on screen is to the left of the camera while the map is shifted.
    MapHd::Update(m_CameraPosition - glm::vec2(m_ViewShift / m_Zoom, 0.0f), m_Zoom,
                  m_CameraWidth / (2.0f * m_Zoom), m_CameraHeight / (2.0f * m_Zoom));

    m_PrevCameraPosition = m_CameraPosition;
    ClampCursorToViewport();
}

void Map::Render()
{
    m_MapBackground.Render();
    MapHd::Render(&m_ProjectionMatrix, &m_ViewMatrix);

    if (SavefileIO::LoadedSavefile)
    {
        // The many ordinary chests go under everything else
        if (m_Legend->m_Show[IconButton::ButtonTypes::Chests])
            MapObject<Data::ChestIcon>::Render();

        if (m_Legend->m_Show[IconButton::ButtonTypes::Koroks])
        {
            // Render korok paths
            for (int k = 0; k < Data::KoroksCount; k++)
            {
                // This korok has no paths
                if (m_Koroks[k].m_ObjectData->path == nullptr)
                    continue;

                // Don't render if found, or outside the region shown
                if (!m_Legend->ShouldShow(m_Koroks[k].m_Found) || !InRegionFilter(m_Koroks[k].m_Region))
                    continue;

                Data::KorokPath* path = m_Koroks[k].m_ObjectData->path;

                // 0 -> 1
                // 1 -> 2
                // 2 -> 3
                for (unsigned int p = 1; p < path->points.size(); p++)
                {
                    glm::vec2 start = path->points[p - 1] * MapScale;
                    start.y *= -1; // Flip the y coord
                    glm::vec2 end = path->points[p] * MapScale;
                    end.y *= -1;

                    float width = (1.0f / m_Zoom) * 2.0f;
                    if (m_Zoom >= 3.0f)
                        width = 0.75f;

                    m_LineRenderer->AddLine(start, end, width);
                }
            }

            m_LineRenderer->RenderLines(m_ProjectionMatrix, m_ViewMatrix);

            MapObject<Data::Korok>::Render();
        }
        if (m_Legend->m_Show[IconButton::ButtonTypes::Shrines])
            MapObject<Data::Shrine>::Render();
        if (m_Legend->m_Show[IconButton::ButtonTypes::Shrines] && SavefileIO::DlcShrinesAppear())
            MapObject<Data::DLCShrine>::Render();
        if (m_Legend->m_Show[IconButton::ButtonTypes::Hinoxes])
            MapObject<Data::Hinox>::Render();
        if (m_Legend->m_Show[IconButton::ButtonTypes::Taluses])
            MapObject<Data::Talus>::Render();
        if (m_Legend->m_Show[IconButton::ButtonTypes::Moldugas])
           MapObject<Data::Molduga>::Render();
        if (m_Legend->m_Show[IconButton::ButtonTypes::Walls])
            MapObject<Data::WallIcon>::Render();
        if (m_Legend->m_Show[IconButton::ButtonTypes::SpecialChests])
            MapObject<Data::SpecialChestIcon>::Render();
        if (m_Legend->m_Show[IconButton::ButtonTypes::Locations])
        {
            // Names of the locations, in map coordinates: the dark edge first, the text over it.
            m_Font.m_ViewMatrix = &m_ViewMatrix;
            for (int pass = 0; pass < 2; ++pass)
            {
                m_Font.BeginBatch();
                MapLocation::BeginLabelPass();
                for (int i = 0; i < Data::LocationsCount; i++)
                    m_Locations[i].Render(pass == 0);
                m_Font.RenderBatch();
            }
        }
        RenderSelectedObjectHighlight();

        if (HasPlayerPosition() && s_PlayerMarker.m_Texture)
        {
            s_PlayerMarker.m_Position = MapToScreen(PlayerMapPosition());
            s_PlayerMarker.Render();
        }
    }

    // zayr7: crosshair fixed at the screen center; A and R pick from here
    if (SavefileIO::LoadedSavefile && !LeftPanelOpen() && !ModalDialogOpen() && s_Crosshair.m_Texture)
    {
        s_Crosshair.m_Position = glm::vec2(m_ViewShift, 0.0f);
        s_Crosshair.Render();
    }

    // The interface, in screen coordinates. Shapes and images are drawn as they come; all the text at the end.
    glm::mat4 emptyViewMatrix(1.0);
    m_Font.m_ViewMatrix = &emptyViewMatrix;
    m_Font.BeginBatch();

    if (m_LoadMasterMode)
        m_MasterModeIcon.Render();

    if (m_Legend->m_IsOpen)
        m_Legend->Render();
    m_KorokDialog->Render();
    m_ObjectInfo->Render();

    DrawHud();
    Ui::FlushShapes();
    m_Font.RenderBatch();

    if (m_NoSavefileDialog->m_IsOpen)
        m_NoSavefileDialog->Render();
    if (m_GameRunningDialog->m_IsOpen)
        m_GameRunningDialog->Render();
    if (m_MasterModeDialog->m_IsOpen)
        m_MasterModeDialog->Render();

    if (m_ImageViewer->IsOpen())
    {
        m_Font.m_ViewMatrix = &emptyViewMatrix;
        m_Font.BeginBatch();
        m_ImageViewer->Render(m_ProjectionMatrix);
        std::vector<Ui::Hint> hints;
        std::string context;
        BuildHints(hints, context);
        Ui::Footer(hints, context);
        Ui::FlushShapes();
        m_Font.RenderBatch();
    }

    m_Font.m_ViewMatrix = &m_ViewMatrix;
}

bool Map::IsInView(glm::vec2 position, float margin = 100.0f)
{
    // Calculate camera bounds (the map is drawn m_ViewShift pixels to the right)
    float viewLeft = m_CameraPosition.x - (m_CameraWidth / 2 + m_ViewShift) / m_Zoom - margin;
    float viewRight = m_CameraPosition.x + (m_CameraWidth / 2 - m_ViewShift) / m_Zoom + margin;
    float viewBottom = m_CameraPosition.y - (m_CameraHeight / 2) / m_Zoom - margin;
    float viewTop = m_CameraPosition.y + (m_CameraHeight / 2) / m_Zoom + margin;

    // Check if the position would be outside of view (horizontal)
    if (position.x < viewLeft || position.x > viewRight)
        return false;

    // Check if the position would be outside of view (vertical)
    if (position.y < viewBottom || position.y > viewTop)
        return false;

    return true;
}

glm::vec2 Map::MapToScreen(const glm::vec2& mapPosition)
{
    return (mapPosition - m_CameraPosition) * m_Zoom + glm::vec2(m_ViewShift, 0.0f);
}

glm::vec2 Map::ScreenToMap(const glm::vec2& screenPosition)
{
    return (screenPosition - glm::vec2(m_ViewShift, 0.0f)) / m_Zoom + m_CameraPosition;
}

void Map::Destroy()
{
    delete[] m_Koroks;
    m_Koroks = nullptr;
    delete[] m_Shrines;
    m_Shrines = nullptr;
    delete[] m_DLCShrines;
    m_DLCShrines = nullptr;
    delete[] m_Hinoxes;
    m_Hinoxes = nullptr;
    delete[] m_Taluses;
    m_Taluses = nullptr;
    delete[] m_Moldugas;
    m_Moldugas = nullptr;
    delete[] m_Chests;
    m_Chests = nullptr;
    delete[] m_SpecialChests;
    m_SpecialChests = nullptr;
    delete[] m_Walls;
    m_Walls = nullptr;
    delete[] m_Locations;
    m_Locations = nullptr;

    delete m_Legend;
    m_Legend = nullptr;
    delete m_NoSavefileDialog;
    m_NoSavefileDialog = nullptr;
    delete m_GameRunningDialog;
    m_GameRunningDialog = nullptr;
    delete m_MasterModeDialog;
    m_MasterModeDialog = nullptr;
    delete m_KorokDialog;
    m_KorokDialog = nullptr;
    delete m_ObjectInfo;
    m_ObjectInfo = nullptr;
    delete m_ImageViewer;
    m_ImageViewer = nullptr;
    delete m_LineRenderer;
    m_LineRenderer = nullptr;
    Ui::Destroy();

    m_Font.Destroy();
    MapHd::Destroy(); // before the background, whose Destroy also deletes the shared textured-quad shader
    m_MapBackground.Destroy();
    m_MasterModeIcon.Destroy();
    s_PlayerMarker.Destroy();
    s_Crosshair.Destroy();
}

static void FocusObject(const MapEntry& target)
{
    Map::m_TargetCameraPosition = target.position;
    Map::m_CursorPosition = target.position;
    Map::m_HasTargetCameraPosition = true;
    Map::m_CursorFollowsTarget = true;
    OpenObject(target);
}

bool Map::OpenNearestObject(const glm::vec2& mapPosition)
{
    std::vector<MapEntry> entries;
    CollectObjects(entries);
    const int best = ObjectAt(entries, mapPosition);
    if (best < 0)
        return false;
    OpenObject(entries[best]);
    return true;
}

void Map::MarkSelectedObjectFound()
{
    if (!m_ObjectInfo->m_IsOpen || m_ObjectInfo->m_Found == nullptr || *m_ObjectInfo->m_Found || !CanMarkByHand())
        return;
    *m_ObjectInfo->m_Found = true;
    if (ManualProgress::MarkFound(CurrentProgressContext(), m_ObjectInfo->m_Type, m_ObjectInfo->m_CompletionHash))
        ManualProgress::Flush();
    Ui::Toast(Localization::Get(Localization::Text::ToastMarked));
}

void Map::FocusMissing(int step)
{
    if (!SavefileIO::LoadedSavefile)
        return;

    std::vector<MapEntry> entries;
    CollectObjects(entries);
    std::vector<Navigation::Candidate> candidates;
    candidates.reserve(entries.size());
    for (const MapEntry& entry : entries)
        candidates.push_back({{entry.position.x, entry.position.y}, entry.visible, *entry.found && !entry.pending});

    // Distances from Link when the save has a position, otherwise from where the camera was at the
    // first press, until the map is moved by hand.
    const bool cameraMoved = !m_HasTargetCameraPosition && glm::distance(m_CameraPosition, s_NextMissingTarget) > 1.0f;
    if (HasPlayerPosition())
        s_NextMissingOrigin = PlayerMapPosition();
    else if (!s_NextMissingActive || cameraMoved)
        s_NextMissingOrigin = m_CameraPosition;

    const std::vector<int> nearest = Navigation::NearestByDistance(
        candidates, {s_NextMissingOrigin.x, s_NextMissingOrigin.y}, true, NextMissingCount);
    if (nearest.empty())
    {
        s_NextMissingActive = false;
        Ui::Toast(Localization::Get(Localization::Text::ToastNothingMissing), true);
        return;
    }

    // Same five as before: step to the next (ZR) or previous (ZL) one, wrapping around. A different list
    // (filters changed, save reloaded, an object marked as found, another origin) starts from the nearest.
    const int count = static_cast<int>(nearest.size());
    if (s_NextMissingActive && nearest == s_NextMissingList)
        s_NextMissingRank = ((s_NextMissingRank + step) % count + count) % count;
    else
        s_NextMissingRank = 0;
    s_NextMissingList = nearest;
    s_NextMissingActive = true;

    const MapEntry& target = entries[nearest[s_NextMissingRank]];
    s_NextMissingTarget = target.position;
    FocusObject(target);
}

void Map::FocusNearestToCenter()
{
    if (!SavefileIO::LoadedSavefile)
        return;

    std::vector<MapEntry> entries;
    CollectObjects(entries);
    std::vector<Navigation::Candidate> candidates;
    candidates.reserve(entries.size());
    for (const MapEntry& entry : entries)
        candidates.push_back({{entry.position.x, entry.position.y}, entry.visible, *entry.found && !entry.pending});

    const std::vector<int> nearest = Navigation::NearestByDistance(
        candidates, {m_CameraPosition.x, m_CameraPosition.y}, true, 1);
    if (nearest.empty())
    {
        Ui::Toast(Localization::Get(Localization::Text::ToastNothingMissing), true);
        return;
    }

    s_NextMissingActive = false; // the next ZR starts again from the nearest to Link
    FocusObject(entries[nearest[0]]);
}

void Map::CloseInfoPanels()
{
    if (m_ImageViewer != nullptr)
        m_ImageViewer->Close();
    if (m_KorokDialog != nullptr)
        m_KorokDialog->SetOpen(false);
    if (m_ObjectInfo != nullptr)
        m_ObjectInfo->SetOpen(false);
    if (m_Legend != nullptr)
        m_Legend->m_IsOpen = false;
}

void Map::ClampCursorToViewport()
{
    // zayr7: the cursor is always the screen center (the crosshair)
    m_CursorPosition = m_CameraPosition;
}

void Map::Sync()
{
    // Nothing can change while this app is open unless a game runs behind it.
    if (!SavefileIO::LoadedSavefile || !SavefileIO::GameIsRunning)
        return;

    using Localization::Text;
    const int before = CountFound();
    if (!SavefileIO::LoadGamesave(m_LoadMasterMode, false, true))
        return;
    m_LoadMasterMode = SavefileIO::MasterModeFileLoaded;
    UpdateMapObjects(false);

    if (SavefileIO::GameIsRunning && !SavefileIO::LiveSynced)
    {
        Ui::Toast(Localization::Get(Text::ToastSyncFailed) + ": " + SavefileIO::CurrentState.liveMessage, true);
        return;
    }
    const int added = CountFound() - before;
    if (added > 0)
        Ui::Toast(Localization::Get(Text::ToastSynced) + ": " + std::to_string(added) + " " + Localization::Get(Text::NewSince));
    else
        Ui::Toast(Localization::Get(Text::ToastNoChange));
}

std::string Map::SourceText(glm::vec3& dotColor, bool& hasDot)
{
    using Localization::Text;
    hasDot = false;
    if (!SavefileIO::LoadedSavefile)
        return std::string();

    if (SavefileIO::LiveSynced)
    {
        hasDot = true;
        dotColor = Ui::Colors::Good;
        return Localization::Get(Text::LiveFromGame);
    }

    std::string text = Localization::Get(SavefileIO::GameIsRunning ? Text::SaveBackup : Text::SaveLabel);
    if (SavefileIO::GameIsRunning)
    {
        hasDot = true;
        dotColor = Ui::Colors::Warn;
    }
    const time_t saveTime = static_cast<time_t>(SavefileIO::CurrentState.saveTime);
    if (saveTime != 0)
    {
        char when[32] = "";
        if (const struct tm* local = std::localtime(&saveTime))
            std::strftime(when, sizeof(when), "%d %b %H:%M", local);
        if (when[0] != '\0')
            text += std::string(" - ") + when;
    }
    return text;
}

std::string Map::NearestRankText()
{
    glm::vec2 selected;
    if (!s_NextMissingActive || !SelectedObjectPosition(selected) || selected != s_NextMissingTarget)
        return std::string();
    return Localization::Get(Localization::Text::NextMissing) + " " + std::to_string(s_NextMissingRank + 1) + " " +
           Localization::Get(Localization::Text::Of) + " " + std::to_string(s_NextMissingList.size());
}

bool Map::IsManuallyMarked(Data::ObjectType type, uint32_t hash)
{
    return !ProgressIsCurrent() && ManualProgress::IsMarkedFound(CurrentProgressContext(), type, hash);
}

TexturedQuad Map::m_MapBackground;
Font Map::m_Font;
LineRenderer* Map::m_LineRenderer;
TexturedQuad Map::m_MasterModeIcon;

float Map::m_Zoom = Map::m_DefaultZoom;
float Map::m_ViewShift = 0.0f;
int Map::m_RegionFilter = -1;

bool Map::InRegionFilter(int region)
{
    return m_RegionFilter < 0 || region == m_RegionFilter;
}

void Map::SetRegionFilter(int region)
{
    m_RegionFilter = region >= 0 && region < Regions::Count ? region : Regions::None;
    s_NextMissingActive = false; // the nearest missing objects are others now
    if (m_RegionFilter < 0)
        return;

    // Bring the region into the part of the screen that the panel leaves free
    const Data::RegionBounds& bounds = Regions::Bounds(m_RegionFilter);
    const float width = (bounds.maxX - bounds.minX) * MapScale;
    const float height = (bounds.maxZ - bounds.minZ) * MapScale;
    const float freeWidth = m_CameraWidth - Ui::PanelWidth - 60.0f;
    const float freeHeight = m_CameraHeight - Ui::FooterHeight - 60.0f;
    m_Zoom = std::max(MinZoom, std::min(1.5f, std::min(freeWidth / width, freeHeight / height)));
    m_TargetCameraPosition = glm::vec2((bounds.minX + bounds.maxX) * 0.5f, -(bounds.minZ + bounds.maxZ) * 0.5f) * MapScale;
    m_HasTargetCameraPosition = true;
    m_CursorFollowsTarget = true;
}

glm::mat4 Map::m_ProjectionMatrix = glm::mat4(1.0f);
glm::mat4 Map::m_ViewMatrix = glm::mat4(1.0f);

glm::vec2 Map::m_CameraPosition = glm::vec2(0.0f, 0.0f);
glm::vec2 Map::m_PrevCameraPosition;

int Map::m_PrevTouchCount = 0;
glm::vec2 Map::m_PrevTouchPosition;
glm::vec2 Map::m_StartDragPos;
bool Map::m_IsDragging = false;
bool Map::m_ShouldExit = false;
bool Map::m_LoadMasterMode = false;
glm::vec2 Map::m_CursorPosition(0.0f);
glm::vec2 Map::m_TargetCameraPosition(0.0f);
bool Map::m_HasTargetCameraPosition = false;
bool Map::m_CursorFollowsTarget = false;

PadState* Map::m_Pad;
MapObject<Data::Korok>* Map::m_Koroks;
MapObject<Data::Shrine>* Map::m_Shrines;
MapObject<Data::DLCShrine>* Map::m_DLCShrines;
MapObject<Data::Hinox>* Map::m_Hinoxes;
MapObject<Data::Talus>* Map::m_Taluses;
MapObject<Data::Molduga>* Map::m_Moldugas;
MapObject<Data::ChestIcon>* Map::m_Chests;
MapObject<Data::SpecialChestIcon>* Map::m_SpecialChests;
MapObject<Data::WallIcon>* Map::m_Walls;
MapLocation* Map::m_Locations;

Legend* Map::m_Legend;
KorokDialog* Map::m_KorokDialog;
ObjectInfo* Map::m_ObjectInfo;
ImageViewer* Map::m_ImageViewer;
Dialog* Map::m_NoSavefileDialog;
Dialog* Map::m_GameRunningDialog;
Dialog* Map::m_MasterModeDialog;
