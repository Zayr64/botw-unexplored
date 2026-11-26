#include "ProgressPanel.h"

#include <algorithm>
#include <string>

#include "BloodMoon.h"
#include "Data.h"
#include "Graphics/Font.h"
#include "Graphics/ShapeBatch.h"
#include "Localization.h"
#include "LocationState.h"
#include "Map.h"
#include "MapLocation.h"
#include "Progress.h"
#include "SavefileIO.h"
#include "ShrineFlags.h"
#include "Ui.h"

namespace
{
    using TextId = Localization::Text;
    using Progress::Counter;

    constexpr float RowStep = 27.0f;
    constexpr float HeaderStep = 28.0f;
    constexpr float SectionGap = 10.0f;
    constexpr float LabelScale = 0.36f;
    constexpr float CapHeight = 34.4f * LabelScale; // height of a capital letter at that scale

    // The rows scroll when they do not fit above the bottom bar; a row outside the panel is not drawn (nothing clips).
    struct Window
    {
        float scroll = 0.0f;  // how far the rows are moved up
        float content = 0.0f; // height of all the rows (last frame)
        float room = 0.0f;    // height they have
    };
    Window s_Windows[2]; // Progress, Collection
    float s_ClipTop = 10000.0f;
    float s_ClipBottom = -10000.0f;

    bool Visible(float top, float height)
    {
        return top <= s_ClipTop + 0.5f && top - height >= s_ClipBottom - 0.5f;
    }

    float Begin(Window& window, float top)
    {
        s_ClipTop = top;
        s_ClipBottom = Ui::Bottom() + Ui::FooterHeight + 8.0f;
        window.room = s_ClipTop - s_ClipBottom;
        return top + window.scroll;
    }

    void End(Window& window, float top)
    {
        window.content = s_ClipTop + window.scroll - top;
        const float most = std::max(0.0f, window.content - window.room);
        window.scroll = std::min(window.scroll, most);
        if (most > 0.0f)
        {
            // Where the rows are: a thin bar on the right edge of the panel.
            const float x = Ui::Left() + Ui::PanelWidth - 10.0f;
            Ui::Shapes().AddRoundedRect(x, s_ClipBottom, x + 4.0f, s_ClipTop, 2.0f, Ui::Colors::RowOff);
            const float thumb = std::max(24.0f, window.room * window.room / window.content);
            const float thumbTop = s_ClipTop - (window.room - thumb) * window.scroll / most;
            Ui::Shapes().AddRoundedRect(x, thumbTop - thumb, x + 4.0f, thumbTop, 2.0f, glm::vec4(Ui::Colors::Accent, 1.0f));
        }
        s_ClipTop = 10000.0f;
        s_ClipBottom = -10000.0f;
    }

    // A paragraph that is drawn only when all of it is in the panel. Returns the top of what comes next.
    float NoteParagraph(float top, const std::string& text, float scale, const glm::vec3& color)
    {
        const float height = Ui::LineCount(text, scale, Ui::PanelInner()) * 50.0f * scale;
        if (!Visible(top, height))
            return top - height;
        return Ui::Paragraph(top, text, scale, color);
    }

    float Header(float top, TextId title)
    {
        if (!Visible(top, HeaderStep))
            return top - HeaderStep;
        const float left = Ui::PanelLeft();
        Ui::Text(Localization::Get(title), glm::vec2(left, top - 18.0f), 0.30f, Ui::Colors::Accent);
        Ui::Shapes().AddRect(left, top - HeaderStep + 3.0f, left + Ui::PanelInner(), top - HeaderStep + 4.0f, Ui::Colors::Hairline);
        return top - HeaderStep;
    }

    float Row(float top, TextId label, const Counter& counter)
    {
        if (!Visible(top, RowStep))
            return top - RowStep;
        const float left = Ui::PanelLeft();
        const float right = left + Ui::PanelInner();
        const float baseline = top - (RowStep + CapHeight) * 0.5f;
        const bool complete = counter.total > 0 && counter.done >= counter.total;

        const std::string& text = Localization::Get(label);
        Ui::Text(text, glm::vec2(left, baseline), LabelScale, Ui::Colors::Text);
        if (counter.open > 0)
            Ui::Text("+" + std::to_string(counter.open) + " " + Localization::Get(Localization::Text::QuestsOpen),
                     glm::vec2(left + Ui::TextWidth(text, LabelScale) + 8.0f, baseline), 0.30f, Ui::Colors::TextDim);
        Ui::Text(std::to_string(counter.done) + "/" + std::to_string(counter.total), glm::vec2(right, baseline),
                 LabelScale, complete ? Ui::Colors::Good : Ui::Colors::Text, ALIGN_RIGHT);
        return top - RowStep;
    }

