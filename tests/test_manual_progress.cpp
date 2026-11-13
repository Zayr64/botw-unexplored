#include <cassert>
#include <sstream>

#include "ManualProgress.h"

int main()
{
    ManualProgress::Clear();
    ManualProgress::Context normal = {1, 2, false};
    ManualProgress::Context master = {1, 2, true};
    ManualProgress::Context otherProfile = {3, 4, false};
    ManualProgress::MarkFound(normal, Data::ObjectType::Korok, 100);
    ManualProgress::MarkFound(master, Data::ObjectType::Korok, 100);
    ManualProgress::MarkFound(otherProfile, Data::ObjectType::Shrine, 200);
    assert(ManualProgress::Entries().size() == 3);
    assert(ManualProgress::IsMarkedFound(normal, Data::ObjectType::Korok, 100));
    assert(!ManualProgress::IsMarkedFound(normal, Data::ObjectType::Shrine, 200));
    ManualProgress::ConfirmFromSave(normal, Data::ObjectType::Korok, 100);
    assert(!ManualProgress::IsMarkedFound(normal, Data::ObjectType::Korok, 100));
    assert(ManualProgress::IsMarkedFound(master, Data::ObjectType::Korok, 100));

    std::stringstream file;
    assert(ManualProgress::Save(file));
    ManualProgress::Clear();
    assert(ManualProgress::Load(file) == ManualProgress::LoadResult::Current);
    assert(ManualProgress::Entries().size() == 2);
    const size_t validEntryCount = ManualProgress::Entries().size();
    std::stringstream truncated("BOTW_UNEXPLORED_MANUAL_PROGRESS\n1\n1 2 0 0\n");
    assert(ManualProgress::Load(truncated) == ManualProgress::LoadResult::Invalid);
    assert(ManualProgress::Entries().size() == validEntryCount);
    std::stringstream garbage("BOTW_UNEXPLORED_MANUAL_PROGRESS\n1\n1 2 0 0 100 trailing\n");
    assert(ManualProgress::Load(garbage) == ManualProgress::LoadResult::Invalid);
    assert(ManualProgress::Entries().size() == validEntryCount);
    std::stringstream malformedMiddle("BOTW_UNEXPLORED_MANUAL_PROGRESS\n1\n1 2 0 0 100\ninvalid line\n3 4 1 1 200\n");
    assert(ManualProgress::Load(malformedMiddle) == ManualProgress::LoadResult::Invalid);
    assert(ManualProgress::Entries().size() == validEntryCount);
    std::stringstream invalid("BOTW_UNEXPLORED_MANUAL_PROGRESS\n99\n");
    assert(ManualProgress::Load(invalid) == ManualProgress::LoadResult::FutureVersion);
    assert(!ManualProgress::CanPersist());
    assert(!ManualProgress::MarkFound(normal, Data::ObjectType::Korok, 300));
    file.clear();
    file.seekg(0);
    assert(ManualProgress::Load(file) == ManualProgress::LoadResult::Current);
    assert(ManualProgress::CanPersist());

    // Without a mark on a chest the file is version 1, which every release reads. With one it is version 2,
    // which the releases from before the chests leave alone instead of calling it invalid.
    ManualProgress::Clear();
    ManualProgress::MarkFound(normal, Data::ObjectType::Molduga, 100);
    std::stringstream base;
    assert(ManualProgress::Save(base));
    assert(base.str() == "BOTW_UNEXPLORED_MANUAL_PROGRESS\n1\n1 2 0 6 100\n");
    assert(ManualProgress::MarkFound(normal, Data::ObjectType::SpecialChest, 500));
    std::stringstream withChest;
    assert(ManualProgress::Save(withChest));
    assert(withChest.str() == "BOTW_UNEXPLORED_MANUAL_PROGRESS\n2\n1 2 0 6 100\n1 2 0 8 500\n");
    ManualProgress::Clear();
    assert(ManualProgress::Load(withChest) == ManualProgress::LoadResult::Current);
    assert(ManualProgress::Entries().size() == 2);
    assert(ManualProgress::IsMarkedFound(normal, Data::ObjectType::SpecialChest, 500));
    assert(!ManualProgress::IsMarkedFound(normal, Data::ObjectType::Chest, 500));
    // The save confirms the chest: the mark goes, and the file is version 1 again.
    assert(ManualProgress::ConfirmFromSave(normal, Data::ObjectType::SpecialChest, 500));
    std::stringstream again;
    assert(ManualProgress::Save(again));
    assert(again.str() == base.str());
    // A mark on a wall makes it version 3, which the releases from before the walls leave alone.
    assert(ManualProgress::MarkFound(normal, Data::ObjectType::BreakableWall, 700));
    std::stringstream withWall;
    assert(ManualProgress::Save(withWall));
    assert(withWall.str() == "BOTW_UNEXPLORED_MANUAL_PROGRESS\n3\n1 2 0 6 100\n1 2 0 9 700\n");
    ManualProgress::Clear();
    assert(ManualProgress::Load(withWall) == ManualProgress::LoadResult::Current);
    assert(ManualProgress::IsMarkedFound(normal, Data::ObjectType::BreakableWall, 700));
    std::stringstream four("BOTW_UNEXPLORED_MANUAL_PROGRESS\n4\n");
    assert(ManualProgress::Load(four) == ManualProgress::LoadResult::FutureVersion);
    std::stringstream reload(base.str());
    assert(ManualProgress::Load(reload) == ManualProgress::LoadResult::Current && ManualProgress::CanPersist());
    std::stringstream zero("BOTW_UNEXPLORED_MANUAL_PROGRESS\n0\n");
    assert(ManualProgress::Load(zero) == ManualProgress::LoadResult::Invalid);
}
