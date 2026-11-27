// BotW Compendium: the best dishes from what is in the pouch, what to hold back for armor and quests, and what to
// sell.
//
// Four tabs (L/R): Dishes, Reserve, Sell, EXP. Everything is computed once from the save; X and Y change what counts
// as free (or what the sale may use) and compute again. Nothing is ever written to the game's save.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include <switch.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>

#include "Graphics/Font.h"
#ifndef BOTW_COMPANION
#include "Graphics/InitGL.h"
#endif
#include "Graphics/ShapeBatch.h"
#include "Log.h"
#include "Measure.h"
#include "SwitchStyle.h"

#include "Cook.h"
#include "CookData.h"
#include "CompendiumSection.h"
#include "Icons.h"
#include "Reserve.h"
#include "Sale.h"
#include "SaveFile.h"
#include "SaveSource.h"
#include "Search.h"
#include "Level.h"

using namespace Compendium;

namespace
{
    // Everything is drawn on a canvas of ScreenW x ScreenH stretched to the 1280x720 screen: with a zoom above 1 the
    // canvas is smaller, so text, icons and rows grow together, and each tab lays itself out on what is left.
    // COMPENDIUM_ZOOM 1 is the size up to 0.1.10; 1.25 chosen on 25/11 for the console's own screen.
#ifndef COMPENDIUM_ZOOM
#define COMPENDIUM_ZOOM 1.25f
#endif
    const float Zoom = COMPENDIUM_ZOOM;
    const float ScreenW = 1280.0f / Zoom;
    const float ScreenH = 720.0f / Zoom;
    const float HeaderH = 56.0f;
    const float FooterH = 46.0f;
    // Text scales are fractions of the font's 44 px: 0.36 is 16 px on the canvas, the smallest text (times the zoom
    // on the screen).
    const float SmallText = 0.36f;

    // 0.1.15: the colours BotW Unexplored uses too (SwitchStyle); here the bars are opaque, there is no map under them.
    const glm::vec4 Background(0.035f, 0.05f, 0.06f, 1.0f);
    const glm::vec4 PanelColor(0.055f, 0.08f, 0.095f, 1.0f);
    const glm::vec4 BarColor(SwitchStyle::BarRgb, 1.0f);
    const glm::vec4 Hairline(SwitchStyle::Hairline);
    const glm::vec4 RowAlt(1.0f, 1.0f, 1.0f, 0.03f);
    const glm::vec4 ChipColor(1.0f, 1.0f, 1.0f, 0.07f); // a chip on the bar
    const glm::vec3 TextColor(SwitchStyle::TextColor);
    const glm::vec3 TextDim(SwitchStyle::TextDim);
    const glm::vec3 Accent(SwitchStyle::Accent);
    const glm::vec3 Gold(SwitchStyle::Gold);
    const glm::vec3 Good(SwitchStyle::Good);
    const glm::vec3 Warn(SwitchStyle::Warn);
    // The buttons' symbols in the footer, as in BotW Unexplored's (the caps it drew up to 0.1.14 without the font)
    const glm::vec3 CapColor(0.93f, 0.95f, 0.96f);
    // The effect icons come coloured from gen_icons.py (the colours of the game's icons below Link's hearts) and are
    // drawn as they are. The heart is a grey shape tinted here: red, or the yellow of extra hearts (#F0E000). The
    // clock and the time: the green of a dish's card in the pause menu (#21DA4F, Layout/Common.sblarc).
    const glm::vec4 NoTint(1.0f, 1.0f, 1.0f, 1.0f);
    const glm::vec4 HeartRed(0.93f, 0.25f, 0.28f, 1.0f);
    const glm::vec4 HeartYellow(0.94f, 0.88f, 0.0f, 1.0f);
    const glm::vec3 TimeGreen(0.129f, 0.855f, 0.31f);

    const char* DataPath = "sdmc:/switch/botw-compendium/cook-data.txt";

    Font* g_font = nullptr;
    ShapeBatch* g_shapes = nullptr;
    unsigned int g_frame = 0; // the pulse of the selection's frame
    // BotW Companion: the section on the screen (0 the map, 1 to 4 the tabs); -1 in the BotW Compendium. On the map the
    // header shows where the map's data comes from.
    int g_section = -1;
    std::string g_mapSource;
    glm::vec3 g_mapDot(0.0f);
    bool g_mapHasDot = false;
    glm::mat4 g_projection(1.0f);
    glm::mat4 g_identity(1.0f);

    // Screen coordinates from the top left, as on the screenshots.
    void Rect(float x, float top, float w, float h, const glm::vec4& color)
    {
        g_shapes->AddRect(x, ScreenH - top - h, x + w, ScreenH - top, color);
    }

    void RoundRect(float x, float top, float w, float h, float r, const glm::vec4& color)
    {
        g_shapes->AddRoundedRect(x, ScreenH - top - h, x + w, ScreenH - top, r, color);
    }

    // `baseline`: distance of the text's baseline from the top of the screen. Returns the x after the text.
    float Text(const std::string& s, float x, float baseline, float scale, const glm::vec3& color,
               int align = ALIGN_LEFT, float maxWidth = -1.0f)
    {
        g_font->AddTextToBatch(s, glm::vec2(x, ScreenH - baseline), scale, color, align, maxWidth);
        return x + (align == ALIGN_LEFT ? g_font->MeasureText(s, scale) : 0.0f);
    }

    float Width(const std::string& s, float scale)
    {
        return g_font->MeasureText(s, scale);
    }

    // The selected row of a list, as in BotW Unexplored: the Switch's pulsing cyan frame, here inside the row (the
    // rows touch each other), and a light fill. `behind`: the colour under the row.
    void SelectedRow(float x, float top, float w, float h, float r, const glm::vec4& behind)
    {
        const float inset = 4.0f;
        const float left = x + inset, right = x + w - inset;
        const float bottom = ScreenH - top - h + inset, upper = ScreenH - top - inset;
        const float radius = std::max(2.0f, r - inset);
        SwitchStyle::SelectionFrame(*g_shapes, left, bottom, right, upper, radius, g_frame, glm::vec3(behind), inset, 2.0f);
        g_shapes->AddRoundedRect(left, bottom, right, upper, radius, SwitchStyle::RowSelected);
    }

    // Cuts a text to a width, with "..." at the end.
    std::string Fit(const std::string& s, float scale, float width)
    {
        if (Width(s, scale) <= width)
            return s;
        std::string t = s;
        while (!t.empty() && Width(t + "...", scale) > width)
            t.pop_back();
        return t + "...";
    }

    // The words of a text on lines no wider than `width`.
    std::vector<std::string> Wrap(const std::string& s, float scale, float width)
    {
        std::vector<std::string> lines;
        std::string line;
        size_t start = 0;
        while (start < s.size())
        {
            size_t end = s.find(' ', start);
            if (end == std::string::npos)
                end = s.size();
            const std::string word = s.substr(start, end - start);
            const std::string longer = line.empty() ? word : line + " " + word;
            if (!line.empty() && Width(longer, scale) > width)
            {
                lines.push_back(line);
                line = word;
            }
            else
                line = longer;
            start = end + 1;
        }
        if (!line.empty())
            lines.push_back(line);
        return lines;
    }

