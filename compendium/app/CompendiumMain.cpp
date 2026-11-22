// BotW Compendium: the best dishes from what is in the pouch, what to hold back for armor and quests, and what to
// sell.
//
// Three tabs (L/R): Dishes, Reserve, Sell. Everything is computed once from the save; X and Y change what counts as
// free and compute again. Nothing is ever written to the game's save.

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

using namespace Compendium;

namespace
{
    const float ScreenW = 1280.0f;
    const float ScreenH = 720.0f;
    const float HeaderH = 64.0f;
    const float FooterH = 46.0f;

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
    // Tints of the game's white icons, as the game shows them.
    const glm::vec4 White(1.0f, 1.0f, 1.0f, 1.0f);
    const glm::vec4 HeartRed(0.93f, 0.25f, 0.28f, 1.0f);
    const glm::vec4 HeartYellow(0.98f, 0.82f, 0.30f, 1.0f);
    const glm::vec4 StaminaGreen(0.45f, 0.85f, 0.40f, 1.0f);

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
        case GutsRecover: return StaminaGreen;
        case ExGutsMaxUp: return HeartYellow;
        default: return White;
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
        bool countHidden = true; // the tiers for armor not owned, the Ancient Oven and quests
        bool wholePouch = false; // dishes from everything in the pouch, not only what is free
        int tab = 0;
        int cursor[3] = {0, 0, 0};
        int scroll[3] = {0, 0, 0};
        std::string error;
        int evaluated = 0;
    };

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
        case LifeMaxUp: return "+" + std::to_string(r.vitality / 4) + " yellow hearts";
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
                 *app.sale);
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

    void Header(const App& app)
    {
        Rect(0, 0, ScreenW, HeaderH, BarColor);
        Rect(0, HeaderH, ScreenW, 1, Hairline);
        const std::string title = "BotW Compendium";
        Text(title, 28, 42, 0.62f, Gold);
        const char* tabs[] = {"Dishes", "Reserve", "Sell"};
        float x = 28 + Width(title, 0.62f) + 36;
        for (int i = 0; i < 3; i++)
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
            Icons::Ui("rupee", ScreenW - 28 - w - 34, 16, 30);
        }
    }

    void Footer(const App& app)
    {
        Rect(0, ScreenH - FooterH, ScreenW, FooterH, BarColor);
        Rect(0, ScreenH - FooterH, ScreenW, 1, Hairline);
        std::string source;
        if (app.loaded.kind != SaveSource::Kind::None)
        {
            source = SaveSource::KindText(app.loaded.kind);
            const std::string date = SaveDate(app.loaded.saveTime);
            if (!date.empty())
                source += ", " + date;
        }
        Text(Fit(source, 0.34f, 560), 24, ScreenH - 16, 0.34f, app.loaded.kind == SaveSource::Kind::Game ? TextDim : Warn);
        std::string hints = "L/R tab   Up/Down move   ";
        if (app.tab == 1)
            hints += "A what for   ";
        hints += app.countHidden ? "X hidden needs: on   " : "X hidden needs: off   ";
        if (app.tab == 0)
            hints += app.wholePouch ? "Y whole pouch   " : "Y free only   ";
        hints += "+ exit";
        Text(hints, ScreenW - 24, ScreenH - 16, 0.34f, TextDim, ALIGN_RIGHT);
    }

    // The result of a dish on one line, with the game's icons: effect, hearts. Returns the x after it.
    float ResultLine(const CookResult& r, float x, float baseline, float scale)
    {
        const float icon = 28;
        const float top = baseline - 22;
        if (r.effect != EffectNone)
        {
            Icons::Ui(r.effect == LifeMaxUp ? "heart" : EffectIcon(r.effect), x, top, icon, EffectTint(r.effect));
            x = Text(Strength(r), x + icon + 6, baseline, scale, Gold) + 18;
            if (r.effect == LifeMaxUp)
                return Text("and every heart", x, baseline, scale * 0.9f, TextDim);
        }
        Icons::Ui("heart", x, top, icon, HeartRed);
        return Text(Hearts(r.life), x + icon + 6, baseline, scale, Gold);
    }

    void DrawDish(const App& app, const FoundDish& d, float x, float top, float width, const std::string& title)
    {
        char name[96];
        DishName(*app.cook, d.result, name, sizeof(name));
        Text(title, x, top, 0.34f, Accent);
        Text(Fit(name, 0.54f, width), x, top + 34, 0.54f, TextColor);
        float y = top + 66;
        for (const Part& p : Parts(d.items, d.itemCount))
        {
            Icons::Item(ActorName(app, p.item), x + 4, y - 22, 30);
            Text(std::to_string(p.count) + "x " + NameOf(app, p.item), x + 44, y, 0.40f, TextColor);
            y += 32;
        }
        y += 6;
        ResultLine(d.result, x, y, 0.42f);
        y += 32;
        Icons::Ui("rupee", x + 2, y - 20, 24);
        float after = Text(Number(d.result.price), x + 32, y, 0.38f, TextColor);
        Text("   ingredients worth " + Number(d.cost), after, y, 0.36f, TextDim);
        y += 26;
        const int chance = std::min(100, static_cast<int>(d.result.critChance));
        std::string crit = "Critical " + std::to_string(chance) + "%";
        std::vector<std::string> options;
        if (d.result.critBonuses & static_cast<uint8_t>(CritBonus::Life))
            options.push_back("+3 hearts");
        if (d.result.critBonuses & static_cast<uint8_t>(CritBonus::Vitality))
            options.push_back(d.result.effect == LifeMaxUp ? "+1 yellow heart" : "+1 level");
        if (d.result.critBonuses & static_cast<uint8_t>(CritBonus::Time))
            options.push_back("+5:00");
        for (size_t i = 0; i < options.size(); i++)
            crit += (i == 0 ? ": " : " or ") + options[i];
        Text(crit, x, y, 0.36f, TextDim);
    }

    // The short result in the list on the left.
    std::string Summary(const App& app, const Goal& g, bool& found)
    {
        (void)app;
        switch (g.kind)
        {
        case GoalKind::Effect:
            found = g.result.found;
            return found ? Strength(g.result.best.result) : "nothing free";
        case GoalKind::Hearts:
            found = g.target.found || g.hearty.found;
            if (g.target.found)
                return Hearts(g.target.dish.result.life) + " hearts";
            return g.hearty.found ? "hearty dish" : "nothing free";
        case GoalKind::Stamina:
            found = g.target.found;
            return found ? Strength(g.target.dish.result) : "nothing free";
        }
        found = false;
        return "";
    }

    void DishesTab(App& app)
    {
        const float listW = 400;
        const float top = HeaderH + 12;
        const float rowH = 46;
        Rect(0, HeaderH + 1, listW, ScreenH - HeaderH - FooterH - 1, PanelColor);
        for (size_t i = 0; i < app.goals.size(); i++)
        {
            const Goal& g = app.goals[i];
            const float y = top + i * rowH;
            if (static_cast<int>(i) == app.cursor[0])
                RoundRect(12, y, listW - 24, rowH - 4, 8, RowOn);
            Icons::Ui(g.kind == GoalKind::Hearts ? "heart" : (g.effect == LifeMaxUp ? "heart" : EffectIcon(g.effect)), 22,
                      y + 6, 30, EffectTint(g.kind == GoalKind::Hearts ? EffectNone : g.effect));
            Text(g.label, 62, y + 29, 0.44f, TextColor);
            bool found = false;
            const std::string summary = Summary(app, g, found);
            Text(summary, listW - 28, y + 29, 0.38f, found ? Gold : TextDim, ALIGN_RIGHT);
        }

        const Goal& g = app.goals[app.cursor[0]];
        const float x = listW + 40;
        const float width = ScreenW - x - 40;
        Text(std::string(g.label) + " (" + g.detail + ")", x, top + 30, 0.40f, TextDim);
        const std::string nothing = app.wholePouch ? "Nothing in the pouch makes this."
                                                   : "Nothing free makes this. Y searches the whole pouch, X stops "
                                                     "counting hidden needs.";
        switch (g.kind)
        {
        case GoalKind::Effect:
        {
            if (!g.result.found)
            {
                Text(nothing, x, top + 80, 0.42f, TextColor, ALIGN_LEFT, width);
                return;
            }
            DrawDish(app, g.result.best, x, top + 72, width, "BEST");
            const FoundDish& c = g.result.cheapest;
            if (c.itemCount > 0 && c.cost < g.result.best.cost)
                DrawDish(app, c, x, top + 72 + 300, width, "CHEAPEST AS STRONG");
            break;
        }
        case GoalKind::Hearts:
        {
            const std::string target = Hearts(HeartsTarget(app));
            const std::string all = Hearts(app.maxHearts);
            float y = top + 72;
            if (g.target.found)
            {
                DrawDish(app, g.target.dish, x, y, width,
                         g.target.reached ? "CHEAPEST TO HEAL " + target + " OF YOUR " + all + " HEARTS"
                                          : "NOTHING HEALS " + target + " HEARTS: THE MOST");
                y += 300;
            }
            if (g.hearty.found)
                DrawDish(app, g.hearty.dish, x, y, width, "OR ANY YELLOW-HEART DISH: IT HEALS EVERY HEART");
            if (!g.target.found && !g.hearty.found)
                Text(nothing, x, top + 80, 0.42f, TextColor, ALIGN_LEFT, width);
            break;
        }
        case GoalKind::Stamina:
        {
            const std::string wheels = Wheels(StaminaTarget(app), 1000);
            if (!g.target.found)
            {
                Text(nothing, x, top + 80, 0.42f, TextColor, ALIGN_LEFT, width);
                return;
            }
            DrawDish(app, g.target.dish, x, top + 72, width,
                     g.target.reached ? "CHEAPEST TO REFILL YOUR " + wheels + " WHEELS"
                                      : "NOTHING REFILLS " + wheels + " WHEELS: THE MOST");
            break;
        }
        }
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
                 " with more fairies",
             28, y, 0.38f, TextColor);
        y += 28;
        Icons::Ui("rupee", 26, y - 19, 24);
        Text("To keep: " + Number(own) + " for your armor and the fountains" +
                 (app.countHidden ? ", " + Number(hidden) + " hidden" : std::string(", hidden not counted")) +
                 "   (you have " + Number(p.rupees) + ")",
             56, y, 0.38f, p.rupees >= p.RupeesNeeded(mask) ? Good : Warn);
        y += 18;

        const float colHave = 640, colOwn = 800, colHidden = 960, colFree = 1100;
        Rect(0, y, ScreenW, 30, BarColor);
        Text("Material", 56, y + 21, 0.34f, TextDim);
        Text("Have", colHave, y + 21, 0.34f, TextDim, ALIGN_RIGHT);
        Text("Your armor", colOwn, y + 21, 0.34f, TextDim, ALIGN_RIGHT);
        Text("Hidden", colHidden, y + 21, 0.34f, TextDim, ALIGN_RIGHT);
        Text("Free", colFree, y + 21, 0.34f, TextDim, ALIGN_RIGHT);
        y += 32;

        const float rowH = 30;
        const float listBottom = ScreenH - FooterH - 70;
        const int visible = std::max(1, static_cast<int>((listBottom - y) / rowH));
        int& cursor = app.cursor[1];
        int& scroll = app.scroll[1];
        if (cursor < scroll)
            scroll = cursor;
        if (cursor >= scroll + visible)
            scroll = cursor - visible + 1;
        for (int i = scroll; i < static_cast<int>(app.rows.size()) && i < scroll + visible; i++)
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
            const int32_t h = m.need[ArmorNotOwned] + m.need[Crafted] + m.need[Quests];
            Text(h ? Number(h) : "-", colHidden, ry + 21, 0.38f, app.countHidden ? TextColor : TextDim, ALIGN_RIGHT);
            const int32_t free = m.Free(mask);
            const int32_t shortBy = m.Needed(mask) - m.have;
            Text(Number(free), colFree, ry + 21, 0.38f, free > 0 ? Good : TextDim, ALIGN_RIGHT);
            if (shortBy > 0)
                Text("short " + Number(shortBy), colFree + 24, ry + 21, 0.34f, Warn);
        }

        // What the selected row's hidden part is for.
        if (!app.rows.empty())
        {
            const MaterialRow& row = app.rows[cursor];
            const MaterialPlan& m = p.materials[row.index];
            const float by = ScreenH - FooterH - 40;
            std::string line;
            const int32_t hiddenNeed = m.need[ArmorNotOwned] + m.need[Crafted] + m.need[Quests];
            if (row.reveal == 0)
                line = hiddenNeed ? "A: what the hidden part is for" : "Nothing hidden: all for the armor you have";
            else if (hiddenNeed == 0 && !row.name.empty())
                line = "Nothing hidden: all for the armor you have";
            else
            {
                line = "Hidden: armor you do not have " + Number(m.need[ArmorNotOwned]) + ", Ancient Oven " +
                       Number(m.need[Crafted]) + ", side quests " + Number(m.need[Quests]);
                if (row.name.empty())
                    line += row.reveal >= 2 ? "" : "   (A again: the name)";
            }
            Text(line, 28, by, 0.38f, Accent);
        }
    }

    struct SellLine
    {
        std::vector<Part> parts; // a pot; empty for a raw sale
        uint32_t rawItem = 0;
        int32_t rawCount = 0;
        int32_t price = 0;
        int32_t raw = 0;
        int count = 0; // identical pots
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
        return lines;
    }

    void SellTab(App& app)
    {
        const SalePlan& s = *app.sale;
        float y = HeaderH + 34;
        const int32_t total = s.cookedTotal + s.partsLeftRaw + s.rawTotal;
        Text("Elixirs: " + std::to_string(s.potCount) + " pots, " + Number(s.cookedTotal) + " rupees (" +
                 Number(s.cookedRawTotal) + " sold raw)",
             28, y, 0.40f, TextColor);
        y += 28;
        Text("Monster parts left with no critter: " + Number(s.partsLeft) + ", " + Number(s.partsLeftRaw) +
                 " rupees raw   Sell as they are: " + Number(s.rawTotal),
             28, y, 0.38f, TextColor);
        y += 28;
        Icons::Ui("rupee", 26, y - 20, 26);
        Text("All of it: " + Number(total) + " rupees, " + Number(app.plan->rupees + total) + " with what you have",
             58, y, 0.40f, Gold);
        y += 14;

        const std::vector<SellLine> lines = SellLines(app);
        Rect(0, y, ScreenW, 30, BarColor);
        Text("Cook and sell", 28, y + 21, 0.34f, TextDim);
        Text("Each", 1020, y + 21, 0.34f, TextDim, ALIGN_RIGHT);
        Text("Raw", 1140, y + 21, 0.34f, TextDim, ALIGN_RIGHT);
        Text("Pots", 1240, y + 21, 0.34f, TextDim, ALIGN_RIGHT);
        y += 32;
        const float rowH = 34;
        const int visible = std::max(1, static_cast<int>((ScreenH - FooterH - 16 - y) / rowH));
        int& cursor = app.cursor[2];
        int& scroll = app.scroll[2];
        cursor = std::min(cursor, std::max(0, static_cast<int>(lines.size()) - 1));
        if (cursor < scroll)
            scroll = cursor;
        if (cursor >= scroll + visible)
            scroll = cursor - visible + 1;
        for (int i = scroll; i < static_cast<int>(lines.size()) && i < scroll + visible; i++)
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
                // Each ingredient: its icon and how many, then the name of the first ones while there is room.
                float x = 24;
                for (size_t k = 0; k < l.parts.size(); k++)
                {
                    Icons::Item(ActorName(app, l.parts[k].item), x, ry + 3, 28);
                    x = Text("x" + std::to_string(l.parts[k].count), x + 30, baseline, 0.36f, TextColor) + 8;
                    const std::string name = NameOf(app, l.parts[k].item);
                    if (x + Width(name, 0.34f) < 880)
                        x = Text(name, x, baseline, 0.34f, TextDim) + 18;
                }
                Text(Number(l.raw), 1140, baseline, 0.36f, TextDim, ALIGN_RIGHT);
                Text("x" + std::to_string(l.count), 1240, baseline, 0.38f, TextColor, ALIGN_RIGHT);
            }
            else
            {
                Icons::Item(ActorName(app, l.rawItem), 24, ry + 3, 28);
                Text("Sell raw: " + std::to_string(l.rawCount) + "x " + NameOf(app, l.rawItem), 62, baseline, 0.36f,
                     TextDim);
            }
            Text(Number(l.price), 1020, baseline, 0.38f, Gold, ALIGN_RIGHT);
        }
        if (lines.empty())
            Text("Nothing free to sell.", 28, y + 30, 0.40f, TextDim);
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
        default: return static_cast<int>(SellLines(app).size());
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
    bool loaded = false;
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

        if (loaded && ok)
        {
            if (down & (HidNpadButton_R | HidNpadButton_ZR))
                app.tab = (app.tab + 1) % 3;
            if (down & (HidNpadButton_L | HidNpadButton_ZL))
                app.tab = (app.tab + 2) % 3;
            // Up/Down, repeating while held.
            int move = 0;
            if (held & (HidNpadButton_Up | HidNpadButton_Down))
            {
                heldFrames++;
                const bool repeat = heldFrames > 18 && heldFrames % 4 == 0;
                if ((down & HidNpadButton_Down) || (repeat && (held & HidNpadButton_Down)))
                    move = 1;
                if ((down & HidNpadButton_Up) || (repeat && (held & HidNpadButton_Up)))
                    move = -1;
            }
            else
                heldFrames = 0;
            const int rows = RowCount(app);
            if (move && rows > 0)
                app.cursor[app.tab] = std::max(0, std::min(rows - 1, app.cursor[app.tab] + move));
            if ((down & HidNpadButton_A) && app.tab == 1 && !app.rows.empty())
            {
                MaterialRow& row = app.rows[app.cursor[1]];
                row.reveal = std::min(row.reveal + 1, row.name.empty() ? 2 : 1);
            }
            if (down & HidNpadButton_X)
            {
                app.countHidden = !app.countHidden;
                Compute(app);
            }
            if ((down & HidNpadButton_Y) && app.tab == 0)
            {
                app.wholePouch = !app.wholePouch;
                Compute(app);
            }
        }

        // Shapes, icons and text all go into queues: the shapes are drawn first, then the icons, then the text.
        glClearColor(Background.r, Background.g, Background.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        font.BeginBatch();
        Header(app);
        Footer(app);
        if (!loaded)
            Message("Reading the save and cooking every combination...", TextDim);
        else if (!ok)
            Message(app.error, Warn);
        else
        {
            if (app.tab == 0)
                DishesTab(app);
            else if (app.tab == 1)
                ReserveTab(app);
            else
                SellTab(app);
        }
        g_shapes->Render(g_projection);
        Icons::Flush(g_projection, ScreenH);
        font.RenderBatch();
        eglSwapBuffers(s_display, s_surface);

        if (!loaded && frame > 0)
        {
            ok = Load(app);
            loaded = true;
            if (!ok)
                Log("Load failed:", app.error);
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
