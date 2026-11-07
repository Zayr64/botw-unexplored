#include <cassert>

#include "SaveLoadDecision.h"

int main()
{
    {
        using SaveLoadDecision::PickMostRecentSlot;
        // Like a real save: autosave slots a few minutes apart, slot 0 the newest by time and by playtime.
        assert(PickMostRecentSlot({{0, 11700, 1793017603u}, {1, 11100, 1793017012u}, {5, 11690, 1793017570u}}) == 0);
        // An older save was loaded: slot 2 is newer by time, slot 4 still has more playtime.
        assert(PickMostRecentSlot({{2, 9000, 2000u}, {4, 12000, 1000u}}) == 2);
        // Without a time for every slot, playtime decides (the previous behaviour).
        assert(PickMostRecentSlot({{2, 9000, 2000u}, {4, 12000, 0u}}) == 4);
        // Slots without a valid save are ignored.
        assert(PickMostRecentSlot({{0, 0, 5000u}, {3, 100, 10u}}) == 3);
        assert(PickMostRecentSlot({{0, 0, 0u}}) == -1);
        assert(PickMostRecentSlot({}) == -1);
    }
    using SaveLoadDecision::Resolve;
    using SaveLoadDecision::MountStatus;
    assert(Resolve(MountStatus::Success, false, true).success);
    assert(!Resolve(MountStatus::Success, false, false).success);
    assert(Resolve(MountStatus::SaveInaccessible, false, true).success);
    assert(Resolve(MountStatus::SaveInaccessible, false, true).loadedSavefile);
    assert(!Resolve(MountStatus::SaveInaccessible, false, true).masterModeLoaded);
    assert(Resolve(MountStatus::SaveInaccessible, true, true).success);
    assert(Resolve(MountStatus::SaveInaccessible, true, true).masterModeLoaded);
    assert(!Resolve(MountStatus::SaveInaccessible, false, false).success);
    assert(Resolve(MountStatus::SaveInaccessible, false, false).showGameRunningDialog);
    assert(!Resolve(MountStatus::NotSelected, false, true).success);
    assert(!Resolve(MountStatus::NoSave, false, true).success);
    assert(!Resolve(MountStatus::AccountError, false, true).success);
    assert(SaveLoadDecision::ResolveProfilePicker(true, true) == SaveLoadDecision::ProfilePickerStatus::Selected);
    assert(SaveLoadDecision::ResolveProfilePicker(true, false) == SaveLoadDecision::ProfilePickerStatus::NotSelected);
    assert(SaveLoadDecision::ResolveProfilePicker(false, true) == SaveLoadDecision::ProfilePickerStatus::Error);
    assert(static_cast<int>(MountStatus::NotSelected) != static_cast<int>(MountStatus::NoSave));
}