    std::string Number(int32_t n)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", n < 0 ? -n : n);
        std::string digits = buf;
        std::string out;
        for (size_t i = 0; i < digits.size(); i++)
        {
            if (i > 0 && (digits.size() - i) % 3 == 0)
                out += ',';
            out += digits[i];
        }
        return n < 0 ? "-" + out : out;
    }

    std::string Clock(int32_t seconds)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", seconds / 60, seconds % 60);
        return buf;
    }

    std::string Hearts(int32_t quarters)
    {
        char buf[16];
        if (quarters % 4 == 0)
            snprintf(buf, sizeof(buf), "%d", quarters / 4);
        else
            snprintf(buf, sizeof(buf), "%.2f", quarters / 4.0);
        return buf;
    }

    std::string Wheels(int32_t units, int perWheel)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(units) / perWheel);
        return buf;
    }

    // The icon of an effect, in ui/, and its tint.
    const char* EffectIcon(Effect e)
    {
        switch (e)
        {
        case AttackUp: return "AttackUp";
        case DefenseUp: return "DefenseUp";
        case MovingSpeed: return "MovingSpeed";
        case Quietness: return "Quietness";
        case ResistHot: return "ResistHot";
        case ResistCold: return "ResistCold";
        case ResistElectric: return "ResistElectric";
        case Fireproof: return "Fireproof";
        case GutsRecover: return "GutsRecover";
        case ExGutsMaxUp: return "ExGutsMaxUp";
        default: return "heart";
        }
    }

    glm::vec4 EffectTint(Effect e)
    {
        switch (e)
        {
        case LifeMaxUp: return HeartYellow;
        case EffectNone: return HeartRed;
        default: return NoTint;
        }
    }

    enum class GoalKind
    {
        Effect,  // the strongest dish with the effect
        Hearts,  // the cheapest that heals all but one heart, and the cheapest yellow-heart dish
        Stamina, // the cheapest that refills every wheel
    };

    struct Goal
    {
        const char* label;
        const char* detail;
        Effect effect;
        GoalKind kind;
        SearchResult result;
        TargetResult target;
        TargetResult hearty;
    };

    struct MaterialRow
    {
        int index;        // in ReservePlan::materials
        std::string name; // every material by name, found or not (no more spoiler protection: 26/11)
    };

    struct LevelRow
    {
        int index; // in LevelData::flags
    };

    struct App
    {
        std::unique_ptr<CookData> cook;
        std::unique_ptr<ReserveData> reserve;
        SaveSource::Loaded loaded;
        SaveFile save;
        std::vector<PouchItem> pouch;
        int pouchCount = 0;
        int dishes = 0;
        int32_t maxHearts = 0;  // quarters
        int32_t maxStamina = 0; // 1000 = one wheel
        std::unique_ptr<ReservePlan> plan;
        std::unique_ptr<SalePlan> sale;
        std::vector<Goal> goals;
        std::vector<MaterialRow> rows;
        std::unique_ptr<LevelData> level;   // null when cook-data.txt has no level tables
        std::unique_ptr<LevelPlan> levelPlan;
        std::vector<LevelRow> levelRows;
        bool countHidden = true; // the other needs: the tiers for armor not owned, the Ancient Oven and quests
        bool wholePouch = false; // dishes from everything in the pouch, not only what is free
        bool sellPrized = false; // the sale also cooks the critters of yellow hearts and extra stamina
        int prizedFree = 0;      // how many of those critters are free
        int tab = 0;
        int cursor[4] = {0, 0, 0, 0};
        int scroll[4] = {0, 0, 0, 0};
        int visible[4] = {0, 0, 0, 0}; // rows the tab's list shows at once (0: no scrolling list), for Left/Right
        bool loading = true;           // the save is still being read
        bool computing = false;        // X or Y was pressed: the dishes are cooked again after this frame
        std::string error;
        int evaluated = 0;
        int dishPage = 0; // the Dishes tab with room for one dish: 0 the first (best), 1 the other (A)
        int gridColumns = 0; // EXP: the enemies are tiles, Up/Down a row of this many, Left/Right one
    };

    // Keeps the cursor in the list and the list's window on the cursor.
    void KeepInView(int& cursor, int& scroll, int rows, int visible)
    {
        cursor = std::max(0, std::min(cursor, rows - 1));
        if (cursor < scroll)
            scroll = cursor;
        if (cursor >= scroll + visible)
            scroll = cursor - visible + 1;
        scroll = std::max(0, std::min(scroll, rows - visible));
    }

    // Where the window is in a list longer than the screen: a bar on the right edge.
    void ScrollBar(float top, float height, int rows, int visible, int scroll, float right = ScreenW)
    {
        if (rows <= visible)
            return;
        const float x = right - 9, w = 5;
        RoundRect(x, top, w, height, 2.5f, glm::vec4(1.0f, 1.0f, 1.0f, 0.10f));
        const float thumb = std::max(28.0f, height * visible / rows);
        RoundRect(x, top + (height - thumb) * scroll / (rows - visible), w, thumb, 2.5f, glm::vec4(Accent, 0.95f));
    }

    int32_t HeartsTarget(const App& app)
    {
        // All hearts but one: the most a living Link can be missing, give or take a quarter.
        return std::max<int32_t>(4, app.maxHearts - 4);
    }

    int32_t StaminaTarget(const App& app)
    {
        return app.maxStamina > 0 ? app.maxStamina : 1000;
    }

    uint8_t TierMask(const App& app)
    {
        uint8_t mask = (1u << OwnedReachable) | (1u << OwnedNextFairy);
        if (app.countHidden)
            mask |= (1u << ArmorNotOwned) | (1u << Crafted) | (1u << Quests);
        return mask;
    }

    const char* NameOf(const App& app, uint32_t hash)
    {
        const Actor* a = app.cook->FindActor(hash);
        if (a && a->display[0])
            return a->display;
        const char* label = app.reserve->Label(hash);
        if (label[0])
            return label;
        return a ? a->name : "?";
    }

    // An enemy whose counter is at its limit: more kills add no points.
    bool LevelRowDone(const LevelData& d, const LevelPlan& p, int index)
    {
        return d.flags[index].max > 0 && p.flags[index].kills >= d.flags[index].max;
    }

    // The actor name, for the item's icon; "" when unknown.
    std::string ActorName(const App& app, uint32_t hash)
    {
        const Actor* a = app.cook->FindActor(hash);
        return a ? a->name : "";
    }

    // The picture of a dish. An elixir (Item_Cook_C_17) has one per effect in the game's pouch,
    // Item_Cook_C_17_<effect>; its own is a placeholder.
    std::string DishPicture(const App& app, const CookResult& r)
    {
        std::string actor = ActorName(app, r.actor);
        if (actor != "Item_Cook_C_17" || r.effect == EffectNone)
            return actor;
        if (r.effect == LifeMaxUp)
            return actor + "_LifeMaxUp";
        if (r.effect == MovingSpeed)
            return actor + "_AllSpeed";
        return actor + "_" + EffectIcon(r.effect);
    }

    struct Part
    {
        uint32_t item;
        int count;
    };

    // The ingredients of a pot, each once with its count, in pot order.
    std::vector<Part> Parts(const uint32_t* items, int count)
    {
        std::vector<Part> parts;
        for (int i = 0; i < count; i++)
        {
            bool seen = false;
            for (Part& p : parts)
            {
                if (p.item == items[i])
                {
                    p.count++;
                    seen = true;
                }
            }
            if (!seen)
                parts.push_back({items[i], 1});
        }
        return parts;
    }

    std::string Strength(const CookResult& r)
    {
        switch (r.effect)
        {
        case LifeMaxUp:
            return "+" + std::to_string(r.vitality / 4) + (r.vitality / 4 == 1 ? " yellow heart" : " yellow hearts");
        case GutsRecover: return Wheels(r.vitality, 1000) + " wheels";
        case ExGutsMaxUp: return "+" + Wheels(r.vitality, 5) + " wheels";
        case EffectNone: return Hearts(r.life) + " hearts";
        default: return "Lv " + std::to_string(r.vitality) + "  " + Clock(r.time);
        }
    }

    void Compute(App& app)
    {
        const uint8_t mask = TierMask(app);
        std::vector<StockItem> stock;
        std::vector<StockItem> freeStock;
        for (int i = 0; i < app.plan->materialCount; i++)
        {
            const MaterialPlan& m = app.plan->materials[i];
            StockItem s;
            s.hash = m.item;
            s.count = m.Free(mask);
            if (s.count > 0)
                freeStock.push_back(s);
            s.count = app.wholePouch ? m.have : m.Free(mask);
            if (s.count > 0)
                stock.push_back(s);
        }
        const int n = static_cast<int>(stock.size());
        app.prizedFree = 0;
        for (const StockItem& s : freeStock)
        {
            const Actor* a = app.cook->FindActor(s.hash);
            if (a && IsPrizedCritter(*a))
                app.prizedFree += s.count;
        }
        app.evaluated = 0;
        for (Goal& g : app.goals)
        {
            g.result = SearchResult();
            g.target = TargetResult();
            g.hearty = TargetResult();
            switch (g.kind)
            {
            case GoalKind::Effect:
                // Among dishes as strong and as long, the cheaper wins: no costly food just for hearts.
                g.result = FindBestForEffect(*app.cook, stock.data(), n, g.effect, Ranking::PriceThenHearts);
                app.evaluated += g.result.evaluated;
                break;
            case GoalKind::Hearts:
                g.target = FindCheapestReaching(*app.cook, stock.data(), n, EffectNone, HeartsTarget(app));
                g.hearty = FindCheapestReaching(*app.cook, stock.data(), n, LifeMaxUp, 4);
                app.evaluated += g.target.evaluated + g.hearty.evaluated;
                break;
            case GoalKind::Stamina:
                g.target = FindCheapestReaching(*app.cook, stock.data(), n, GutsRecover, StaminaTarget(app));
                app.evaluated += g.target.evaluated;
                break;
            }
        }
        PlanSale(*app.cook, freeStock.data(), static_cast<int>(freeStock.size()), std::max(0, MaxSalePots - app.dishes),
                 *app.sale, !app.sellPrized);
    }

    bool Load(App& app)
    {
        FILE* f = fopen(DataPath, "rb");
        if (!f)
        {
            app.error = "cook-data.txt is missing in switch/botw-compendium/ (it is made on the PC from the game's files)";
            return false;
        }
        std::string text;
        char buffer[16384];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
            text.append(buffer, n);
        fclose(f);

        Measure::Step("cook-data read");
        app.cook.reset(new CookData());
        app.reserve.reset(new ReserveData());
        char error[160] = {};
        if (!app.cook->Load(text.data(), text.size(), error, sizeof(error)) ||
            !app.reserve->Load(text.data(), text.size(), error, sizeof(error)))
        {
            app.error = std::string("cook-data.txt: ") + error;
            return false;
        }

        Measure::Step("cook-data parsed");
        app.loaded = SaveSource::Load();
        if (app.loaded.kind == SaveSource::Kind::None || !app.save.Load(app.loaded.bytes.data(), app.loaded.bytes.size()))
        {
            app.error = app.loaded.message.empty() ? "The save could not be read" : app.loaded.message;
            return false;
        }
        app.pouch.resize(PouchSize);
        app.pouchCount = app.save.ReadPouch(app.pouch.data(), PouchSize);
        for (int i = 0; i < app.pouchCount; i++)
            app.dishes += strncmp(app.pouch[i].name, "Item_Cook_", 10) == 0;
        app.save.GetS32("MaxHartValue", app.maxHearts);
        float stamina = 0.0f;
        if (app.save.GetF32("StaminaMax", stamina))
            app.maxStamina = static_cast<int32_t>(stamina);
        Log("Max hearts (quarters):", app.maxHearts);
        Log("Max stamina:", app.maxStamina);

        Measure::Step("save or game read");
        app.plan.reset(new ReservePlan());
        PlanReserve(*app.cook, *app.reserve, app.save, app.pouch.data(), app.pouchCount, *app.plan);
        Measure::Step("reserve planned");
        {
            std::unique_ptr<LevelData> level(new LevelData());
            char levelError[160] = {};
            if (level->Load(text.data(), text.size(), levelError, sizeof(levelError)))
            {
                app.level = std::move(level);
                app.levelPlan.reset(new LevelPlan());
                PlanLevel(*app.level, app.save, *app.levelPlan);
                for (int i = 0; i < app.level->flagCount; i++)
                    app.levelRows.push_back({i});
                // By the points each enemy gives, the fewest first (Ganon last); the enemies whose counter is full
                // (they add nothing more) after all the others, in the same order (the user's order, 26/11).
                const LevelPlan& lp = *app.levelPlan;
                const LevelData& ld = *app.level;
                std::stable_sort(app.levelRows.begin(), app.levelRows.end(), [&](const LevelRow& a, const LevelRow& b) {
                    const bool fa = LevelRowDone(ld, lp, a.index), fb = LevelRowDone(ld, lp, b.index);
                    if (fa != fb)
                        return fb;
                    if (ld.flags[a.index].point != ld.flags[b.index].point)
                        return ld.flags[a.index].point < ld.flags[b.index].point;
                    return std::strcmp(NameOf(app, Crc32(ld.flags[a.index].actor)),
                                       NameOf(app, Crc32(ld.flags[b.index].actor))) < 0;
                });
                Log("Level points:", static_cast<int>(lp.points));
            }
            else
                Log("No level tables:", std::string(levelError));
        }
        app.sale.reset(new SalePlan());

        app.goals = {
            {"Mighty", "attack up", AttackUp, GoalKind::Effect, {}, {}, {}},
            {"Tough", "defense up", DefenseUp, GoalKind::Effect, {}, {}, {}},
            {"Hasty", "speed up", MovingSpeed, GoalKind::Effect, {}, {}, {}},
            {"Sneaky", "stealth up", Quietness, GoalKind::Effect, {}, {}, {}},
            {"Chilly", "heat resistance", ResistHot, GoalKind::Effect, {}, {}, {}},
            {"Spicy", "cold resistance", ResistCold, GoalKind::Effect, {}, {}, {}},
            {"Electro", "shock resistance", ResistElectric, GoalKind::Effect, {}, {}, {}},
            {"Fireproof", "flame guard", Fireproof, GoalKind::Effect, {}, {}, {}},
            {"Hearty", "yellow hearts", LifeMaxUp, GoalKind::Effect, {}, {}, {}},
            {"Enduring", "extra stamina", ExGutsMaxUp, GoalKind::Effect, {}, {}, {}},
            {"Energizing", "refill your stamina", GutsRecover, GoalKind::Stamina, {}, {}, {}},
            {"Full heal", "heal your hearts", EffectNone, GoalKind::Hearts, {}, {}, {}},
        };

        // Reserve rows: every material by name, alphabetically.
        for (int i = 0; i < app.plan->materialCount; i++)
        {
            const MaterialPlan& m = app.plan->materials[i];
            if (m.Needed(AllTiers) == 0)
                continue;
            MaterialRow row;
            row.index = i;
            row.name = NameOf(app, m.item);
            app.rows.push_back(row);
        }
        std::stable_sort(app.rows.begin(), app.rows.end(),
                         [](const MaterialRow& a, const MaterialRow& b) { return a.name < b.name; });

        Measure::Step("tables");
        Compute(app);
        Log("Computed dishes, combinations:", app.evaluated);
        Measure::Step("cooking (" + std::to_string(app.evaluated) + " combinations)");
        return true;
    }

    // ---- Drawing ----

    int RowCount(const App& app);

    // Where the numbers come from, in BotW Unexplored's words (SwitchStyle): with BotW open the save cannot be read,
    // so a copy is shown, or the game's memory.
    std::string SourceText(const App& app, bool brief, glm::vec3& dot, bool& hasDot)
    {
        hasDot = false;
        if (app.loaded.kind == SaveSource::Kind::None)
            return std::string();
        const SwitchStyle::Source source = app.loaded.kind == SaveSource::Kind::Live ? SwitchStyle::Source::Live
                                         : app.loaded.kind == SaveSource::Kind::Game ? SwitchStyle::Source::Save
                                                                                     : SwitchStyle::Source::Copy;
        return SwitchStyle::SourceText(source, app.loaded.saveTime, brief, dot, hasDot);
    }

    void Header(const App& app)
    {
        Rect(0, 0, ScreenW, HeaderH, BarColor);
        Rect(0, HeaderH, ScreenW, 1, Hairline);
        // Each tab with the icon of the game's pause menu for it (on the SD, from the dump; without it, the title
        // alone): the pot of the dishes, the apple of the materials, a rupee, a skull.
        const bool companion = g_section >= 0;
        const char* compendiumTabs[] = {"Dishes", "Reserve", "Sell", "EXP"};
        const char* compendiumIcons[] = {"tab-dishes", "tab-reserve", "tab-sell", "tab-exp"};
        const char* companionTabs[] = {"Map", "Dishes", "Reserve", "Sell", "EXP"};
        const char* companionIcons[] = {"tab-map", "tab-dishes", "tab-reserve", "tab-sell", "tab-exp"};
        const char* const* tabs = companion ? companionTabs : compendiumTabs;
        const char* const* tabIcons = companion ? companionIcons : compendiumIcons;
        const int tabCount = companion ? 5 : 4;
        const int activeTab = companion ? g_section : app.tab;
        const float tabScale = 0.46f, tabBaseline = 36.0f, icon = 30.0f, iconGap = 5.0f, pad = 12.0f, tabGap = 4.0f;
        const float cap = SwitchStyle::CapHeight(*g_font, tabScale);
        auto contentW = [&](int i) {
            return Width(tabs[i], tabScale) + (Icons::HasUi(tabIcons[i]) ? icon + iconGap : 0.0f);
        };
        float tabsW = 0.0f;
        for (int i = 0; i < tabCount; i++)
            tabsW += contentW(i) + 2 * pad + tabGap;
        // On the right the rupees, then where the data comes from; the long form when there is room, else the date.
        const std::string rupees = app.plan ? Number(app.plan->rupees) : std::string();
        const float rupeesW = app.plan ? Width(rupees, 0.46f) + 40 : 0.0f;
        glm::vec3 dot;
        bool hasDot = false;
        std::string source = SourceText(app, false, dot, hasDot);
        if (companion && g_section == 0)
        {
            source = g_mapSource;
            dot = g_mapDot;
            hasDot = g_mapHasDot;
        }
        // The title gets shorter when the zoom leaves no room for it; the companion has the sections in its place.
        std::string title = companion ? std::string() : std::string("BotW Compendium");
        auto room = [&]() {
            return ScreenW - (24 + (title.empty() ? 0.0f : Width(title, 0.62f) + 16) + tabsW + 12 + rupeesW + 16);
        };
        if (!source.empty() && !title.empty() && SwitchStyle::ChipWidth(*g_font, source, hasDot) + 16 > room())
            title = "Compendium";
        if (!source.empty() && SwitchStyle::ChipWidth(*g_font, source, hasDot) + 16 > room() && !(companion && g_section == 0))
            source = SourceText(app, true, dot, hasDot);
        if (!source.empty() && SwitchStyle::ChipWidth(*g_font, source, hasDot) + 16 > room())
            source.clear();
        float x = 24;
        if (!title.empty())
        {
            Text(title, 24, 38, 0.62f, Gold);
            x += Width(title, 0.62f) + 16;
        }
        else
            x = 12;
        for (int i = 0; i < tabCount; i++)
        {
            const float w = contentW(i) + 2 * pad;
            const bool active = i == activeTab;
            const float left = x + pad, right = x + w - pad;
            if (active)
                SwitchStyle::TabMark(*g_shapes, left, right, ScreenH - tabBaseline);
            float tx = left;
            if (Icons::HasUi(tabIcons[i]))
            {
                const glm::vec3 c = SwitchStyle::TabTitleColor(active);
                Icons::Ui(tabIcons[i], tx, tabBaseline - cap * 0.5f - icon * 0.5f, icon, glm::vec4(c, 1.0f));
                tx += icon + iconGap;
            }
            Text(tabs[i], tx, tabBaseline, tabScale, SwitchStyle::TabTitleColor(active));
            x += w + tabGap;
        }
        float right = ScreenW - 16;
        if (!source.empty())
            right -= SwitchStyle::Chip(*g_shapes, *g_font, source, right, ScreenH - (HeaderH - SwitchStyle::ChipHeight) / 2,
                                       true, ChipColor, hasDot ? &dot : nullptr) + 16;
        if (app.plan)
        {
            const float w = Width(rupees, 0.46f);
            Text(rupees, right, 37, 0.46f, TextColor, ALIGN_RIGHT);
            Icons::Ui("rupee", right - w - 40, 12, 30);
        }
    }

    // What a material is kept for beyond the armor the player has: armor not owned, the Ancient Oven, side quests.
    int32_t HiddenNeed(const MaterialPlan& m)
    {
        return m.need[ArmorNotOwned] + m.need[Crafted] + m.need[Quests];
    }

    struct Hint
    {
        std::string keys; // the buttons, by SwitchStyle's names, with spaces between
        std::string label;
        int keep;        // when the footer has no room for every hint, the lowest go first
        int toggle = -1; // a toggle after the label: 1 on, 0 off (as on a cell of BotW Unexplored's legend)
    };

    std::string DishToggleHint(const App& app);

    // What the buttons do right now: only the ones that do something on this tab and on the selected row.
    std::vector<Hint> Hints(const App& app)
    {
        std::vector<Hint> hints;
        if (!app.loading && app.error.empty())
        {
            hints.push_back({"L R", g_section >= 0 ? "section" : "tab", 1});
            hints.push_back({app.tab == 3 ? "Up/Down Left/Right" : "Up/Down", "move", 0});
            if (app.visible[app.tab] > 0 && RowCount(app) > app.visible[app.tab])
                hints.push_back({"Left/Right", "page", 2});
            if (app.tab == 0)
            {
                const std::string other = DishToggleHint(app);
                if (!other.empty())
                    hints.push_back({"A", other, 3});
                hints.push_back({"Y", app.wholePouch ? "ingredients: whole pouch" : "ingredients: free only", 3});
            }
            if (app.tab == 2 && app.prizedFree > 0)
                hints.push_back({"Y", app.sellPrized ? "valuable critters: sold" : "valuable critters: kept", 3});
            if (app.tab != 3)
                hints.push_back({"X", "other needs", 3, app.countHidden ? 1 : 0});
        }
        hints.push_back({"+", "exit", 2});
        return hints;
    }

    // The buttons of a hint, by name.
    std::vector<std::string> Keys(const std::string& keys)
    {
        std::vector<std::string> names;
        size_t start = 0;
        while (start < keys.size())
        {
            size_t end = keys.find(' ', start);
            if (end == std::string::npos)
                end = keys.size();
            if (end > start)
                names.push_back(keys.substr(start, end - start));
            start = end + 1;
        }
        return names;
    }

    // 0.1.15: a button as the Switch draws it (its symbol in the console's Nintendo Extension font, as in BotW
    // Unexplored); without that font, its name on a key cap as up to 0.1.14. Returns its width; draws it when `draw`.
    float Key(const std::string& name, float x, bool draw)
    {
        const float scale = SmallText;
        const float centerY = FooterH * 0.5f; // from the bottom
        const std::string glyphs = SwitchStyle::ButtonGlyphs(*g_font, name);
        if (!glyphs.empty())
        {
            static const float glyphScale = SwitchStyle::GlyphScale(*g_font, 23.0f);
            const float w = Width(glyphs, glyphScale);
            if (draw)
                g_font->AddTextToBatch(glyphs, glm::vec2(x, SwitchStyle::GlyphBaseline(*g_font, glyphScale, centerY)),
                                       glyphScale, CapColor);
            return w;
        }
        const float pad = 8.0f;
        const float w = Width(name, scale) + 2 * pad;
        if (draw)
        {
            RoundRect(x, ScreenH - FooterH + 10, w, FooterH - 20, 7, glm::vec4(1.0f, 1.0f, 1.0f, 0.16f));
            Text(name, x + pad, ScreenH - 16, scale, TextColor);
        }
        return w;
    }

    // The hints on the right of the footer, as in BotW Unexplored: the buttons, what they do. Returns where they start.
    float DrawHints(std::vector<Hint> hints)
    {
        const float scale = SmallText;
        const float keyGap = 7.0f;
        const float gap = 18.0f;
        auto keysW = [&](const Hint& h) {
            float w = 0.0f;
            for (const std::string& k : Keys(h.keys))
                w += Key(k, 0.0f, false) + 3.0f;
            return w - 3.0f;
        };
        auto hintW = [&](const Hint& h) {
            return keysW(h) + keyGap + Width(h.label, scale) + (h.toggle >= 0 ? 8.0f + SwitchStyle::ToggleWidth : 0.0f);
        };
        auto total = [&]() {
            float w = -gap;
            for (const Hint& h : hints)
                w += hintW(h) + gap;
            return w;
        };
        // With a big zoom not every hint fits: moving and tabs go first, they are learned on the first day.
        while (total() > ScreenW - 48 && hints.size() > 1)
        {
            size_t drop = 0;
            for (size_t i = 1; i < hints.size(); i++)
                if (hints[i].keep < hints[drop].keep)
                    drop = i;
            hints.erase(hints.begin() + drop);
        }
        const float start = ScreenW - 24 - total();
        const float baseline = ScreenH - FooterH * 0.5f + SwitchStyle::CapHeight(*g_font, scale) * 0.5f;
        float x = start;
        for (const Hint& h : hints)
        {
            for (const std::string& k : Keys(h.keys))
                x += Key(k, x, true) + 3.0f;
            x += keyGap - 3.0f;
            x = Text(h.label, x, baseline, scale, TextColor);
            if (h.toggle >= 0)
            {
                x += 8.0f;
                SwitchStyle::Toggle(*g_shapes, x + SwitchStyle::ToggleWidth, (FooterH + SwitchStyle::ToggleHeight) * 0.5f,
                                    h.toggle == 1);
                x += SwitchStyle::ToggleWidth;
            }
            x += gap;
        }
        return start;
    }

    void Footer(const App& app)
    {
        Rect(0, ScreenH - FooterH, ScreenW, FooterH, BarColor);
        Rect(0, ScreenH - FooterH, ScreenW, 1, Hairline);
        const float hintsStart = DrawHints(Hints(app));
        // 0.1.15: where the numbers come from went to the top right, as in BotW Unexplored; here, while X or Y have
        // the dishes cooked again, that it is being done.
        const float baseline = ScreenH - FooterH * 0.5f + SwitchStyle::CapHeight(*g_font, SmallText) * 0.5f;
        if (app.computing && Width("Cooking again...", SmallText) <= hintsStart - 24 - 28)
            Text("Cooking again...", 24, baseline, SmallText, Accent);
    }

    // The result of a dish on one line, with the game's icons: effect, hearts. Returns the x after it.
    float ResultLine(const CookResult& r, float x, float baseline, float scale)
    {
        const float icon = 34;
        const float top = baseline - 25;
        if (r.effect != EffectNone)
        {
            Icons::Ui(r.effect == LifeMaxUp ? "heart" : EffectIcon(r.effect), x, top, icon, EffectTint(r.effect));
            const bool timed = r.effect != LifeMaxUp && r.effect != GutsRecover && r.effect != ExGutsMaxUp;
            if (timed)
            {
                // The level, then the time in the game's green, with its clock.
                x = Text("Lv " + std::to_string(r.vitality), x + icon + 6, baseline, scale, Gold) + 16;
                Icons::Ui("clock", x, top + 2, icon - 4, glm::vec4(TimeGreen, 1.0f));
                x = Text(Clock(r.time), x + icon, baseline, scale, TimeGreen) + 18;
            }
            else
                x = Text(Strength(r), x + icon + 6, baseline, scale, Gold) + 18;
            if (r.effect == LifeMaxUp)
                return Text("and every heart", x, baseline, scale * 0.9f, TextDim);
        }
        Icons::Ui("heart", x, top, icon, HeartRed);
        return Text(Hearts(r.life), x + icon + 6, baseline, scale, Gold);
    }

    // "Critical 30%: +3 hearts or +5:00". What each bonus adds comes from cooking the pot again with it, so stamina
    // shows in wheels and a level in levels.
    std::string CritText(const App& app, const FoundDish& d)
    {
        const CookResult& r = d.result;
        std::vector<std::string> options;
        if (r.critBonuses & static_cast<uint8_t>(CritBonus::Life))
        {
            const int32_t more = Cook(*app.cook, d.items, d.itemCount, CritBonus::Life).life - r.life;
            options.push_back(more > 0 ? "+" + Hearts(more) + " hearts" : std::string("more hearts"));
        }
        if (r.critBonuses & static_cast<uint8_t>(CritBonus::Vitality))
        {
            const int32_t more = Cook(*app.cook, d.items, d.itemCount, CritBonus::Vitality).vitality - r.vitality;
            if (more <= 0)
                options.push_back("a stronger effect");
            else if (r.effect == LifeMaxUp)
                options.push_back("+" + Hearts(more) + (more == 4 ? " yellow heart" : " yellow hearts"));
            else if (r.effect == GutsRecover)
                options.push_back("+" + Wheels(more, 1000) + " wheels");
            else if (r.effect == ExGutsMaxUp)
                options.push_back("+" + Wheels(more, 5) + " wheels");
            else
                options.push_back("+" + std::to_string(more) + (more == 1 ? " level" : " levels"));
        }
        if (r.critBonuses & static_cast<uint8_t>(CritBonus::Time))
        {
            const int32_t more = Cook(*app.cook, d.items, d.itemCount, CritBonus::Time).time - r.time;
            options.push_back(more > 0 ? "+" + Clock(more) : std::string("more time"));
        }
        std::string crit = "Critical " + std::to_string(std::min(100, static_cast<int>(r.critChance))) + "%";
        for (size_t i = 0; i < options.size(); i++)
            crit += (i == 0 ? ": " : " or ") + options[i];
        return crit;
    }

    const float PartStep = 30.0f; // the height of an ingredient's line in a dish

    // A dish's name: smaller before it is cut, and on two lines when it does not fit even so.
    std::vector<std::string> NameLines(const std::string& name, float width, float& scale)
    {
        scale = 0.54f;
        while (scale > 0.46f && Width(name, scale) > width)
            scale -= 0.02f;
        if (Width(name, scale) <= width)
            return {name};
        std::vector<std::string> lines = Wrap(name, scale, width);
        if (lines.size() > 2)
        {
            std::string rest = lines[1];
            for (size_t i = 2; i < lines.size(); i++)
                rest += " " + lines[i];
            lines.resize(2);
            lines[1] = rest;
        }
        for (std::string& line : lines)
            line = Fit(line, scale, width);
        return lines;
    }

    // `top`: the baseline of the title. Returns the baseline of the last line.
    float DrawDish(const App& app, const FoundDish& d, float x, float top, float width, const std::string& title)
    {
        char name[96];
        DishName(*app.cook, d.result, name, sizeof(name));
        Text(Fit(title, SmallText, width), x, top, SmallText, Accent);
        float nameScale = 0.54f;
        const std::vector<std::string> nameLines = NameLines(name, width, nameScale);
        float y = top + 34;
        for (size_t i = 0; i < nameLines.size(); i++)
        {
            Text(nameLines[i], x, y, nameScale, TextColor);
            if (i + 1 < nameLines.size())
                y += 28;
        }

        // The dish as the game shows it in the pouch, on the right of what makes it; the lines under the name stop
        // short of it.
        const float picture = 112.0f;
        Icons::Dish(DishPicture(app, d.result), x + width - picture, y + 10, picture);
        const float textWidth = width - picture - 20;

        const uint8_t mask = TierMask(app);
        const float step = PartStep;
        const float icon = 30.0f;
        const float scale = 0.40f;
        y += step;
        for (const Part& p : Parts(d.items, d.itemCount))
        {
            // With the whole pouch, say which ingredients come out of what the Reserve tab holds back.
            std::string held;
            if (app.wholePouch)
            {
                const MaterialPlan* m = app.plan->Find(p.item);
                const int32_t reserved = m ? p.count - m->Free(mask) : 0;
                if (reserved > 0)
                    held = std::to_string(std::min(reserved, p.count)) + " reserved";
            }
            const float heldWidth = held.empty() ? 0.0f : Width(held, SmallText) + 16;
            Icons::Item(ActorName(app, p.item), x + 4, y - 7 - icon / 2, icon);
            const std::string line = std::to_string(p.count) + "x " + NameOf(app, p.item);
            const float after = Text(Fit(line, scale, textWidth - 44 - heldWidth), x + 44, y, scale, TextColor);
            if (!held.empty())
                Text(held, after + 16, y, SmallText, Warn);
            y += step;
        }
        y += 38 - step;
        ResultLine(d.result, x, y, 0.42f);
        // What it sells for is in the Sell tab.
        y += 30;
        Text(Fit(CritText(app, d), SmallText, textWidth), x, y, SmallText, TextDim);
        return y;
    }

    struct ShownDish
    {
        const FoundDish* dish;
        std::string title;
        std::string brief; // what A says to switch to it when only one dish fits
    };

    // Two dishes side by side: two dishes of five different ingredients do not fit one under the other, and this way
    // the lines of one can be compared with the other's.
    void DrawDishes(const App& app, const std::vector<ShownDish>& shown, float x, float top, float width)
    {
        const float gap = 44;
        const float w = (width - gap) / 2;
        float bottom = top;
        for (size_t i = 0; i < shown.size() && i < 2; i++)
            bottom = std::max(bottom, DrawDish(app, *shown[i].dish, x + i * (w + gap), top, w, shown[i].title));
        if (shown.size() > 1)
            Rect(x + w + gap / 2, top - 16, 1, bottom - top + 26, Hairline);
    }

    // The short result in the list on the left.
    std::string Summary(const App& app, const Goal& g, bool& found)
    {
        const char* nothing = app.wholePouch ? "nothing" : "nothing free";
        switch (g.kind)
        {
        case GoalKind::Effect:
            found = g.result.found;
            return found ? Strength(g.result.best.result) : nothing;
        case GoalKind::Hearts:
            found = g.target.found || g.hearty.found;
            if (g.target.found)
                return Hearts(g.target.dish.result.life) + " hearts";
            return g.hearty.found ? "hearty dish" : nothing;
        case GoalKind::Stamina:
            found = g.target.found;
            return found ? Strength(g.target.dish.result) : nothing;
        }
        found = false;
        return "";
    }

    // The dishes of a goal: the best and the cheapest as strong, or the cheapest reaching the target.
    std::vector<ShownDish> ShownDishes(const App& app, const Goal& g)
    {
        std::vector<ShownDish> shown;
        switch (g.kind)
        {
        case GoalKind::Effect:
        {
            if (!g.result.found)
                break;
            shown.push_back({&g.result.best, "BEST", "best"});
            const FoundDish& c = g.result.cheapest;
            if (c.itemCount > 0 && c.cost < g.result.best.cost)
                shown.push_back({&c, "CHEAPEST AS STRONG", "cheapest"});
            break;
        }
        case GoalKind::Hearts:
        {
            const std::string target = Hearts(HeartsTarget(app));
            const std::string all = Hearts(app.maxHearts);
            if (g.target.found)
                shown.push_back({&g.target.dish,
                                 g.target.reached ? "CHEAPEST TO HEAL " + target + " OF " + all + " HEARTS"
                                                  : "NOTHING HEALS " + target + " HEARTS: THE MOST",
                                 "cheapest"});
            // Its result line says the rest: "and every heart".
            if (g.hearty.found)
                shown.push_back({&g.hearty.dish, "OR ANY YELLOW-HEART DISH", "yellow hearts"});
            break;
        }
        case GoalKind::Stamina:
        {
            const std::string wheels = Wheels(StaminaTarget(app), 1000);
            if (g.target.found)
                shown.push_back({&g.target.dish,
                                 g.target.reached ? "CHEAPEST TO REFILL YOUR " + wheels + " WHEELS"
                                                  : "NOTHING REFILLS " + wheels + " WHEELS: THE MOST",
                                 "cheapest"});
            break;
        }
        }
        return shown;
    }

    // The list on the left: as wide as the longest name and result need, a third of the screen or more.
    float DishListW(const App& app)
    {
        float label = 0.0f;
        for (const Goal& g : app.goals)
            label = std::max(label, Width(g.label, 0.44f));
        const float w = 64 + label + 24 + Width("+20 yellow hearts", 0.38f) + 28;
        return std::min(ScreenW * 0.45f, std::max(w, std::min(400.0f, ScreenW * 0.3125f)));
    }

    // Two dishes side by side need 640 px; with less, one dish and A for the other.
    bool TwoDishColumns(const App& app)
    {
        return ScreenW - DishListW(app) - 80 >= 640;
    }

    std::string DishToggleHint(const App& app)
    {
        if (app.goals.empty() || TwoDishColumns(app))
            return "";
        const std::vector<ShownDish> shown = ShownDishes(app, app.goals[app.cursor[0]]);
        if (shown.size() < 2)
            return "";
        return shown[app.dishPage ? 0 : 1].brief;
    }

    void DishesTab(App& app)
    {
        const float listW = DishListW(app);
        const float top = HeaderH + 12;
        const float rowH = std::min(46.0f, (ScreenH - HeaderH - FooterH - 16) /
                                               static_cast<float>(std::max<size_t>(1, app.goals.size())));
        const float icon = std::min(40.0f, rowH - 6);
        app.visible[0] = 0;
        Rect(0, HeaderH + 1, listW, ScreenH - HeaderH - FooterH - 1, PanelColor);
        for (size_t i = 0; i < app.goals.size(); i++)
        {
            const Goal& g = app.goals[i];
            const float y = top + i * rowH;
            if (static_cast<int>(i) == app.cursor[0])
                SelectedRow(12, y, listW - 24, rowH - 4, 8, PanelColor);
            Icons::Ui(g.kind == GoalKind::Hearts ? "heart" : (g.effect == LifeMaxUp ? "heart" : EffectIcon(g.effect)), 18,
                      y + (rowH - 4 - icon) / 2, icon, EffectTint(g.kind == GoalKind::Hearts ? EffectNone : g.effect));
            const float baseline = y + (rowH - 4) / 2 + 7;
            Text(g.label, 24 + icon, baseline, 0.44f, TextColor);
            bool found = false;
            const std::string summary = Summary(app, g, found);
            Text(summary, listW - 28, baseline, 0.38f, found ? Gold : TextDim, ALIGN_RIGHT);
        }

        const Goal& g = app.goals[app.cursor[0]];
        const float x = listW + 40;
        const float width = ScreenW - x - 40;
        float y = top + 30;
        const std::string heading = std::string(g.label) + " (" + g.detail + ")";
        Text(Fit(heading, 0.40f, width), x, y, 0.40f, TextDim);
        if (app.wholePouch)
        {
            // Beside the heading when there is room, else under it.
            const std::string warn = "Whole pouch: may use what the Reserve tab holds back";
            if (Width(heading, 0.40f) + 32 + Width(warn, SmallText) <= width)
                Text(warn, ScreenW - 40, y, SmallText, Warn, ALIGN_RIGHT);
            else
            {
                y += 26;
                Text(Fit(warn, SmallText, width), x, y, SmallText, Warn);
            }
        }
        const std::string nothing = app.wholePouch ? "Nothing in the pouch makes this."
                                                   : "Nothing free makes this. Y searches the whole pouch, X stops "
                                                     "counting the other needs.";
        const std::vector<ShownDish> shown = ShownDishes(app, g);
        if (shown.empty())
            Text(nothing, x, y + 50, 0.42f, TextColor, ALIGN_LEFT, width);
        else if (TwoDishColumns(app))
            DrawDishes(app, shown, x, y + 42, width);
        else
        {
            const size_t i = std::min(static_cast<size_t>(app.dishPage), shown.size() - 1);
            std::string title = shown[i].title;
            if (shown.size() > 1)
                title += "   " + std::to_string(i + 1) + " of " + std::to_string(shown.size());
            DrawDish(app, *shown[i].dish, x, y + 42, width, title);
        }
    }

    // 0.1.17, the user's review of 26/11 ("Tudo um listão enorme não é muito legal de navegar e descobrir"): the
    // numbers that were lines of text at the top of Reserve, Sell and EXP, each in a card; Reserve and Sell a short
    // list in groups with the selected item on the right, as in Dishes; the enemies of EXP in tiles.
    const float CardH = 64.0f;
    const glm::vec4 CardColor(1.0f, 1.0f, 1.0f, 0.045f);
    const glm::vec4 TrackColor(1.0f, 1.0f, 1.0f, 0.12f);

    struct Stat
    {
        std::string label;
        std::string value;
        glm::vec3 color;
        std::string extra; // after the value, dim
        std::string note;  // at the card's right end, dim
        float bar;         // 0 to 1: a bar between the value and the note; < 0 for none
        float weight;      // the card's share of the width
    };

    Stat MakeStat(const std::string& label, const std::string& value, const glm::vec3& color, const std::string& extra,
                  float weight)
    {
        return {label, value, color, extra, std::string(), -1.0f, weight};
    }

    // A row of cards across the screen. Returns the y under them.
    float StatCards(const std::vector<Stat>& stats, float top)
    {
        const float gap = 12.0f, left = 24.0f;
        float weights = 0.0f;
        for (const Stat& s : stats)
            weights += s.weight;
        const float room = ScreenW - 2 * left - gap * (stats.size() - 1);
        float x = left;
        for (const Stat& s : stats)
        {
            const float w = room * s.weight / weights;
            RoundRect(x, top, w, CardH, 10, CardColor);
            Text(Fit(s.label, SmallText, w - 32), x + 16, top + 23, SmallText, TextDim);
            float after = Text(s.value, x + 16, top + 52, 0.54f, s.color);
            const float noteW = s.note.empty() ? 0.0f : Width(s.note, SmallText) + 14;
            if (!s.extra.empty())
            {
                const float space = x + w - 16 - noteW - (after + 10) - (s.bar >= 0.0f ? 60 : 0);
                if (space > 30)
                    after = Text(Fit(s.extra, SmallText, space), after + 10, top + 51, SmallText, TextDim);
            }
            if (s.bar >= 0.0f)
            {
                const float bx = after + 14, bw = x + w - 16 - noteW - bx;
                if (bw > 40)
                {
                    RoundRect(bx, top + 41, bw, 8, 4, TrackColor);
                    RoundRect(bx, top + 41, std::max(8.0f, bw * std::min(1.0f, std::max(0.0f, s.bar))), 8, 4,
                              glm::vec4(s.color, 1.0f));
                }
            }
            if (!s.note.empty())
                Text(s.note, x + w - 16, top + 51, SmallText, TextDim, ALIGN_RIGHT);
            x += w + gap;
        }
        return top + CardH;
    }

    struct GroupTitle
    {
        std::string text;
        glm::vec3 color;
    };

    const float ListLine = 34.0f;

    float ListWidth()
    {
        return std::min(430.0f, ScreenW * 0.42f);
    }

    // A list in groups on the left, from `top` to `bottom`: each group's title on a line of its own, then its items.
    // `groupOf[i]` is the group of the i-th item, in order; the cursor counts the items only, and the title over the
    // first item of a group comes into view with it. `drawItem(i, top)` draws one item's line.
    template <typename DrawItem>
    void GroupedList(App& app, int tab, const std::vector<int>& groupOf, const std::vector<GroupTitle>& titles, float top,
                     float bottom, DrawItem drawItem)
    {
        const float w = ListWidth();
        Rect(0, top, w, bottom - top, PanelColor);
        std::vector<int> lines; // an item, or -1 - its group for a title
        std::vector<int> lineOf(groupOf.size(), 0);
        for (size_t i = 0; i < groupOf.size(); i++)
        {
            if (i == 0 || groupOf[i] != groupOf[i - 1])
                lines.push_back(-1 - groupOf[i]);
            lineOf[i] = static_cast<int>(lines.size());
            lines.push_back(static_cast<int>(i));
        }
        const int count = static_cast<int>(lines.size());
        const int visible = std::max(1, static_cast<int>((bottom - top - 8) / ListLine));
        app.visible[tab] = visible;
        if (groupOf.empty())
            return;
        int& cursor = app.cursor[tab];
        int& scroll = app.scroll[tab];
        cursor = std::max(0, std::min(cursor, static_cast<int>(groupOf.size()) - 1));
        int line = lineOf[cursor];
        KeepInView(line, scroll, count, visible);
        if (line > 0 && lines[line - 1] < 0 && scroll == line)
            scroll--;
        for (int l = scroll; l < count && l < scroll + visible; l++)
        {
            const float ry = top + 4 + (l - scroll) * ListLine;
            if (lines[l] < 0)
            {
                const GroupTitle& title = titles[-1 - lines[l]];
                Text(title.text, 20, ry + 26, SmallText, title.color);
                continue;
            }
            if (lines[l] == cursor)
                SelectedRow(6, ry, w - 18, ListLine, 8, PanelColor);
            drawItem(lines[l], ry);
        }
        ScrollBar(top + 4, visible * ListLine, count, visible, scroll, w);
    }

    // "★2–4": the stars of an armor's upgrades
    std::string Stars(int from, int to)
    {
        const std::string star = "\xE2\x98\x85";
        return from == to ? star + std::to_string(to) : star + std::to_string(from) + "\xE2\x80\x93" + std::to_string(to);
    }

    struct ReserveItem
    {
        int group;      // 0 short for the armor in the pouch, 1 short for the rest, 2 enough
        int row;        // in app.rows
        int32_t amount; // what is missing, or what is free
    };

    // The materials in three groups, the most missing (or the most free) first.
    std::vector<ReserveItem> ReserveItems(const App& app)
    {
        const uint8_t mask = TierMask(app);
        std::vector<ReserveItem> items;
        for (size_t i = 0; i < app.rows.size(); i++)
        {
            const MaterialPlan& m = app.plan->materials[app.rows[i].index];
            const int32_t ownShort = std::max(0, m.need[OwnedReachable] + m.need[OwnedNextFairy] - m.have);
            const int32_t shortBy = m.Needed(mask) - m.have;
            if (ownShort > 0)
                items.push_back({0, static_cast<int>(i), ownShort});
            else if (shortBy > 0)
                items.push_back({1, static_cast<int>(i), shortBy});
            else
                items.push_back({2, static_cast<int>(i), m.Free(mask)});
        }
        std::stable_sort(items.begin(), items.end(), [](const ReserveItem& a, const ReserveItem& b) {
            if (a.group != b.group)
                return a.group < b.group;
            return a.amount > b.amount;
        });
        return items;
    }

    // The name and the picture of what a material is held back for.
    std::string UseName(const App& app, const MaterialUse& u)
    {
        if (u.tier == Quests)
            return u.what < static_cast<uint32_t>(app.reserve->questCount) ? app.reserve->quests[u.what].title : "?";
        return NameOf(app, u.what);
    }

    std::string UseIcon(const App& app, const MaterialUse& u)
    {
        if (u.tier == Crafted)
        {
            for (int i = 0; i < app.reserve->craftCount; i++)
                if (app.reserve->crafts[i].hash == u.what)
                    return app.reserve->crafts[i].name;
            return "";
        }
        if (u.tier == Quests)
            return "";
        const ArmorEntry* a = app.reserve->FindArmor(u.what);
        return a ? a->name : "";
    }

    // After the name: the stars of an armor's upgrades; for a piece not owned, "make" when the Ancient Oven makes it.
    std::string UseTag(const MaterialUse& u)
    {
        if (u.tier != OwnedReachable && u.tier != ArmorNotOwned)
            return "";
        if (u.toStar == 0)
            return "make";
        const std::string stars = Stars(std::max(1, u.fromStar), u.toStar);
        return u.fromStar == 0 ? "make, " + stars : stars;
    }

    // What a material asks for, by its group: for the upgrades of the armor in the pouch only the upgrades (as the group
    // is sorted); for the rest every need counted (the others when X counts them). And what is left over.
    struct ReserveGoal
    {
        int32_t have, goal, spare;
    };

    ReserveGoal GoalOf(const App& app, const ReserveItem& it)
    {
        const MaterialPlan& m = app.plan->materials[app.rows[it.row].index];
        const uint8_t mask = TierMask(app);
        const int32_t goal = it.group == 0 ? m.need[OwnedReachable] + m.need[OwnedNextFairy] : m.Needed(mask);
        return {m.have, goal, m.Free(mask)};
    }

    // The colour of a bar by the group, as the detail's bar has always been.
    glm::vec3 GoalBarColor(const ReserveItem& it)
    {
        return it.group == 0 ? Warn : it.group == 1 ? TextDim : Good;
    }

    // The colour of a material by its group: missing for the upgrades, missing for the rest, enough.
    glm::vec3 GroupColor(const ReserveItem& it, const ReserveGoal& g)
    {
        if (it.group == 0)
            return Warn;
        if (it.group == 1)
            return TextColor;
        return g.spare > 0 || g.goal > 0 ? Good : TextDim;
    }

    // The text after a material, in the list (short) and on top of the detail: what you have of the goal, as "3/45" (as
    // the map's legend counts), or what is left over when it is enough (chosen by the user, 27/11, over "42 more" and
    // over the bar alone).
    std::string GoalText(const ReserveItem& it, const ReserveGoal& g, bool detail)
    {
        if (it.group == 2 && g.spare > 0)
            return detail ? Number(g.spare) + " spare" : "+" + Number(g.spare);
        if (g.goal == 0)
            return std::string();
        return Number(g.have) + "/" + Number(g.goal);
    }

    // A small bar: what you have of the goal (full when it is enough).
    void MiniBar(float x, float top, float w, const ReserveItem& it, const ReserveGoal& g)
    {
        RoundRect(x, top, w, 8, 4, glm::vec4(1.0f, 1.0f, 1.0f, 0.10f));
        const float part = g.goal > 0 ? std::min(1.0f, static_cast<float>(g.have) / static_cast<float>(g.goal)) : 1.0f;
        if (part > 0.0f)
            RoundRect(x, top, std::max(8.0f, w * part), 8, 4, glm::vec4(GoalBarColor(it), 1.0f));
    }

    // The selected material: have against needed, then what it is for by name, in four short sections.
    void ReserveDetail(const App& app, const ReserveItem& it, float x, float top, float width)
    {
        const ReservePlan& p = *app.plan;
        const uint8_t mask = TierMask(app);
        const MaterialPlan& m = p.materials[app.rows[it.row].index];
        const ReserveGoal goal = GoalOf(app, it);
        const int32_t needed = goal.goal;
        const int32_t shortBy = needed - m.have;
        const int32_t free = m.Free(mask);
        float y = top + 40;
        Icons::Item(ActorName(app, m.item), x, y - 33, 44);
        const std::string status = GoalText(it, goal, true);
        const glm::vec3 color = it.group == 2 && free == 0 ? TextDim : GroupColor(it, goal);
        const float statusW = Width(status, 0.46f);
        Text(Fit(app.rows[it.row].name, 0.50f, width - 56 - statusW - 20), x + 56, y, 0.50f, TextColor);
        Text(status, x + width, y, 0.46f, color, ALIGN_RIGHT);

        // Have against needed: the needed part of the bar lighter, what you have over it.
        y += 22;
        const float most = static_cast<float>(std::max<int32_t>(1, std::max(m.have, needed)));
        RoundRect(x, y, width, 12, 6, glm::vec4(1.0f, 1.0f, 1.0f, 0.07f));
        if (needed > 0)
            RoundRect(x, y, std::max(12.0f, width * needed / most), 12, 6, glm::vec4(1.0f, 1.0f, 1.0f, 0.16f));
        if (m.have > 0)
            RoundRect(x, y, std::max(12.0f, width * m.have / most), 12, 6,
                      glm::vec4(shortBy > 0 ? GoalBarColor(it) : Good, 1.0f));
        y += 46; // nothing under the bar: the fraction on top says it

        // What it is for: each thing by name and picture, the most first, in sections. The upgrades that wait for
        // another Great Fairy are among the upgrades (their stars say it).
        struct Section
        {
            Tier tier;
            const char* title;
            std::vector<const MaterialUse*> uses;
            int32_t total;
        };
        Section sections[] = {{OwnedReachable, "Upgrades", {}, 0},
                              {ArmorNotOwned, "Armor to get", {}, 0},
                              {Crafted, "Ancient Oven", {}, 0},
                              {Quests, "Side quests", {}, 0}};
        int present = 0;
        for (Section& s : sections)
        {
            for (int i = 0; i < p.useCount; i++)
                if (p.uses[i].item == m.item && p.uses[i].tier == s.tier)
                {
                    s.uses.push_back(&p.uses[i]);
                    s.total += p.uses[i].amount;
                }
            std::stable_sort(s.uses.begin(), s.uses.end(),
                             [](const MaterialUse* a, const MaterialUse* b) { return a->amount > b->amount; });
            present += !s.uses.empty();
        }
        // Lines: a title for each section, and its things as long as there is room, the later sections keeping one
        // line each; a section cut short says how many more on its last line.
        const float lineH = 27.0f, titleH = 28.0f;
        int budget = static_cast<int>((ScreenH - FooterH - 8 - y - present * titleH) / lineH);
        int later = present;
        const float iconX = x, numX = x + 70, nameX = x + 82;
        for (const Section& s : sections)
        {
            if (s.uses.empty())
                continue;
            later--;
            const int lines = std::min(static_cast<int>(s.uses.size()), std::max(1, budget - later));
            budget -= lines;
            const bool counted = s.tier == OwnedReachable || app.countHidden;
            const glm::vec3 titleColor = s.tier == OwnedReachable ? Warn : TextDim;
            const float after = Text(s.title, x, y + 4, SmallText, counted ? titleColor : TextDim);
            Text(Number(s.total) + (counted ? std::string() : "  not counted"), after + 12, y + 4, SmallText, TextDim);
            y += titleH;
            const bool cut = lines < static_cast<int>(s.uses.size());
            const int shown = cut && lines > 1 ? lines - 1 : lines;
            for (int k = 0; k < shown; k++)
            {
                const MaterialUse& u = *s.uses[k];
                const std::string icon = UseIcon(app, u);
                if (!icon.empty())
                    Icons::Item(icon, iconX, y - 21, 26);
                Text(Number(u.amount), numX, y, 0.38f, counted ? TextColor : TextDim, ALIGN_RIGHT);
                std::string tag = UseTag(u);
                if (cut && lines == 1)
                    tag += (tag.empty() ? "" : "   ") + std::string("+") + std::to_string(s.uses.size() - 1) + " more";
                const float tagW = tag.empty() ? 0.0f : Width(tag, SmallText) + 12;
                const float end = Text(Fit(UseName(app, u), 0.38f, x + width - nameX - tagW), nameX, y, 0.38f,
                                       counted ? TextColor : TextDim);
                if (!tag.empty())
                    Text(tag, end + 12, y, SmallText, u.tier == OwnedReachable && counted ? Gold : TextDim);
                y += lineH;
            }
            if (cut && lines > 1)
            {
                Text("and " + std::to_string(s.uses.size() - shown) + " more", nameX, y, SmallText, TextDim);
                y += lineH;
            }
        }
    }

    void ReserveTab(App& app)
    {
        const ReservePlan& p = *app.plan;
        const uint8_t mask = TierMask(app);
        const int32_t rest = p.rupeesNeeded[ArmorNotOwned] + p.rupeesNeeded[Crafted] + p.rupeesNeeded[Quests];
        const int32_t own = p.rupeesNeeded[OwnedReachable] + p.rupeesNeeded[OwnedNextFairy];
        const float top =
            StatCards({MakeStat("Great Fairies", std::to_string(p.fairies), TextColor, "of 4", 0.7f),
                       MakeStat("Upgrades left", std::to_string(p.ownedUpgradesLeft[0]), TextColor,
                                p.ownedUpgradesLeft[1] > 0 ? "+" + std::to_string(p.ownedUpgradesLeft[1]) + " after another fairy"
                                                           : std::string(),
                                1.25f),
                       MakeStat("Rupees: fountains", Number(own), p.rupees >= own ? Good : Gold, std::string(), 1.0f),
                       MakeStat("Rupees: armor, oven", app.countHidden ? Number(rest) : std::string("not counted"),
                                app.countHidden ? (p.rupees >= p.RupeesNeeded(mask) ? Good : Gold) : TextDim,
                                std::string(), 0.95f)},
                      HeaderH + 14) +
            14;
        const float bottom = ScreenH - FooterH;
        const std::vector<ReserveItem> items = ReserveItems(app);
        if (items.empty())
        {
            app.visible[1] = 0;
            Text("Nothing to hold back: no armor, purchase or quest needs your materials.", 28, top + 40, 0.40f, TextDim,
                 ALIGN_LEFT, ScreenW - 56);
            return;
        }
        int counts[3] = {0, 0, 0};
        std::vector<int> groupOf;
        for (const ReserveItem& it : items)
        {
            counts[it.group]++;
            groupOf.push_back(it.group);
        }
        const std::vector<GroupTitle> titles = {{"For upgrades   " + std::to_string(counts[0]), Warn},
                                                {"For new armor, oven, quests   " + std::to_string(counts[1]), TextDim},
                                                {"Enough   " + std::to_string(counts[2]), Good}};
        const float listW = ListWidth();
        GroupedList(app, 1, groupOf, titles, top, bottom, [&](int i, float ry) {
            const ReserveItem& it = items[i];
            const MaterialPlan& m = p.materials[app.rows[it.row].index];
            const float baseline = ry + 23;
            Icons::Item(ActorName(app, m.item), 20, ry + 4, 26);
            // A small bar in a column of its own, as the detail's, and the fraction after it (27/11, at the user's word:
            // a bar instead of "short 38")
            const ReserveGoal goal = GoalOf(app, it);
            const std::string amount = GoalText(it, goal, false);
            const float barW = 84, barRight = listW - 26 - 96;
            Text(Fit(app.rows[it.row].name, 0.38f, barRight - barW - 16 - 56), 56, baseline, 0.38f, TextColor);
            MiniBar(barRight - barW, ry + 13, barW, it, goal); // full when enough, also when nothing counted asks for it
            if (!amount.empty())
                Text(amount, listW - 26, baseline, SmallText, GroupColor(it, goal), ALIGN_RIGHT);
        });
        ReserveDetail(app, items[app.cursor[1]], listW + 32, top, ScreenW - listW - 64);
    }

    struct SellLine
    {
        int group = 0;           // 0 a pot, 1 sold as it is, 2 a critter kept for cooking
        std::vector<Part> parts; // a pot's ingredients
        CookResult result;       // a pot's dish
        uint32_t rawItem = 0;
        int32_t rawCount = 0;
        int32_t price = 0; // each pot, or the raw sale
        int32_t raw = 0;   // a pot's ingredients sold as they are
        int count = 0;     // identical pots
    };

    // The pots, the most they bring in all first; then what sells as it is; then the critters kept.
    std::vector<SellLine> SellLines(const App& app)
    {
        const SalePlan& s = *app.sale;
        std::vector<SellLine> lines;
        for (int i = 0; i < s.potCount; i++)
        {
            const SalePot& pot = s.pots[i];
            if (!lines.empty() && lines.back().price == pot.price && i > 0 &&
                memcmp(s.pots[i - 1].items, pot.items, sizeof(pot.items)) == 0)
            {
                lines.back().count++;
                continue;
            }
            SellLine l;
            l.parts = Parts(pot.items, pot.itemCount);
            l.result = Cook(*app.cook, pot.items, pot.itemCount);
            l.price = pot.price;
            l.raw = pot.rawPrice;
            l.count = 1;
            lines.push_back(l);
        }
        std::stable_sort(lines.begin(), lines.end(),
                         [](const SellLine& a, const SellLine& b) { return a.price * a.count > b.price * b.count; });
        for (int i = 0; i < s.rawCount; i++)
        {
            SellLine l;
            l.group = 1;
            l.rawItem = s.raw[i].item;
            l.rawCount = s.raw[i].count;
            l.price = s.raw[i].price;
            lines.push_back(l);
        }
        for (int i = 0; i < s.keptCount; i++)
        {
            SellLine l;
            l.group = 2;
            l.rawItem = s.kept[i].item;
            l.rawCount = s.kept[i].count;
            lines.push_back(l);
        }
        return lines;
    }

    // The selected line: a pot's ingredients and picture, what cooking adds; or why an item is sold or kept as it is.
    void SellDetail(const App& app, const SellLine& l, float x, float top, float width)
    {
        float y = top + 40;
        if (l.group == 0)
        {
            char name[96];
            DishName(*app.cook, l.result, name, sizeof(name));
            const std::string each = Number(l.price) + " each";
            const float eachW = Width(each, 0.46f);
            Text(Fit(name, 0.50f, width - eachW - 20), x, y, 0.50f, TextColor);
            Text(each, x + width, y, 0.46f, Gold, ALIGN_RIGHT);
            const float picture = 112.0f;
            Icons::Dish(DishPicture(app, l.result), x + width - picture, y + 16, picture);
            y += 42;
            for (const Part& part : l.parts)
            {
                Icons::Item(ActorName(app, part.item), x + 4, y - 22, 30);
                Text(Fit(std::to_string(part.count) + "x " + NameOf(app, part.item), 0.40f, width - picture - 64), x + 44, y,
                     0.40f, TextColor);
                y += 30;
            }
            y += 16;
            const float after = Text("Sold raw: " + Number(l.raw) + "   cooking adds ", x, y, 0.38f, TextDim);
            Text("+" + Number(l.price - l.raw), after, y, 0.38f, Good);
            y += 30;
            Text(l.count > 1 ? std::to_string(l.count) + " pots, " + Number(l.price * l.count) + " in all"
                             : std::string("One pot"),
                 x, y, 0.38f, TextColor);
            return;
        }
        Icons::Item(ActorName(app, l.rawItem), x, y - 33, 44);
        const std::string price = l.group == 1 ? Number(l.price) : std::string();
        const float priceW = price.empty() ? 0.0f : Width(price, 0.46f) + 20;
        Text(Fit(std::to_string(l.rawCount) + "x " + NameOf(app, l.rawItem), 0.50f, width - 56 - priceW), x + 56, y, 0.50f,
             TextColor);
        if (!price.empty())
            Text(price, x + width, y, 0.46f, Gold, ALIGN_RIGHT);
        y += 50;
        if (l.group == 1)
            Text("Sell as it is.", x, y, 0.38f, TextDim);
        else
        {
            Text("Kept for cooking: yellow hearts or extra stamina.", x, y, 0.38f, TextDim);
            Text("Y sells these too.", x, y + 30, 0.38f, TextDim);
        }
    }

    void SellTab(App& app)
    {
        const SalePlan& s = *app.sale;
        const int32_t total = s.cookedTotal + s.partsLeftRaw + s.rawTotal;
        const float top =
            StatCards({MakeStat("All of it sells for", Number(total), Gold,
                                Number(app.plan->rupees + total) + " with your " + Number(app.plan->rupees), 2.0f),
                       MakeStat("Elixirs, " + std::to_string(s.potCount) + " pots", Number(s.cookedTotal), TextColor,
                                "raw " + Number(s.cookedRawTotal), 1.25f),
                       MakeStat("Monster parts (" + Number(s.partsLeft) + ")", Number(s.partsLeftRaw), TextColor,
                                std::string(), 1.15f),
                       MakeStat("Other items", Number(s.rawTotal), TextColor, std::string(), 0.75f)},
                      HeaderH + 14) +
            14;
        const float bottom = ScreenH - FooterH;
        const std::vector<SellLine> lines = SellLines(app);
        if (lines.empty())
        {
            app.visible[2] = 0;
            Text("Nothing free to sell.", 28, top + 40, 0.40f, TextDim);
            return;
        }
        int counts[3] = {0, 0, 0};
        std::vector<int> groupOf;
        for (const SellLine& l : lines)
        {
            counts[l.group]++;
            groupOf.push_back(l.group);
        }
        const std::vector<GroupTitle> titles = {{"Cook and sell   " + std::to_string(counts[0]), Gold},
                                                {"Sell as it is   " + std::to_string(counts[1]), TextDim},
                                                {"Kept for cooking   " + std::to_string(counts[2]), TextDim}};
        const float listW = ListWidth();
        GroupedList(app, 2, groupOf, titles, top, bottom, [&](int i, float ry) {
            const SellLine& l = lines[i];
            const float baseline = ry + 23;
            std::string name, amount;
            glm::vec3 color = Gold;
            if (l.group == 0)
            {
                Icons::Dish(DishPicture(app, l.result), 18, ry + 2, 30);
                char dish[96];
                DishName(*app.cook, l.result, dish, sizeof(dish));
                name = dish;
                amount = Number(l.price * l.count);
            }
            else
            {
                Icons::Item(ActorName(app, l.rawItem), 20, ry + 4, 26);
                name = std::to_string(l.rawCount) + "x " + NameOf(app, l.rawItem);
                amount = l.group == 1 ? Number(l.price) : std::string("kept");
                color = l.group == 1 ? Gold : TextDim;
            }
            const std::string times = l.group == 0 && l.count > 1 ? "x" + std::to_string(l.count) : std::string();
            const float amountW = Width(amount, 0.38f);
            const float timesW = times.empty() ? 0.0f : Width(times, SmallText) + 10;
            const float after = Text(Fit(name, 0.38f, listW - 56 - amountW - timesW - 44), 56, baseline, 0.38f, TextColor);
            if (!times.empty())
                Text(times, after + 10, baseline, SmallText, TextDim);
            Text(amount, listW - 26, baseline, 0.38f, color, ALIGN_RIGHT);
        });
        SellDetail(app, lines[app.cursor[2]], listW + 32, top, ScreenW - listW - 64);
    }

    // The hidden level: the points and the two levels in cards, each family's rank in a small card, and the enemies
    // counted in tiles, a dot for each kill that counts.
    void ExpTab(App& app)
    {
        app.visible[3] = 0;
        app.gridColumns = 0;
        if (!app.level)
        {
            Text("This cook-data.txt has no level tables: make it again with the current gen_cook_data.py.", 28,
                 HeaderH + 60, 0.40f, Warn, ALIGN_LEFT, ScreenW - 56);
            return;
        }
        const LevelData& d = *app.level;
        const LevelPlan& p = *app.levelPlan;
        char buf[64];
        Stat points = MakeStat("Points, " + std::to_string(p.defeatedKinds) + " of " + std::to_string(d.flagCount) +
                                   " enemies",
                               Number(static_cast<int32_t>(p.points)), Gold, std::string(), 2.4f);
        points.bar = p.maxPoints > 0 ? p.points / p.maxPoints : 0.0f;
        points.note = "of " + Number(static_cast<int32_t>(p.maxPoints));
        snprintf(buf, sizeof(buf), "%.1f", p.enemyLevel);
        const std::string enemy = buf;
        snprintf(buf, sizeof(buf), "%.1f", p.weaponLevel);
        float y = StatCards({points, MakeStat("Enemy level", enemy, TextColor, std::string(), 1.0f),
                             MakeStat("Weapon level", buf, TextColor, std::string(), 1.0f)},
                            HeaderH + 14) +
                  12;

        // Families: the rank the enemy level reaches, and the way to the next one.
        const float left = 24.0f, gap = 10.0f, famH = 60.0f;
        const int families = std::max(1, d.speciesCount);
        const float famW = (ScreenW - 2 * left - gap * (families - 1)) / families;
        for (int s = 0; s < d.speciesCount; s++)
        {
            const Species& sp = d.species[s];
            const SpeciesState& st = p.species[s];
            const float fx = left + s * (famW + gap);
            RoundRect(fx, y, famW, famH, 10, CardColor);
            Icons::Enemy(sp.ranks[std::max(0, st.current)].actor, fx + 8, y + 8, 44);
            const std::string name = st.current >= 0 ? NameOf(app, Crc32(sp.ranks[st.current].actor)) : "No rank yet";
            Text(Fit(name, SmallText, famW - 68), fx + 60, y + 22, SmallText, st.current >= 0 ? TextColor : TextDim);
            if (st.current + 1 < sp.rankCount)
            {
                const float from = st.current >= 0 ? sp.ranks[st.current].value : 0.0f;
                const float to = sp.ranks[st.current + 1].value;
                const float f = to > from ? (p.enemyLevel - from) / (to - from) : 0.0f;
                const float bw = famW - 76;
                RoundRect(fx + 60, y + 30, bw, 6, 3, TrackColor);
                RoundRect(fx + 60, y + 30, std::max(6.0f, bw * std::min(1.0f, std::max(0.0f, f))), 6, 3,
                          glm::vec4(Gold, 1.0f));
                snprintf(buf, sizeof(buf), "next at %.0f", to);
                Text(Fit(buf, SmallText, famW - 68), fx + 60, y + 52, SmallText, TextDim);
            }
            else
                Text("top rank", fx + 60, y + 46, SmallText, Good);
        }
        y += famH + 12;

        // The enemies, in the order of the points they give (those whose counter is full last), four to a row.
        const int cols = 4;
        app.gridColumns = cols;
        const float tileW = (ScreenW - 2 * left - gap * (cols - 1)) / cols, tileH = 66.0f, step = tileH + 8;
        const int rows = static_cast<int>(app.levelRows.size());
        if (rows == 0)
            return;
        const int tileRows = (rows + cols - 1) / cols;
        const int visibleRows = std::max(1, static_cast<int>((ScreenH - FooterH - 6 - y + 8) / step));
        int& cursor = app.cursor[3];
        int& scroll = app.scroll[3];
        cursor = std::max(0, std::min(cursor, rows - 1));
        int cursorRow = cursor / cols;
        KeepInView(cursorRow, scroll, tileRows, visibleRows);
        for (int r = scroll; r < tileRows && r < scroll + visibleRows; r++)
            for (int c = 0; c < cols; c++)
            {
                const int i = r * cols + c;
                if (i >= rows)
                    break;
                const LevelRow& row = app.levelRows[i];
                const LevelFlag& f = d.flags[row.index];
                const LevelFlagState& fs = p.flags[row.index];
                const bool done = LevelRowDone(d, p, row.index);
                const float tx = left + c * (tileW + gap), ty = y + (r - scroll) * step;
                if (i == cursor)
                    SelectedRow(tx, ty, tileW, tileH, 10, Background);
                else
                    RoundRect(tx, ty, tileW, tileH, 10, done ? glm::vec4(1.0f, 1.0f, 1.0f, 0.02f) : CardColor);
                Icons::Enemy(f.actor, tx + 10, ty + 11, 44);
                Text(Fit(NameOf(app, Crc32(f.actor)), SmallText, tileW - 76), tx + 64, ty + 22, SmallText,
                     done ? TextDim : TextColor);
                // A dot for each kill that can count (10 for most enemies), filled for those done.
                const int slots = f.max > 0 ? std::min<int32_t>(f.max, 10) : 10;
                for (int k = 0; k < slots; k++)
                    RoundRect(tx + 64 + k * 13, ty + 30, 9, 9, 4.5f,
                              k < fs.kills ? glm::vec4(done ? TextDim : Gold, 1.0f) : TrackColor);
                snprintf(buf, sizeof(buf), "%g each", f.point);
                const float after = Text(Number(static_cast<int32_t>(fs.total)), tx + 64, ty + 57, SmallText,
                                         fs.kills > 0 && !done ? Gold : TextDim);
                Text(buf, after + 10, ty + 57, SmallText, TextDim);
            }
        ScrollBar(y, visibleRows * step - 8, tileRows, visibleRows, scroll);
    }

    void Message(const std::string& text, const glm::vec3& color)
    {
        Text(text, 80, 300, 0.48f, color, ALIGN_LEFT, ScreenW - 160);
    }

    int RowCount(const App& app)
    {
        switch (app.tab)
        {
        case 0: return static_cast<int>(app.goals.size());
        case 1: return static_cast<int>(app.rows.size());
        case 2: return static_cast<int>(SellLines(app).size());
        default: return static_cast<int>(app.levelRows.size());
        }
    }
}

