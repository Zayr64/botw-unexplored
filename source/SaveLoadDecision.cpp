#include "SaveLoadDecision.h"

SaveLoadDecision::Outcome SaveLoadDecision::Resolve(MountStatus mountStatus, bool requestedMasterMode, bool parseSucceeded)
{
    Outcome outcome;
    if (mountStatus == MountStatus::Success || mountStatus == MountStatus::SaveInaccessible)
    {
        outcome.success = parseSucceeded;
        outcome.loadedSavefile = parseSucceeded;
        outcome.masterModeLoaded = parseSucceeded && requestedMasterMode;
        outcome.showGameRunningDialog = mountStatus == MountStatus::SaveInaccessible && !parseSucceeded;
    }
    return outcome;
}

SaveLoadDecision::ProfilePickerStatus SaveLoadDecision::ResolveProfilePicker(bool transportSucceeded,
                                                                               bool validUid)
{
    if (!transportSucceeded)
        return ProfilePickerStatus::Error;
    return validUid ? ProfilePickerStatus::Selected : ProfilePickerStatus::NotSelected;
}

int SaveLoadDecision::PickMostRecentSlot(const std::vector<SlotCandidate>& slots)
{
    bool allHaveTimestamp = false;
    for (const SlotCandidate& candidate : slots)
    {
        if (candidate.playtime == 0)
            continue;
        if (candidate.timestamp == 0)
        {
            allHaveTimestamp = false;
            break;
        }
        allHaveTimestamp = true;
    }

    int best = -1;
    uint32_t bestTimestamp = 0;
    uint32_t bestPlaytime = 0;
    for (const SlotCandidate& candidate : slots)
    {
        if (candidate.playtime == 0)
            continue;
        const bool newer = allHaveTimestamp
            ? (candidate.timestamp > bestTimestamp ||
               (candidate.timestamp == bestTimestamp && candidate.playtime >= bestPlaytime))
            : candidate.playtime >= bestPlaytime;
        if (best == -1 || newer)
        {
            best = candidate.slot;
            bestTimestamp = candidate.timestamp;
            bestPlaytime = candidate.playtime;
        }
    }
    return best;
}