    // A row with a text on the right instead of a count.
    float TextRow(float top, TextId label, const std::string& value, const glm::vec3& color)
    {
        if (!Visible(top, RowStep))
            return top - RowStep;
        const float left = Ui::PanelLeft();
        const float right = left + Ui::PanelInner();
        const float baseline = top - (RowStep + CapHeight) * 0.5f;
        Ui::Text(Localization::Get(label), glm::vec2(left, baseline), LabelScale, Ui::Colors::Text);
        Ui::Text(value, glm::vec2(right, baseline), LabelScale, color, ALIGN_RIGHT);
        return top - RowStep;
    }

    // "2 h 28 min" or "35 min".
    std::string Duration(int minutes)
    {
        if (minutes < 60)
            return std::to_string(minutes) + " " + Localization::Get(Localization::Text::MinutesOfPlay);
        return std::to_string(minutes / 60) + " h " + std::to_string(minutes % 60) + " " +
               Localization::Get(Localization::Text::MinutesOfPlay);
    }

    // When the next Blood Moon comes, from the save or from the running game. Returns the new top.
    float BloodMoonRow(float top)
    {
        const BloodMoon::Estimate next = BloodMoon::Next(SavefileIO::CurrentState.bloodMoon);
        switch (next.status)
        {
        case BloodMoon::Status::Unknown:
            return top;
        case BloodMoon::Status::NotStarted:
            return TextRow(top, Localization::Text::BloodMoon, Localization::Get(Localization::Text::BloodMoonNotYet),
                           Ui::Colors::TextDim);
        case BloodMoon::Status::Tonight:
            top = TextRow(top, Localization::Text::BloodMoon,
                          Localization::Get(Localization::Text::BloodMoonTonight) + " " + Duration(next.minutesOfPlay),
                          Ui::Colors::Warn);
            break;
        case BloodMoon::Status::Counting:
            top = TextRow(top, Localization::Text::BloodMoon,
                          Localization::Get(Localization::Text::BloodMoonIn) + " " + Duration(next.minutesOfPlay),
                          Ui::Colors::Text);
            break;
        }
        return NoteParagraph(top - 2.0f, Localization::Get(Localization::Text::BloodMoonNote), 0.26f, Ui::Colors::TextDim);
    }

    // What this game has: the Expansion Pass and Master Mode add entries and pieces.
    Progress::Game Game()
    {
        Progress::Game game;
        game.expansionPass = SavefileIO::HasExpansionPass();
        game.masterMode = SavefileIO::MasterModeFileLoaded;
        return game;
    }

    Counter Towers()
    {
        Counter counter;
        const LocationState::Entry* table = LocationState::Table();
        for (int i = 0; i < LocationState::Count; ++i)
        {
            if (table[i].kind != LocationState::Kind::Tower)
                continue;
            ++counter.total;
            counter.done += SavefileIO::IsLocationStateDone(table[i].locationHash) ? 1 : 0;
        }
        return counter;
    }

    // The four fountains; the spring of the horse god has a flag of the same family and is not one of them.
    Counter GreatFairies()
    {
        static const uint32_t spring = ShrineFlags::Crc32("Location_MaronSpring");
        Counter counter;
        const LocationState::Entry* table = LocationState::Table();
        for (int i = 0; i < LocationState::Count; ++i)
        {
            if (table[i].kind != LocationState::Kind::GreatFairy || table[i].locationHash == spring)
                continue;
            ++counter.total;
            counter.done += SavefileIO::IsLocationStateDone(table[i].locationHash) ? 1 : 0;
        }
        return counter;
    }

    Counter ShrinesCompleted()
    {
        Counter counter;
        counter.total = Data::ShrineCount;
        for (int i = 0; i < Data::ShrineCount; ++i)
            counter.done += SavefileIO::IsShrineCleared(Data::Shrines[i].hash) ? 1 : 0;
        return counter;
    }

    // The chests inside the 120 shrines. The game's own flag tells when a shrine has none left.
    Counter ShrineChests()
    {
        Counter counter;
        for (int i = 0; i < Data::ShrineCount; ++i)
        {
            int opened = 0, total = 0;
            if (!SavefileIO::GetShrineChests(Data::Shrines[i].hash, opened, total))
                continue;
            counter.total += total;
            counter.done += SavefileIO::HasAllShrineChests(Data::Shrines[i].hash) ? total : opened;
        }
        return counter;
    }