// The work of one frame, shared by main() (the BotW Compendium) and by the BotW Companion (CompendiumSection).
namespace
{
    struct Runtime
    {
        App app;
        bool ok = false;
        int frame = 0; // frames drawn with a tab: the first says the save is being read
        int heldFrames = 0;
    };

    std::unique_ptr<Runtime> s_run;
    std::unique_ptr<Font> s_font;
    std::unique_ptr<ShapeBatch> s_shapes;

    void StartDrawing()
    {
        g_projection = glm::ortho(0.0f, ScreenW, 0.0f, ScreenH, -1.0f, 1.0f);
        s_font.reset(new Font());
        if (s_font->LoadSystem() != 1) // the console's own font: nothing in romfs
            Log("Font failed to load");
        s_font->m_ProjectionMatrix = &g_projection;
        s_font->m_ViewMatrix = &g_identity;
        g_font = s_font.get();
        s_shapes.reset(new ShapeBatch());
        g_shapes = s_shapes.get();
        Icons::Init();
        s_run.reset(new Runtime());
    }

    void StopDrawing()
    {
        Icons::Destroy();
        s_run.reset();
        s_shapes.reset();
        g_shapes = nullptr;
        if (s_font)
            s_font->Destroy();
        s_font.reset();
        g_font = nullptr;
    }

