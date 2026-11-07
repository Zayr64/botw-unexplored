#pragma once

#include <cstdint>
#include <vector>

namespace SaveLoadDecision
{
    enum class MountStatus
    {
        Success = 1,
        NotSelected = 0,
        SaveInaccessible = -1,
        NoSave = -2,
        AccountError = -3
    };

    enum class ProfilePickerStatus
    {
        Selected,
        NotSelected,
        Error
    };

    struct Outcome
    {
        bool success = false;
        bool loadedSavefile = false;
        bool masterModeLoaded = false;
        bool showGameRunningDialog = false;
    };

    Outcome Resolve(MountStatus mountStatus, bool requestedMasterMode, bool parseSucceeded);
    ProfilePickerStatus ResolveProfilePicker(bool transportSucceeded, bool validUid);

    struct SlotCandidate
    {
        int slot = -1;
        uint32_t playtime = 0;  // 0 = no valid game_data.sav
        uint32_t timestamp = 0; // save time from caption.sav, 0 = unknown
    };

    // The most recent slot: by save time when every valid slot has one, otherwise by playtime.
    // The save time stays right after loading an older save; the playtime does not. -1 when none is valid.
    int PickMostRecentSlot(const std::vector<SlotCandidate>& slots);
}
