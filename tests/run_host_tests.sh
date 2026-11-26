#!/usr/bin/env bash
set -euo pipefail
compiler="${CXX:-g++}"
sanitizers=()
if [[ "${SANITIZE:-0}" == "1" ]]; then
    sanitizers=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_settings.cpp source/Settings.cpp source/Utf8.cpp -o /tmp/test_settings
/tmp/test_settings
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_manual_progress.cpp source/ManualProgress.cpp -o /tmp/test_manual_progress
/tmp/test_manual_progress
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_legacy_korok_migration.cpp source/LegacyKorokMigration.cpp -o /tmp/test_legacy_korok_migration
/tmp/test_legacy_korok_migration
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_object_model.cpp source/ObjectModel.cpp -o /tmp/test_object_model
/tmp/test_object_model
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_save_load_decision.cpp source/SaveLoadDecision.cpp -o /tmp/test_save_load_decision
/tmp/test_save_load_decision
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_save_parser.cpp source/SaveParser.cpp -o /tmp/test_save_parser
/tmp/test_save_parser
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_mode_change_decision.cpp source/ModeChangeDecision.cpp -o /tmp/test_mode_change_decision
/tmp/test_mode_change_decision
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_navigation.cpp source/Navigation.cpp -o /tmp/test_navigation
/tmp/test_navigation
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_localization.cpp source/Localization.cpp -o /tmp/test_localization
/tmp/test_localization
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_image_viewer_layout.cpp source/ImageViewerLayout.cpp -o /tmp/test_image_viewer_layout
/tmp/test_image_viewer_layout
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_map_completion.cpp source/MapCompletion.cpp -o /tmp/test_map_completion
/tmp/test_map_completion
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_map_hd_grid.cpp source/MapHdGrid.cpp -o /tmp/test_map_hd_grid
/tmp/test_map_hd_grid
# DDS_FILE: a DDS made by tools/map_dds.cpp to read too (romfs/map-lowres.dds or a chunk of map-hd/).
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_dds.cpp source/Dds.cpp -o /tmp/test_dds
/tmp/test_dds ${DDS_FILE:+"$DDS_FILE"}
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_icon_scale.cpp source/IconScale.cpp -o /tmp/test_icon_scale
/tmp/test_icon_scale
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_hero_path.cpp source/HeroPath.cpp -o /tmp/test_hero_path
# HERO_PATH_DIR: the tracker folder of a real save, to check too (never in the repository).
/tmp/test_hero_path ${HERO_PATH_DIR:+"$HERO_PATH_DIR"}
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_image_override.cpp source/ImageOverride.cpp -o /tmp/test_image_override
/tmp/test_image_override
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_shrine_flags.cpp source/ShrineFlags.cpp -o /tmp/test_shrine_flags
/tmp/test_shrine_flags
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource -Itests tests/test_live_game_data.cpp source/LiveGameData.cpp -o /tmp/test_live_game_data
/tmp/test_live_game_data
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource -Itests tests/test_live_extras.cpp source/LiveGameData.cpp -o /tmp/test_live_extras
/tmp/test_live_extras
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource -Itests tests/test_map_marks.cpp source/MapMarks.cpp source/LiveGameData.cpp -o /tmp/test_map_marks
/tmp/test_map_marks
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_blood_moon.cpp source/BloodMoon.cpp source/ShrineFlags.cpp -o /tmp/test_blood_moon
/tmp/test_blood_moon
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_chests.cpp source/ChestData.cpp source/ChestOwnedData.cpp -o /tmp/test_chests
/tmp/test_chests
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_shrine_chests.cpp source/ShrineChests.cpp source/ShrineChestData.cpp -o /tmp/test_shrine_chests
/tmp/test_shrine_chests
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_location_state.cpp source/LocationState.cpp source/ShrineFlags.cpp -o /tmp/test_location_state
/tmp/test_location_state
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_walls.cpp source/WallData.cpp -o /tmp/test_walls
/tmp/test_walls
# QUESTS_FILE: a quests.txt made by tools/gen_quests.py (from the game's files, never in the repository).
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_quest_map.cpp source/QuestMap.cpp source/ProgressData.cpp -o /tmp/test_quest_map
/tmp/test_quest_map
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_progress.cpp source/Progress.cpp source/ProgressData.cpp source/ShrineFlags.cpp -o /tmp/test_progress
/tmp/test_progress
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_regions.cpp source/Regions.cpp source/RegionData.cpp source/ShrineFlags.cpp -o /tmp/test_regions
/tmp/test_regions
python3 tests/test_data_mapping.py
# The hidden level of the cooking app (EXP tab): the most kills each enemy can count, with a made-up save.
"$compiler" -std=c++14 -Wall -Wextra -pedantic -ffp-contract=off "${sanitizers[@]}" -Icompendium/source compendium/tests/test_level.cpp compendium/source/Level.cpp compendium/source/CookData.cpp compendium/source/SaveFile.cpp -o /tmp/test_level
/tmp/test_level
# Cooking app: needs cook-data.txt, made from the game's files (compendium/tools/gen_cook_data.py), so it is skipped
# without COOK_DATA. COOK_CASES: dishes read from the game's screen (compendium/tests/savage13_cases.py).
"$compiler" -std=c++14 -Wall -Wextra -pedantic -ffp-contract=off "${sanitizers[@]}" -Icompendium/source compendium/tests/test_cook.cpp compendium/source/Cook.cpp compendium/source/CookData.cpp compendium/source/SaveFile.cpp compendium/source/Search.cpp compendium/source/Sale.cpp compendium/source/Level.cpp -o /tmp/test_cook
/tmp/test_cook ${COOK_DATA:+"$COOK_DATA"} ${COOK_CASES:+"$COOK_CASES"}
"$compiler" -std=c++14 -Wall -Wextra -pedantic -ffp-contract=off "${sanitizers[@]}" -Icompendium/source -Isource -Itests compendium/tests/test_live_save.cpp compendium/source/LiveSave.cpp compendium/source/SaveFile.cpp compendium/source/CookData.cpp source/LiveGameData.cpp -o /tmp/test_live_save
# LIVE_SAVE: a real game_data.sav to check too (never in the repository).
/tmp/test_live_save ${LIVE_SAVE:+"$LIVE_SAVE"}