    // The buttons of one frame on the tabs. `changeTabs`: L/R and ZL/ZR change the tab (the BotW Compendium; in the
    // companion L and R change the section, outside).
    void TabInput(Runtime& r, u64 down, u64 held, bool changeTabs)
    {
        App& app = r.app;
        if (app.loading || !r.ok)
            return;
        if (changeTabs && (down & (HidNpadButton_R | HidNpadButton_ZR)))
            app.tab = (app.tab + 1) % 4;
        if (changeTabs && (down & (HidNpadButton_L | HidNpadButton_ZL)))
            app.tab = (app.tab + 3) % 4;
        // Up/Down on the d-pad or a stick, repeating while held; Left/Right a page at a time on the long lists.
        const u64 vertical = HidNpadButton_AnyUp | HidNpadButton_AnyDown;
        const u64 horizontal = HidNpadButton_AnyLeft | HidNpadButton_AnyRight;
        int move = 0;
        bool paged = false;
        if (held & (vertical | horizontal))
        {
            r.heldFrames++;
            const bool repeat = r.heldFrames > 18 && r.heldFrames % 4 == 0;
            if ((down & HidNpadButton_AnyDown) || (repeat && (held & HidNpadButton_AnyDown)))
                move = 1;
            if ((down & HidNpadButton_AnyUp) || (repeat && (held & HidNpadButton_AnyUp)))
                move = -1;
            // Not while a stick is also pushed up or down: a diagonal is not a page.
            if (!(held & vertical) && app.visible[app.tab] > 0)
            {
                const bool pageRepeat = r.heldFrames > 18 && r.heldFrames % 12 == 0;
                const int page = std::max(1, app.visible[app.tab] - 1);
                if ((down & HidNpadButton_AnyRight) || (pageRepeat && (held & HidNpadButton_AnyRight)))
                    move = page;
                if ((down & HidNpadButton_AnyLeft) || (pageRepeat && (held & HidNpadButton_AnyLeft)))
                    move = -page;
                paged = move != 0;
            }
            if (app.tab == 3 && app.gridColumns > 0)
            {
                move = 0;
                paged = false;
                if ((down & HidNpadButton_AnyDown) || (repeat && (held & HidNpadButton_AnyDown)))
                    move = app.gridColumns;
                if ((down & HidNpadButton_AnyUp) || (repeat && (held & HidNpadButton_AnyUp)))
                    move = -app.gridColumns;
                if ((down & HidNpadButton_AnyRight) || (repeat && (held & HidNpadButton_AnyRight)))
                    move = 1;
                if ((down & HidNpadButton_AnyLeft) || (repeat && (held & HidNpadButton_AnyLeft)))
                    move = -1;
            }
        }
        else
            r.heldFrames = 0;
        const int rows = RowCount(app);
        if (move && rows > 0)
        {
            // A page moves the window with the cursor (the list keeps both inside when it draws).
            if (paged)
                app.scroll[app.tab] += move;
            app.cursor[app.tab] = std::max(0, std::min(rows - 1, app.cursor[app.tab] + move));
        }
        // With room for one dish, A shows the other (the cheapest as strong, or the yellow-heart dish).
        if ((down & HidNpadButton_A) && app.tab == 0)
            app.dishPage = app.dishPage ? 0 : 1;
        // X and Y change what counts as free: the dishes and the sale are computed again, after a frame that says so.
        if ((down & HidNpadButton_X) && app.tab != 3)
        {
            app.countHidden = !app.countHidden;
            app.computing = true;
        }
        if ((down & HidNpadButton_Y) && app.tab == 0)
        {
            app.wholePouch = !app.wholePouch;
            app.computing = true;
        }
        if ((down & HidNpadButton_Y) && app.tab == 2 && app.prizedFree > 0)
        {
            app.sellPrized = !app.sellPrized;
            app.computing = true;
        }
    }