    // The chests of the world map with nothing rare inside, which the map does not show; those of this game
    // (mode and Expansion Pass).
    Counter CommonChests()
    {
        Counter counter;
        const bool expansionPass = SavefileIO::HasExpansionPass();
        for (int i = 0; i < Data::ChestsCount; ++i)
        {
            const Data::Chest& chest = Data::Chests[i];
            if (((chest.flags & Data::ChestMasterModeOnly) && !Map::m_LoadMasterMode) ||
                ((chest.flags & Data::ChestDlc) && !expansionPass))
                continue;
            ++counter.total;
            counter.done += SavefileIO::IsChestOpened(chest.hash) ? 1 : 0;
        }
        return counter;
    }

    // The named places of the map: the third part of the map completion, with the Koroks and the shrines.
    Counter Locations()
    {
        Counter counter;
        counter.total = Data::LocationsCount;
        if (Map::m_Locations != nullptr)
            for (int i = 0; i < Data::LocationsCount; ++i)
                counter.done += Map::m_Locations[i].m_Found ? 1 : 0;
        return counter;
    }
}

void ProgressPanel::Render(float top)
{
    const Progress::IsSet isSet = &SavefileIO::IsProgressFlagSet;
    Window& window = s_Windows[0];
    top = Begin(window, top);

    top = Header(top, Localization::Text::SectionQuests);
    top = Row(top, Localization::Text::MainQuests, Progress::Quests(Data::QuestKind::Main, false, isSet));
    top = Row(top, Localization::Text::ShrineQuests, Progress::Quests(Data::QuestKind::Shrine, false, isSet));
    top = Row(top, Localization::Text::SideQuests, Progress::Quests(Data::QuestKind::Side, false, isSet));
    if (SavefileIO::HasExpansionPass())
    {
        top = Row(top, Localization::Text::MainQuestsDlc, Progress::Quests(Data::QuestKind::Main, true, isSet));
        top = Row(top, Localization::Text::SideQuestsDlc, Progress::Quests(Data::QuestKind::Side, true, isSet));
    }

    top = Header(top - SectionGap, Localization::Text::SectionWorld);
    top = Row(top, Localization::Text::Towers, Towers());
    top = Row(top, Localization::Text::ShrinesCompleted, ShrinesCompleted());
    top = Row(top, Localization::Text::ShrineChests, ShrineChests());
    top = Row(top, Localization::Text::GreatFairies, GreatFairies());
    top = Row(top, Localization::Text::DivineBeasts, Progress::DivineBeasts(isSet));
    top = Row(top, Localization::Text::DivineBeastChests, Progress::DivineBeastChests(isSet));
    top = Row(top, Localization::Text::CommonChests, CommonChests());
    // Nothing is said of the memories before the album has the pictures that lead to them.
    const Counter memories = Progress::Memories(isSet);
    if (memories.total > 0)
        top = Row(top, Localization::Text::Memories, memories);
    top = Row(top, Localization::Text::Locations, Locations());
    top = BloodMoonRow(top);

    End(window, top);
    Ui::FlushShapes();
}

void ProgressPanel::RenderCollection(float top)
{
    const Progress::IsSet isSet = &SavefileIO::IsProgressFlagSet;
    const Progress::Game game = Game();
    Window& window = s_Windows[1];
    top = Begin(window, top);

    top = Header(top, Localization::Text::Compendium);
    top = Row(top, Localization::Text::CompendiumAll, Progress::Compendium(isSet, game));
    for (int category = 0; category < static_cast<int>(Data::CompendiumCategory::Count); ++category)
        top = Row(top, static_cast<TextId>(static_cast<int>(Localization::Text::Creatures) + category),
                  Progress::Compendium(static_cast<Data::CompendiumCategory>(category), isSet, game));

    top = Header(top - SectionGap, Localization::Text::SectionArmor);
    top = Row(top, Localization::Text::ArmorPieces, Progress::Armor(isSet, game));
    top = NoteParagraph(top - 4.0f, Localization::Get(Localization::Text::ArmorNote), 0.28f, Ui::Colors::TextDim);

    End(window, top);
    Ui::FlushShapes();
}

void ProgressPanel::Scroll(bool collection, int rows)
{
    Window& window = s_Windows[collection ? 1 : 0];
    window.scroll = std::max(0.0f, window.scroll + rows * RowStep); // the end is held when the rows are drawn
}

bool ProgressPanel::Scrolls(bool collection)
{
    const Window& window = s_Windows[collection ? 1 : 0];
    return window.content > window.room + 0.5f;
}
