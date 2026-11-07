#pragma once

#include <string>

#include "ObjectTypes.h"

// User interface strings. The app is English only.
namespace Localization
{
    enum class Text
    {
        Exit,
        ChooseAnotherProfile,
        NoSaveDataTitle,
        NoSaveDataDescription,
        GameRunningTitle,
        GameRunningDescription,
        GameRunningDescription2,
        No,
        Yes,
        MasterModeTitle,
        MasterModeDescription,
        Legend,
        Close,
        ImageViewerBack,
        Koroks,
        Shrines,
        Hinoxes,
        Taluses,
        Moldugas,
        Locations,
        MarkFound,
        OpenLegend,
        GameRunning,
        LoadedOlderSave,
        LoadMasterMode,
        LoadNormalMode,
        Controls,
        Position,
        Found,
        NotFound,
        ObjectInfo,
        NextMissing,
        Korok,
        Shrine,
        DLCShrine,
        Location,
        Hinox,
        StoneTalus,
        Molduga,
        MapCompletion,
        ShrineCompleted,
        ShrineAllChests,
        DistanceFromLink,
        Count
    };

    const std::string& Get(Text text);
    const std::string& GetObjectTypeName(Data::ObjectType objectType);
}