    // One frame drawn: the header, and with `content` the tab (or what is going on) and the footer. Shapes, icons and
    // text all go into queues: the shapes are drawn first, then the icons, then the text.
    void TabFrame(Runtime& r, bool content)
    {
        App& app = r.app;
        g_font->BeginBatch();
        Header(app);
        if (content)
        {
            if (app.loading)
                Message("Reading the save and cooking every combination...", TextDim);
            else if (!r.ok)
                Message(app.error, Warn);
            else if (app.tab == 0)
                DishesTab(app);
            else if (app.tab == 1)
                ReserveTab(app);
            else if (app.tab == 2)
                SellTab(app);
            else
                ExpTab(app);
            // After the tab: the hints depend on how many rows its list shows.
            Footer(app);
        }
        g_shapes->Render(g_projection);
        Icons::Flush(g_projection, ScreenH);
        g_font->RenderBatch();
    }

    // After the frame is on the screen: the save read (on the frame after the one that says so), or the dishes cooked
    // again.
    void AfterTabFrame(Runtime& r, bool content)
    {
        App& app = r.app;
        if (content && app.loading && r.frame > 0)
        {
            r.ok = Load(app);
            app.loading = false;
            if (!r.ok)
            {
                if (app.error.empty())
                    app.error = "The save could not be read";
                Log("Load failed:", app.error);
            }
        }
        else if (app.computing)
        {
            Compute(app);
            app.computing = false;
            Measure::Step("cooking again");
        }
        if (content)
            r.frame++;
        g_frame++;
    }
}

