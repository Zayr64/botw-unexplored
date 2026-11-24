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
#include "Graphics/InitGL.h"
#include "Graphics/ShapeBatch.h"
#include "Log.h"

#include "Cook.h"
#include "CookData.h"
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
    const float ScreenW = 1280.0f;
    const float ScreenH = 720.0f;
    const float HeaderH = 64.0f;
    const float FooterH = 46.0f;
    // The font is rasterised at 48 px: 0.36 is 17 px, the smallest text on the screen (read from a sofa, or on the
    // console's own 7 inch screen).
    const float SmallText = 0.36f;

    const glm::vec4 Background(0.035f, 0.05f, 0.06f, 1.0f);
    const glm::vec4 PanelColor(0.055f, 0.08f, 0.095f, 1.0f);
    const glm::vec4 BarColor(0.03f, 0.045f, 0.055f, 1.0f);
    const glm::vec4 Hairline(1.0f, 1.0f, 1.0f, 0.10f);
    const glm::vec4 RowOn(0.17f, 0.36f, 0.48f, 0.92f);
    const glm::vec4 RowAlt(1.0f, 1.0f, 1.0f, 0.03f);
    const glm::vec3 TextColor(1.0f, 1.0f, 1.0f);
    const glm::vec3 TextDim(0.70f, 0.75f, 0.78f);
    const glm::vec3 Accent(0.38f, 0.82f, 0.96f);
    const glm::vec3 Gold(0.86f, 0.73f, 0.40f);
    const glm::vec3 Good(0.47f, 0.86f, 0.56f);
    const glm::vec3 Warn(0.99f, 0.76f, 0.33f);
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

    std::string SaveDate(uint32_t t)
    {
        if (t == 0)
            return "";
        // The game writes its local time as if it were UTC.
        const time_t tt = static_cast<time_t>(t);
        struct tm parts;
        gmtime_r(&tt, &parts);
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d", parts.tm_mday, parts.tm_mon + 1, parts.tm_hour, parts.tm_min);
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
        std::string name; // "" while hidden
        int reveal = 0;   // 0 nothing shown, 1 what the hidden part is for, 2 the name too
    };

    struct LevelRow
    {
        int index;      // in LevelData::flags
        int reveal = 0; // for an enemy not defeated yet: 1 shows its name
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
        bool countHidden = true; // the tiers for armor not owned, the Ancient Oven and quests
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
    void ScrollBar(float top, float height, int rows, int visible, int scroll)
    {
        if (rows <= visible)
            return;
        const float x = ScreenW - 9, w = 5;
        RoundRect(x, top, w, height, 2.5f, glm::vec4(1.0f, 1.0f, 1.0f, 0.10f));
        const float thumb = std::max(28.0f, height * visible / rows);
        RoundRect(x, top + (height - thumb) * scroll / (rows - visible), w, thumb, 2.5f, glm::vec4(Accent, 0.95f));
    }

    // "3 / 62": the cursor's row and how many there are, next to the name of a list's first column.
    std::string Position(int cursor, int rows)
    {
        return rows > 0 ? std::to_string(cursor + 1) + " / " + std::to_string(rows) : std::string();
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

    // The actor name, for the item's icon; "" when unknown.
    std::string ActorName(const App& app, uint32_t hash)
    {
        const Actor* a = app.cook->FindActor(hash);
        return a ? a->name : "";
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
                g.result = FindBestForEffect(*app.cook, stock.data(), n, g.effect);
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

        app.cook.reset(new CookData());
        app.reserve.reset(new ReserveData());
        char error[160] = {};
        if (!app.cook->Load(text.data(), text.size(), error, sizeof(error)) ||
            !app.reserve->Load(text.data(), text.size(), error, sizeof(error)))
        {
            app.error = std::string("cook-data.txt: ") + error;
            return false;
        }

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

        app.plan.reset(new ReservePlan());
        PlanReserve(*app.cook, *app.reserve, app.save, app.pouch.data(), app.pouchCount, *app.plan);
        {
            std::unique_ptr<LevelData> level(new LevelData());
            char levelError[160] = {};
            if (level->Load(text.data(), text.size(), levelError, sizeof(levelError)))
            {
                app.level = std::move(level);
                app.levelPlan.reset(new LevelPlan());
                PlanLevel(*app.level, app.save, *app.levelPlan);
                for (int i = 0; i < app.level->flagCount; i++)
                    app.levelRows.push_back({i, 0});
                // Defeated enemies first, by the points they brought; then the others, by their points.
                const LevelPlan& lp = *app.levelPlan;
                const LevelData& ld = *app.level;
                std::stable_sort(app.levelRows.begin(), app.levelRows.end(), [&](const LevelRow& a, const LevelRow& b) {
                    const bool da = lp.flags[a.index].kills > 0, db = lp.flags[b.index].kills > 0;
                    if (da != db)
                        return da;
                    if (da)
                        return lp.flags[a.index].total > lp.flags[b.index].total;
                    return ld.flags[a.index].point < ld.flags[b.index].point;
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

        // Reserve rows: named materials alphabetically, then the ones not found yet.
        for (int i = 0; i < app.plan->materialCount; i++)
        {
            const MaterialPlan& m = app.plan->materials[i];
            if (m.Needed(AllTiers) == 0)
                continue;
            MaterialRow row;
            row.index = i;
            if (m.everObtained)
                row.name = NameOf(app, m.item);
            app.rows.push_back(row);
        }
        std::stable_sort(app.rows.begin(), app.rows.end(), [](const MaterialRow& a, const MaterialRow& b) {
            if (a.name.empty() != b.name.empty())
                return !a.name.empty();
            return a.name < b.name;
        });

        Compute(app);
        Log("Computed dishes, combinations:", app.evaluated);
        return true;
    }

    // ---- Drawing ----

    int RowCount(const App& app);

    void Header(const App& app)
    {
        Rect(0, 0, ScreenW, HeaderH, BarColor);
        Rect(0, HeaderH, ScreenW, 1, Hairline);
        const std::string title = "BotW Compendium";
        Text(title, 28, 42, 0.62f, Gold);
        const char* tabs[] = {"Dishes", "Reserve", "Sell", "EXP"};
        float x = 28 + Width(title, 0.62f) + 36;
        for (int i = 0; i < 4; i++)
        {
            const float w = Width(tabs[i], 0.46f) + 36;
            if (i == app.tab)
                RoundRect(x, 14, w, 36, 10, RowOn);
            Text(tabs[i], x + w / 2, 40, 0.46f, i == app.tab ? TextColor : TextDim, ALIGN_CENTER);
            x += w + 8;
        }
        if (app.plan)
        {
            const std::string rupees = Number(app.plan->rupees);
            const float w = Width(rupees, 0.46f);
            Text(rupees, ScreenW - 28, 41, 0.46f, TextColor, ALIGN_RIGHT);
            Icons::Ui("rupee", ScreenW - 28 - w - 40, 16, 30);
            if (app.computing)
                Text("Cooking again...", ScreenW - 28 - w - 64, 41, SmallText, Accent, ALIGN_RIGHT);
        }
    }

    int32_t HiddenNeed(const MaterialPlan& m)
    {
        return m.need[ArmorNotOwned] + m.need[Crafted] + m.need[Quests];
    }

    // What A shows next on a Reserve row: 1 what the hidden part is for, 2 the name of a material not found yet,
    // 0 nothing more.
    int NextReveal(const App& app, const MaterialRow& row)
    {
        if (row.reveal == 0 && HiddenNeed(app.plan->materials[row.index]) > 0)
            return 1;
        if (row.name.empty() && row.reveal < 2)
            return 2;
        return 0;
    }

    struct Hint
    {
        std::string key;
        std::string label;
    };

    // What the buttons do right now: only the ones that do something on this tab and on the selected row.
    std::vector<Hint> Hints(const App& app)
    {
        std::vector<Hint> hints;
        if (!app.loading && app.error.empty())
        {
            hints.push_back({"L/R", "tab"});
            hints.push_back({"Up/Down", "move"});
            if (app.visible[app.tab] > 0 && RowCount(app) > app.visible[app.tab])
                hints.push_back({"Left/Right", "page"});
            if (app.tab == 1 && !app.rows.empty())
            {
                const int next = NextReveal(app, app.rows[app.cursor[1]]);
                if (next)
                    hints.push_back({"A", next == 1 ? "what for" : "name"});
            }
            if (app.tab == 3 && app.levelPlan && !app.levelRows.empty())
            {
                const LevelRow& row = app.levelRows[app.cursor[3]];
                if (row.reveal == 0 && app.levelPlan->flags[row.index].kills == 0)
                    hints.push_back({"A", "name"});
            }
            if (app.tab == 0)
                hints.push_back({"Y", app.wholePouch ? "ingredients: whole pouch" : "ingredients: free only"});
            if (app.tab == 2 && app.prizedFree > 0)
                hints.push_back({"Y", app.sellPrized ? "valuable critters: sold" : "valuable critters: kept"});
            if (app.tab != 3)
                hints.push_back({"X", app.countHidden ? "hidden needs: on" : "hidden needs: off"});
        }
        hints.push_back({"+", "exit"});
        return hints;
    }

    // The hints on the right of the footer: each button on a key cap, what it does in grey. Returns where they start.
    float DrawHints(const std::vector<Hint>& hints)
    {
        const float scale = SmallText;
        const float pad = 8.0f;
        const float keyGap = 8.0f;
        const float gap = 20.0f;
        float total = 0.0f;
        for (const Hint& h : hints)
            total += Width(h.key, scale) + 2 * pad + keyGap + Width(h.label, scale) + gap;
        total -= gap;
        const float start = ScreenW - 24 - total;
        const float baseline = ScreenH - 16;
        float x = start;
        for (const Hint& h : hints)
        {
            const float w = Width(h.key, scale);
            RoundRect(x, ScreenH - FooterH + 10, w + 2 * pad, FooterH - 20, 7, glm::vec4(1.0f, 1.0f, 1.0f, 0.16f));
            Text(h.key, x + pad, baseline, scale, TextColor);
            x += w + 2 * pad + keyGap;
            x = Text(h.label, x, baseline, scale, TextDim) + gap;
        }
        return start;
    }

    void Footer(const App& app)
    {
        Rect(0, ScreenH - FooterH, ScreenW, FooterH, BarColor);
        Rect(0, ScreenH - FooterH, ScreenW, 1, Hairline);
        const float hintsStart = DrawHints(Hints(app));
        // Where the numbers come from. With BotW open the save cannot be read: say that it is a copy, and from when.
        // The long form when it fits beside the hints, else the short one.
        std::string source, brief;
        glm::vec3 color = TextDim;
        if (app.loaded.kind != SaveSource::Kind::None)
        {
            const std::string date = SaveDate(app.loaded.saveTime);
            if (app.loaded.kind == SaveSource::Kind::Game)
            {
                source = "Save read from the game" + (date.empty() ? std::string() : ", " + date);
                brief = date.empty() ? std::string("Save from the game") : "Save of " + date;
            }
            else
            {
                source = app.loaded.kind == SaveSource::Kind::UnexploredCopy ? "BotW is open: BotW Unexplored's copy"
                                                                              : "BotW is open: copy of the save";
                brief = "BotW is open: copy";
                if (!date.empty())
                {
                    source += " of " + date;
                    brief += " of " + date;
                }
                color = Warn;
            }
        }
        const float room = hintsStart - 24 - 28;
        Text(Width(source, SmallText) <= room ? source : Fit(brief, SmallText, room), 24, ScreenH - 16, SmallText,
             color);
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

    const float PartStep = 32.0f; // the height of an ingredient's line in a dish

    // From a dish's title to its last line (baselines).
    float DishHeight(const FoundDish& d)
    {
        return 130.0f + PartStep * Parts(d.items, d.itemCount).size();
    }

    // `top`: the baseline of the title.
    void DrawDish(const App& app, const FoundDish& d, float x, float top, float width, const std::string& title)
    {
        char name[96];
        DishName(*app.cook, d.result, name, sizeof(name));
        Text(Fit(title, SmallText, width), x, top, SmallText, Accent);
        // A long name gets smaller before it is cut.
        float nameScale = 0.54f;
        while (nameScale > 0.45f && Width(name, nameScale) > width)
            nameScale -= 0.02f;
        Text(Fit(name, nameScale, width), x, top + 34, nameScale, TextColor);

        const uint8_t mask = TierMask(app);
        const float step = PartStep;
        const float icon = 30.0f;
        const float scale = 0.40f;
        float y = top + 34 + step;
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
            const float after = Text(Fit(line, scale, width - 44 - heldWidth), x + 44, y, scale, TextColor);
            if (!held.empty())
                Text(held, after + 16, y, SmallText, Warn);
            y += step;
        }
        y += 38 - step;
        ResultLine(d.result, x, y, 0.42f);
        y += 32;
        // What the dish sells for, and what its ingredients would sell for as they are.
        Icons::Ui("rupee", x + 2, y - 20, 24);
        const float after = Text("Sells for " + Number(d.result.price), x + 32, y, 0.38f, TextColor);
        Text("ingredients worth " + Number(d.cost), after + 18, y, SmallText, TextDim);
        y += 26;
        Text(Fit(CritText(app, d), SmallText, width), x, y, SmallText, TextDim);
    }

    struct ShownDish
    {
        const FoundDish* dish;
        std::string title;
    };

    // One or two dishes on the right of the Dishes tab, side by side: two dishes of five different ingredients do
    // not fit one under the other, and this way the lines of one can be compared with the other's.
    void DrawDishes(const App& app, const std::vector<ShownDish>& shown, float x, float top, float width)
    {
        const float gap = 44;
        const float w = (width - gap) / 2;
        float tallest = 0.0f;
        for (size_t i = 0; i < shown.size() && i < 2; i++)
        {
            DrawDish(app, *shown[i].dish, x + i * (w + gap), top, w, shown[i].title);
            tallest = std::max(tallest, DishHeight(*shown[i].dish));
        }
        if (shown.size() > 1)
            Rect(x + w + gap / 2, top - 16, 1, tallest + 26, Hairline);
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

    void DishesTab(App& app)
    {
        const float listW = 400;
        const float top = HeaderH + 12;
        const float rowH = 46;
        app.visible[0] = 0;
        Rect(0, HeaderH + 1, listW, ScreenH - HeaderH - FooterH - 1, PanelColor);
        for (size_t i = 0; i < app.goals.size(); i++)
        {
            const Goal& g = app.goals[i];
            const float y = top + i * rowH;
            if (static_cast<int>(i) == app.cursor[0])
                RoundRect(12, y, listW - 24, rowH - 4, 8, RowOn);
            Icons::Ui(g.kind == GoalKind::Hearts ? "heart" : (g.effect == LifeMaxUp ? "heart" : EffectIcon(g.effect)), 18,
                      y + 1, 40, EffectTint(g.kind == GoalKind::Hearts ? EffectNone : g.effect));
            Text(g.label, 64, y + 29, 0.44f, TextColor);
            bool found = false;
            const std::string summary = Summary(app, g, found);
            Text(summary, listW - 28, y + 29, 0.38f, found ? Gold : TextDim, ALIGN_RIGHT);
        }

        const Goal& g = app.goals[app.cursor[0]];
        const float x = listW + 40;
        const float width = ScreenW - x - 40;
        Text(std::string(g.label) + " (" + g.detail + ")", x, top + 30, 0.40f, TextDim);
        if (app.wholePouch)
            Text("Whole pouch: may use what the Reserve tab holds back", ScreenW - 40, top + 30, SmallText, Warn,
                 ALIGN_RIGHT);
        const std::string nothing = app.wholePouch ? "Nothing in the pouch makes this."
                                                   : "Nothing free makes this. Y searches the whole pouch, X stops "
                                                     "counting hidden needs.";
        // The titles fit a column: half of the panel.
        std::vector<ShownDish> shown;
        switch (g.kind)
        {
        case GoalKind::Effect:
        {
            if (!g.result.found)
                break;
            shown.push_back({&g.result.best, "BEST"});
            const FoundDish& c = g.result.cheapest;
            if (c.itemCount > 0 && c.cost < g.result.best.cost)
                shown.push_back({&c, "CHEAPEST AS STRONG"});
            break;
        }
        case GoalKind::Hearts:
        {
            const std::string target = Hearts(HeartsTarget(app));
            const std::string all = Hearts(app.maxHearts);
            if (g.target.found)
                shown.push_back({&g.target.dish, g.target.reached ? "CHEAPEST TO HEAL " + target + " OF " + all +
                                                                        " HEARTS"
                                                                  : "NOTHING HEALS " + target + " HEARTS: THE MOST"});
            // Its result line says the rest: "and every heart".
            if (g.hearty.found)
                shown.push_back({&g.hearty.dish, "OR ANY YELLOW-HEART DISH"});
            break;
        }
        case GoalKind::Stamina:
        {
            const std::string wheels = Wheels(StaminaTarget(app), 1000);
            if (g.target.found)
                shown.push_back({&g.target.dish, g.target.reached ? "CHEAPEST TO REFILL YOUR " + wheels + " WHEELS"
                                                                  : "NOTHING REFILLS " + wheels + " WHEELS: THE MOST"});
            break;
        }
        }
        if (shown.empty())
            Text(nothing, x, top + 80, 0.42f, TextColor, ALIGN_LEFT, width);
        else
            DrawDishes(app, shown, x, top + 72, width);
    }

    void ReserveTab(App& app)
    {
        const ReservePlan& p = *app.plan;
        const uint8_t mask = TierMask(app);
        float y = HeaderH + 34;
        const int32_t own = p.rupeesNeeded[OwnedReachable] + p.rupeesNeeded[OwnedNextFairy];
        const int32_t hidden = p.rupeesNeeded[ArmorNotOwned] + p.rupeesNeeded[Crafted] + p.rupeesNeeded[Quests];
        Text("Great Fairies " + std::to_string(p.fairies) + " of 4   upgrades left on your armor: " +
                 std::to_string(p.ownedUpgradesLeft[0]) + " now, " + std::to_string(p.ownedUpgradesLeft[1]) +
                 " more that need other fairies",
             28, y, 0.38f, TextColor);
        y += 28;
        Icons::Ui("rupee", 26, y - 19, 24);
        Text("To keep: " + Number(own) + " for your armor and the fountains" +
                 (app.countHidden ? ", " + Number(hidden) + " hidden" : std::string(", hidden not counted")) +
                 "   (you have " + Number(p.rupees) + ")",
             56, y, 0.38f, p.rupees >= p.RupeesNeeded(mask) ? Good : Warn);
        y += 18;

        const int rows = static_cast<int>(app.rows.size());
        int& cursor = app.cursor[1];
        int& scroll = app.scroll[1];
        const float colHave = 640, colOwn = 800, colHidden = 960, colFree = 1100;
        Rect(0, y, ScreenW, 30, BarColor);
        const float afterName = Text("Material", 56, y + 21, SmallText, TextDim);
        Text(Position(cursor, rows), afterName + 20, y + 21, SmallText, TextDim);
        Text("Have", colHave, y + 21, SmallText, TextDim, ALIGN_RIGHT);
        Text("Your armor", colOwn, y + 21, SmallText, TextDim, ALIGN_RIGHT);
        Text("Hidden", colHidden, y + 21, SmallText, TextDim, ALIGN_RIGHT);
        Text("Free", colFree, y + 21, SmallText, TextDim, ALIGN_RIGHT);
        y += 32;

        const float rowH = 30;
        const float listBottom = ScreenH - FooterH - 70;
        const int visible = std::max(1, static_cast<int>((listBottom - y) / rowH));
        app.visible[1] = visible;
        if (rows == 0)
        {
            Text("Nothing to hold back: no armor, purchase or quest needs your materials.", 56, y + 30, 0.40f, TextDim);
            return;
        }
        KeepInView(cursor, scroll, rows, visible);
        for (int i = scroll; i < rows && i < scroll + visible; i++)
        {
            const MaterialRow& row = app.rows[i];
            const MaterialPlan& m = p.materials[row.index];
            const float ry = y + (i - scroll) * rowH;
            if (i == cursor)
                Rect(0, ry, ScreenW, rowH, RowOn);
            else if (i % 2)
                Rect(0, ry, ScreenW, rowH, RowAlt);
            const bool named = !row.name.empty() || row.reveal >= 2;
            const std::string name = !row.name.empty() ? row.name : (row.reveal >= 2 ? NameOf(app, m.item) : "Not found yet");
            if (named)
                Icons::Item(ActorName(app, m.item), 22, ry + 2, 26);
            Text(Fit(name, 0.38f, 400), 56, ry + 21, 0.38f, named ? TextColor : TextDim);
            Text(Number(m.have), colHave, ry + 21, 0.38f, TextColor, ALIGN_RIGHT);
            Text(Number(m.need[OwnedReachable] + m.need[OwnedNextFairy]), colOwn, ry + 21, 0.38f, TextColor, ALIGN_RIGHT);
            const int32_t h = HiddenNeed(m);
            Text(h ? Number(h) : "-", colHidden, ry + 21, 0.38f, app.countHidden ? TextColor : TextDim, ALIGN_RIGHT);
            const int32_t free = m.Free(mask);
            const int32_t shortBy = m.Needed(mask) - m.have;
            Text(Number(free), colFree, ry + 21, 0.38f, free > 0 ? Good : TextDim, ALIGN_RIGHT);
            if (shortBy > 0)
            {
                // What is missing for the armor in the pouch in orange; what only the hidden needs miss, in grey.
                const int32_t ownNeed = m.need[OwnedReachable] + m.need[OwnedNextFairy];
                const int32_t ownShort = std::min(shortBy, std::max(0, ownNeed - m.have));
                float sx = colFree + 24;
                if (ownShort > 0)
                    sx = Text("short " + Number(ownShort), sx, ry + 21, SmallText, Warn) + 8;
                if (shortBy > ownShort)
                    Text((ownShort > 0 ? "+" : "short ") + Number(shortBy - ownShort), sx, ry + 21, SmallText, TextDim);
            }
        }
        ScrollBar(y, visible * rowH, rows, visible, scroll);

        // What the selected row's hidden part is for: only said when A asks.
        const MaterialRow& row = app.rows[cursor];
        const MaterialPlan& m = p.materials[row.index];
        const float by = ScreenH - FooterH - 40;
        std::string line;
        if (HiddenNeed(m) == 0)
            line = "Nothing hidden: all for the armor you have";
        else if (row.reveal == 0)
            line = "A: what the hidden part is for";
        else
        {
            // Only the kinds this material is needed for.
            const struct
            {
                Tier tier;
                const char* what;
            } kinds[] = {{ArmorNotOwned, " for armor you do not have"},
                         {Crafted, " for the Ancient Oven"},
                         {Quests, " for side quests"}};
            for (const auto& k : kinds)
                if (m.need[k.tier] > 0)
                    line += (line.empty() ? "Hidden: " : ", ") + Number(m.need[k.tier]) + k.what;
        }
        if (NextReveal(app, row) == 2)
            line += "   (A: the name)";
        Text(line, 28, by, 0.38f, Accent);
    }

    struct SellLine
    {
        std::vector<Part> parts; // a pot; empty for a raw sale
        uint32_t rawItem = 0;
        int32_t rawCount = 0;
        int32_t price = 0;
        int32_t raw = 0;
        int count = 0; // identical pots
        bool kept = false; // a critter left out of the sale: nothing is sold
    };

    std::vector<SellLine> SellLines(const App& app)
    {
        const SalePlan& s = *app.sale;
        std::vector<SellLine> lines;
        for (int i = 0; i < s.potCount; i++)
        {
            const SalePot& pot = s.pots[i];
            if (!lines.empty() && lines.back().count > 0 && lines.back().price == pot.price && i > 0 &&
                memcmp(s.pots[i - 1].items, pot.items, sizeof(pot.items)) == 0)
            {
                lines.back().count++;
                continue;
            }
            SellLine l;
            l.parts = Parts(pot.items, pot.itemCount);
            l.price = pot.price;
            l.raw = pot.rawPrice;
            l.count = 1;
            lines.push_back(l);
        }
        for (int i = 0; i < s.rawCount; i++)
        {
            SellLine l;
            l.rawItem = s.raw[i].item;
            l.rawCount = s.raw[i].count;
            l.price = s.raw[i].price;
            l.raw = s.raw[i].price;
            lines.push_back(l);
        }
        for (int i = 0; i < s.keptCount; i++)
        {
            SellLine l;
            l.rawItem = s.kept[i].item;
            l.rawCount = s.kept[i].count;
            l.kept = true;
            lines.push_back(l);
        }
        return lines;
    }

    void SellTab(App& app)
    {
        const SalePlan& s = *app.sale;
        float y = HeaderH + 34;
        const int32_t total = s.cookedTotal + s.partsLeftRaw + s.rawTotal;
        Text("Elixirs: " + std::to_string(s.potCount) + " pots sell for " + Number(s.cookedTotal) +
                 " rupees (their ingredients sold raw: " + Number(s.cookedRawTotal) + ")",
             28, y, 0.40f, TextColor);
        y += 28;
        Text("Monster parts left with no critter to cook them: " + Number(s.partsLeft) + ", worth " +
                 Number(s.partsLeftRaw) + " raw.   Other items to sell raw: " + Number(s.rawTotal) + " rupees",
             28, y, 0.38f, TextColor);
        y += 28;
        Icons::Ui("rupee", 26, y - 20, 26);
        Text("All of it: " + Number(total) + " rupees   (" + Number(app.plan->rupees + total) + " with the " +
                 Number(app.plan->rupees) + " you have)",
             58, y, 0.40f, Gold);
        y += 14;

        const std::vector<SellLine> lines = SellLines(app);
        const int rows = static_cast<int>(lines.size());
        int& cursor = app.cursor[2];
        int& scroll = app.scroll[2];
        const float colEach = 1020, colRaw = 1140, colPots = 1240;
        Rect(0, y, ScreenW, 30, BarColor);
        const float afterName = Text("Cook and sell", 28, y + 21, SmallText, TextDim);
        Text(Position(cursor, rows), afterName + 20, y + 21, SmallText, TextDim);
        Text("Each pot", colEach, y + 21, SmallText, TextDim, ALIGN_RIGHT);
        Text("Raw", colRaw, y + 21, SmallText, TextDim, ALIGN_RIGHT);
        Text("Pots", colPots, y + 21, SmallText, TextDim, ALIGN_RIGHT);
        y += 32;
        const float rowH = 34;
        const int visible = std::max(1, static_cast<int>((ScreenH - FooterH - 16 - y) / rowH));
        app.visible[2] = visible;
        if (rows == 0)
        {
            Text("Nothing free to sell.", 28, y + 30, 0.40f, TextDim);
            return;
        }
        KeepInView(cursor, scroll, rows, visible);
        const float scale = 0.38f;
        for (int i = scroll; i < rows && i < scroll + visible; i++)
        {
            const SellLine& l = lines[i];
            const float ry = y + (i - scroll) * rowH;
            const float baseline = ry + 24;
            if (i == cursor)
                Rect(0, ry, ScreenW, rowH, RowOn);
            else if (i % 2)
                Rect(0, ry, ScreenW, rowH, RowAlt);
            if (l.count)
            {
                // Each ingredient: its icon and "4x"; the names too, for as many (from the first) as fit before the
                // prices.
                const float limit = colEach - 110;
                float needed = 24;
                for (const Part& part : l.parts)
                    needed += 34 + Width(std::to_string(part.count) + "x", scale) + 18;
                size_t named = 0;
                for (; named < l.parts.size(); named++)
                {
                    const float more = Width(std::string(" ") + NameOf(app, l.parts[named].item), scale) + 6;
                    if (needed + more > limit)
                        break;
                    needed += more;
                }
                float x = 24;
                for (size_t k = 0; k < l.parts.size(); k++)
                {
                    Icons::Item(ActorName(app, l.parts[k].item), x, ry + 3, 28);
                    std::string text = std::to_string(l.parts[k].count) + "x";
                    if (k < named)
                        text += std::string(" ") + NameOf(app, l.parts[k].item);
                    x = Text(text, x + 34, baseline, scale, TextColor) + (k < named ? 24 : 18);
                }
                Text(Number(l.raw), colRaw, baseline, scale, TextDim, ALIGN_RIGHT);
                Text(std::to_string(l.count) + "x", colPots, baseline, scale, TextColor, ALIGN_RIGHT);
            }
            else
            {
                Icons::Item(ActorName(app, l.rawItem), 24, ry + 3, 28);
                const std::string item = std::to_string(l.rawCount) + "x " + NameOf(app, l.rawItem);
                const float after = Text(item, 58, baseline, scale, TextColor);
                Text(l.kept ? "kept for cooking (Y sells them too)" : "sell as it is", after + 16, baseline, SmallText,
                     TextDim);
            }
            if (!l.kept)
                Text(Number(l.price), colEach, baseline, scale, Gold, ALIGN_RIGHT);
        }
        ScrollBar(y, visible * rowH, rows, visible, scroll);
    }

    // The hidden level: the points, the enemy and weapon levels, each family's rank, and the enemies counted.
    void ExpTab(App& app)
    {
        app.visible[3] = 0;
        if (!app.level)
        {
            Text("This cook-data.txt has no level tables: make it again with the current gen_cook_data.py.", 28,
                 HeaderH + 60, 0.40f, Warn, ALIGN_LEFT, ScreenW - 56);
            return;
        }
        const LevelData& d = *app.level;
        const LevelPlan& p = *app.levelPlan;
        char buf[160];
        float y = HeaderH + 34;
        snprintf(buf, sizeof(buf), "%s of %s points (%d of %d enemies)   enemy level %.1f   weapon level %.1f",
                 Number(static_cast<int32_t>(p.points)).c_str(), Number(static_cast<int32_t>(p.maxPoints)).c_str(),
                 p.defeatedKinds, d.flagCount, p.enemyLevel, p.weaponLevel);
        Text(buf, 28, y, 0.40f, Gold);
        y += 12;

        // Families: the rank the level reaches and the way to the next one.
        const float rowH = 34;
        for (int s = 0; s < d.speciesCount; s++)
        {
            const Species& sp = d.species[s];
            const SpeciesState& st = p.species[s];
            const float ry = y + s * rowH;
            if (s % 2)
                Rect(0, ry, ScreenW, rowH, RowAlt);
            // The family by the name of its lowest rank the player has defeated.
            const std::string family = st.knownRank >= 0 ? NameOf(app, Crc32(sp.ranks[st.knownRank].actor)) : "Unknown family";
            if (st.knownRank >= 0)
                Icons::Enemy(sp.ranks[st.currentKnown ? st.current : st.knownRank].actor, 22, ry + 1, 32);
            Text(Fit(family, 0.38f, 226), 62, ry + 24, 0.38f, st.knownRank >= 0 ? TextColor : TextDim);
            std::string rank;
            if (st.current < 0)
                rank = "below the first rank";
            else if (st.currentKnown)
                rank = NameOf(app, Crc32(sp.ranks[st.current].actor));
            else
                rank = "rank " + std::to_string(st.current + 1) + " of " + std::to_string(sp.rankCount);
            Text(Fit("now: " + rank, SmallText, 326), 300, ry + 24, SmallText, TextColor);
            if (st.current + 1 < sp.rankCount)
            {
                const float from = st.current >= 0 ? sp.ranks[st.current].value : 0.0f;
                const float to = sp.ranks[st.current + 1].value;
                const float f = to > from ? (p.enemyLevel - from) / (to - from) : 0.0f;
                const float bx = 640, bw = 250;
                RoundRect(bx, ry + 12, bw, 10, 5, glm::vec4(1.0f, 1.0f, 1.0f, 0.12f));
                RoundRect(bx, ry + 12, std::max(10.0f, bw * std::min(1.0f, std::max(0.0f, f))), 10, 5,
                          glm::vec4(Gold, 1.0f));
                snprintf(buf, sizeof(buf), "%.1f / %.0f", p.enemyLevel, to);
                Text(buf, bx + bw + 16, ry + 24, SmallText, TextDim);
                Text(Fit(st.nextKnown ? "next: " + std::string(NameOf(app, Crc32(sp.ranks[st.current + 1].actor)))
                                      : "next: hidden",
                         SmallText, 250),
                     ScreenW - 28, ry + 24, SmallText, TextDim, ALIGN_RIGHT);
            }
            else
                Text("top rank", 640, ry + 24, SmallText, Good);
        }
        y += d.speciesCount * rowH + 10;

        const int rows = static_cast<int>(app.levelRows.size());
        int& cursor = app.cursor[3];
        int& scroll = app.scroll[3];
        Rect(0, y, ScreenW, 30, BarColor);
        const float afterName = Text("Enemy", 60, y + 21, SmallText, TextDim);
        Text(Position(cursor, rows), afterName + 20, y + 21, SmallText, TextDim);
        Text("Defeated", 760, y + 21, SmallText, TextDim, ALIGN_RIGHT);
        Text("Points each", 960, y + 21, SmallText, TextDim, ALIGN_RIGHT);
        Text("Points", 1150, y + 21, SmallText, TextDim, ALIGN_RIGHT);
        y += 32;
        const float lineH = 30;
        const int visible = std::max(1, static_cast<int>((ScreenH - FooterH - 10 - y) / lineH));
        app.visible[3] = visible;
        if (rows == 0)
            return;
        KeepInView(cursor, scroll, rows, visible);
        for (int i = scroll; i < rows && i < scroll + visible; i++)
        {
            const LevelRow& row = app.levelRows[i];
            const LevelFlag& f = d.flags[row.index];
            const LevelFlagState& fs = p.flags[row.index];
            const float ry = y + (i - scroll) * lineH;
            if (i == cursor)
                Rect(0, ry, ScreenW, lineH, RowOn);
            else if (i % 2)
                Rect(0, ry, ScreenW, lineH, RowAlt);
            const bool named = fs.kills > 0 || row.reveal > 0;
            const char* label = NameOf(app, Crc32(f.actor));
            if (named)
                Icons::Enemy(f.actor, 24, ry + 1, 28);
            Text(named ? Fit(label, 0.38f, 560) : std::string("Not defeated yet"), 60, ry + 21, 0.38f,
                 named ? TextColor : TextDim);
            // Each counter stops at its limit (10 for most enemies): further kills add nothing.
            const std::string defeated = Number(fs.kills) + (f.max > 0 ? " / " + Number(f.max) : std::string());
            Text(defeated, 760, ry + 21, 0.38f, fs.kills == 0 ? TextDim : (f.max > 0 && fs.kills >= f.max ? Good : TextColor),
                 ALIGN_RIGHT);
            snprintf(buf, sizeof(buf), "%g", f.point);
            Text(buf, 960, ry + 21, 0.38f, TextDim, ALIGN_RIGHT);
            Text(Number(static_cast<int32_t>(fs.total)), 1150, ry + 21, 0.38f, fs.kills ? Gold : TextDim, ALIGN_RIGHT);
        }
        ScrollBar(y, visible * lineH, rows, visible, scroll);
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

int main()
{
    LogInit();
    Log("START: botw-compendium");
    appletLockExit();
    const bool romfs = R_SUCCEEDED(romfsInit());
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    if (!initEgl(nwindowGetDefault()))
    {
        Log("EGL failed");
        if (romfs)
            romfsExit();
        appletUnlockExit();
        return 1;
    }
    gladLoadGL();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    g_projection = glm::ortho(0.0f, ScreenW, 0.0f, ScreenH, -1.0f, 1.0f);
    Font font;
    if (font.Load("romfs:/arial.ttf") != 1)
        Log("Font failed to load");
    font.m_ProjectionMatrix = &g_projection;
    font.m_ViewMatrix = &g_identity;
    g_font = &font;
    std::unique_ptr<ShapeBatch> shapes(new ShapeBatch());
    g_shapes = shapes.get();
    Icons::Init();

    App app;
    bool ok = false;
    int frame = 0;
    int heldFrames = 0;
    while (appletMainLoop())
    {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        const u64 held = padGetButtons(&pad);
        if (down & HidNpadButton_Plus)
            break;

        if (!app.loading && ok)
        {
            if (down & (HidNpadButton_R | HidNpadButton_ZR))
                app.tab = (app.tab + 1) % 4;
            if (down & (HidNpadButton_L | HidNpadButton_ZL))
                app.tab = (app.tab + 3) % 4;
            // Up/Down on the d-pad or a stick, repeating while held; Left/Right a page at a time on the long lists.
            const u64 vertical = HidNpadButton_AnyUp | HidNpadButton_AnyDown;
            const u64 horizontal = HidNpadButton_AnyLeft | HidNpadButton_AnyRight;
            int move = 0;
            bool paged = false;
            if (held & (vertical | horizontal))
            {
                heldFrames++;
                const bool repeat = heldFrames > 18 && heldFrames % 4 == 0;
                if ((down & HidNpadButton_AnyDown) || (repeat && (held & HidNpadButton_AnyDown)))
                    move = 1;
                if ((down & HidNpadButton_AnyUp) || (repeat && (held & HidNpadButton_AnyUp)))
                    move = -1;
                // Not while a stick is also pushed up or down: a diagonal is not a page.
                if (!(held & vertical) && app.visible[app.tab] > 0)
                {
                    const bool pageRepeat = heldFrames > 18 && heldFrames % 12 == 0;
                    const int page = std::max(1, app.visible[app.tab] - 1);
                    if ((down & HidNpadButton_AnyRight) || (pageRepeat && (held & HidNpadButton_AnyRight)))
                        move = page;
                    if ((down & HidNpadButton_AnyLeft) || (pageRepeat && (held & HidNpadButton_AnyLeft)))
                        move = -page;
                    paged = move != 0;
                }
            }
            else
                heldFrames = 0;
            const int rows = RowCount(app);
            if (move && rows > 0)
            {
                // A page moves the window with the cursor (the list keeps both inside when it draws).
                if (paged)
                    app.scroll[app.tab] += move;
                app.cursor[app.tab] = std::max(0, std::min(rows - 1, app.cursor[app.tab] + move));
            }
            if ((down & HidNpadButton_A) && app.tab == 3 && !app.levelRows.empty())
                app.levelRows[app.cursor[3]].reveal = 1;
            if ((down & HidNpadButton_A) && app.tab == 1 && !app.rows.empty())
            {
                MaterialRow& row = app.rows[app.cursor[1]];
                const int next = NextReveal(app, row);
                if (next)
                    row.reveal = next;
            }
            // X and Y change what counts as free: the dishes and the sale are computed again, after a frame that
            // says so.
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

        // Shapes, icons and text all go into queues: the shapes are drawn first, then the icons, then the text.
        glClearColor(Background.r, Background.g, Background.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        font.BeginBatch();
        Header(app);
        if (app.loading)
            Message("Reading the save and cooking every combination...", TextDim);
        else if (!ok)
            Message(app.error, Warn);
        else
        {
            if (app.tab == 0)
                DishesTab(app);
            else if (app.tab == 1)
                ReserveTab(app);
            else if (app.tab == 2)
                SellTab(app);
            else
                ExpTab(app);
        }
        // After the tab: the hints depend on how many rows its list shows.
        Footer(app);
        g_shapes->Render(g_projection);
        Icons::Flush(g_projection, ScreenH);
        font.RenderBatch();
        eglSwapBuffers(s_display, s_surface);

        if (app.loading && frame > 0)
        {
            ok = Load(app);
            app.loading = false;
            if (!ok)
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
        }
        frame++;
    }

    Log("SHUTDOWN");
    Icons::Destroy();
    shapes.reset();
    font.Destroy();
    deinitEgl();
    if (romfs)
        romfsExit();
    appletUnlockExit();
    return 0;
}
