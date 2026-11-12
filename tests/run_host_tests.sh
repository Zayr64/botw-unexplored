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
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_image_override.cpp source/ImageOverride.cpp -o /tmp/test_image_override
/tmp/test_image_override
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_shrine_flags.cpp source/ShrineFlags.cpp -o /tmp/test_shrine_flags
/tmp/test_shrine_flags
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource -Itests tests/test_live_game_data.cpp source/LiveGameData.cpp -o /tmp/test_live_game_data
/tmp/test_live_game_data
"$compiler" -std=c++14 -Wall -Wextra -pedantic "${sanitizers[@]}" -Isource tests/test_chests.cpp source/ChestData.cpp -o /tmp/test_chests
/tmp/test_chests
python3 tests/test_data_mapping.py