#ifdef BOTW_COMPANION
void CompendiumSection::Init()
{
    g_section = 0;
    StartDrawing();
    Measure::Step("Compendium: font and icons ready");
}

void CompendiumSection::Destroy()
{
    StopDrawing();
}

int CompendiumSection::Switch(int section, u64 down)
{
    if (down & HidNpadButton_R)
        section = (section + 1) % Sections;
    if (down & HidNpadButton_L)
        section = (section + Sections - 1) % Sections;
    return section;
}

void CompendiumSection::Frame(int section, u64 down, u64 held, const std::string& mapSource, const glm::vec3& mapDot,
                              bool mapHasDot)
{
    g_section = section;
    g_mapSource = mapSource;
    g_mapDot = mapDot;
    g_mapHasDot = mapHasDot;
    Runtime& r = *s_run;
    const bool content = section > 0;
    if (content)
    {
        r.app.tab = section - 1;
        TabInput(r, down, held, false);
    }
    TabFrame(r, content);
}

void CompendiumSection::AfterFrame(int section)
{
    AfterTabFrame(*s_run, section > 0);
}

void CompendiumSection::Reload(int section)
{
    // The tab and where the lists were stay; the rest is read and cooked again (the first frame says so).
    Runtime& r = *s_run;
    const int tab = r.app.tab;
    r.app = App();
    r.app.tab = tab;
    r.ok = false;
    r.frame = section > 0 ? 1 : 0;
    Log("Compendium: read again");
}

float CompendiumSection::HeaderHeight()
{
    return (HeaderH + 1.0f) * Zoom;
}
#else
int main()
{
    LogInit();
    Measure::Start();
    Log("START: botw-compendium");
    appletLockExit();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    if (!initEgl(nwindowGetDefault()))
    {
        Log("EGL failed");
        appletUnlockExit();
        return 1;
    }
    gladLoadGL();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    StartDrawing();
    Measure::Step("EGL, font and icons ready");

    Runtime& r = *s_run;
    while (appletMainLoop())
    {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        const u64 held = padGetButtons(&pad);
        if (down & HidNpadButton_Plus)
            break;
        TabInput(r, down, held, true);
        glClearColor(Background.r, Background.g, Background.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        TabFrame(r, true);
        eglSwapBuffers(s_display, s_surface);
        AfterTabFrame(r, true);
    }

    Measure::Step("exit");
    Log("SHUTDOWN");
    StopDrawing();
    deinitEgl();
    appletUnlockExit();
    return 0;
}
#endif
