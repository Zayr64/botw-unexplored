// BotW Cozinha: the best dishes from what is in the pouch, what to hold back for armor and quests, and what to sell.
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
#include "Reserve.h"
#include "Sale.h"
#include "SaveFile.h"
#include "SaveSource.h"
#include "Search.h"

using namespace Cozinha;

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

    const char* DataPath = "sdmc:/switch/botw-cozinha/cook-data.txt";

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

    // `baseline`: distance of the text's baseline from the top of the screen.
    void Text(const std::string& s, float x, float baseline, float scale, const glm::vec3& color,
              int align = ALIGN_LEFT, float maxWidth = -1.0f)
    {
        g_font->AddTextToBatch(s, glm::vec2(x, ScreenH - baseline), scale, color, align, maxWidth);
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

    struct Goal
    {
        const char* label;
        const char* detail;
        Effect effect;
        bool hearts;
        SearchResult result;
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

    // "2x Razorclaw Crab" lines for the ingredients of a dish, in pot order.
    std::vector<std::string> IngredientLines(const App& app, const uint32_t* items, int count)
    {
        std::vector<std::string> lines;
        std::vector<uint32_t> seen;
        for (int i = 0; i < count; i++)
        {
            if (std::find(seen.begin(), seen.end(), items[i]) != seen.end())
                continue;
            seen.push_back(items[i]);
            int n = 0;
            for (int k = 0; k < count; k++)
                n += items[k] == items[i];
            lines.push_back(std::to_string(n) + "x " + NameOf(app, items[i]));
        }
        return lines;
    }

    std::string Strength(const CookResult& r)
    {
        switch (r.effect)
        {
        case LifeMaxUp: return "+" + std::to_string(r.vitality / 4) + " yellow hearts";
        case GutsRecover:
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f wheels", r.vitality / 1000.0);
            return buf;
        }
        case ExGutsMaxUp:
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "+%.1f wheels", r.vitality / 5.0);
            return buf;
        }
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
        app.evaluated = 0;
        for (Goal& g : app.goals)
        {
            g.result = g.hearts ? FindMostHearts(*app.cook, stock.data(), static_cast<int>(stock.size()))
                                : FindBestForEffect(*app.cook, stock.data(), static_cast<int>(stock.size()), g.effect);
            app.evaluated += g.result.evaluated;
        }
        PlanSale(*app.cook, freeStock.data(), static_cast<int>(freeStock.size()), std::max(0, MaxSalePots - app.dishes),
                 *app.sale);
    }

    bool Load(App& app)
    {
        FILE* f = fopen(DataPath, "rb");
        if (!f)
        {
            app.error = "cook-data.txt is missing in switch/botw-cozinha/ (it is made on the PC from the game's files)";
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

        app.plan.reset(new ReservePlan());
        PlanReserve(*app.cook, *app.reserve, app.save, app.pouch.data(), app.pouchCount, *app.plan);
        app.sale.reset(new SalePlan());

        app.goals = {
            {"Mighty", "attack up", AttackUp, false, SearchResult()},
            {"Tough", "defense up", DefenseUp, false, SearchResult()},
            {"Hasty", "speed up", MovingSpeed, false, SearchResult()},
            {"Sneaky", "stealth up", Quietness, false, SearchResult()},
            {"Chilly", "heat resistance", ResistHot, false, SearchResult()},
            {"Spicy", "cold resistance", ResistCold, false, SearchResult()},
            {"Electro", "shock resistance", ResistElectric, false, SearchResult()},
            {"Fireproof", "flame guard", Fireproof, false, SearchResult()},
            {"Hearty", "yellow hearts", LifeMaxUp, false, SearchResult()},
            {"Energizing", "stamina", GutsRecover, false, SearchResult()},
            {"Enduring", "extra stamina", ExGutsMaxUp, false, SearchResult()},
            {"Hearts", "most hearts", EffectNone, true, SearchResult()},
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
        Text("BotW Cozinha", 28, 42, 0.62f, Gold);
        const char* tabs[] = {"Dishes", "Reserve", "Sell"};
        float x = 300;
        for (int i = 0; i < 3; i++)
        {
            const float w = Width(tabs[i], 0.46f) + 36;
            if (i == app.tab)
                RoundRect(x, 14, w, 36, 10, RowOn);
            Text(tabs[i], x + w / 2, 40, 0.46f, i == app.tab ? TextColor : TextDim, ALIGN_CENTER);
            x += w + 8;
        }
        if (app.plan)
            Text(Number(app.plan->rupees) + " rupees", ScreenW - 28, 40, 0.44f, TextColor, ALIGN_RIGHT);
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

    void DrawDish(const App& app, const FoundDish& d, float x, float top, float width, const char* title)
    {
        char name[96];
        DishName(*app.cook, d.result, name, sizeof(name));
        Text(title, x, top, 0.34f, Accent);
        Text(Fit(name, 0.54f, width), x, top + 34, 0.54f, TextColor);
        float y = top + 66;
        for (const std::string& line : IngredientLines(app, d.items, d.itemCount))
        {
            Text(line, x + 10, y, 0.40f, TextColor);
            y += 26;
        }
        y += 8;
        std::string result = Strength(d.result);
        if (d.result.effect != EffectNone && d.result.effect != LifeMaxUp)
            result += "   " + Hearts(d.result.life) + " hearts";
        Text(result, x, y, 0.42f, Gold);
        y += 28;
        Text("Sells for " + Number(d.result.price) + "   ingredients worth " + Number(d.cost), x, y, 0.36f, TextDim);
        y += 24;
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
            Text(g.label, 28, y + 29, 0.44f, TextColor);
            const std::string summary = g.result.found ? Strength(g.result.best.result) : "nothing free";
            Text(summary, listW - 28, y + 29, 0.38f, g.result.found ? Gold : TextDim, ALIGN_RIGHT);
        }

        const Goal& g = app.goals[app.cursor[0]];
        const float x = listW + 40;
        const float width = ScreenW - x - 40;
        Text(std::string(g.label) + " (" + g.detail + ")", x, top + 30, 0.40f, TextDim);
        if (!g.result.found)
        {
            Text(app.wholePouch ? "Nothing in the pouch makes this." :
                                  "Nothing free makes this. Y searches the whole pouch, X stops counting hidden needs.",
                 x, top + 80, 0.42f, TextColor, ALIGN_LEFT, width);
            return;
        }
        DrawDish(app, g.result.best, x, top + 72, width, "BEST");
        const FoundDish& c = g.result.cheapest;
        if (c.itemCount > 0 && c.cost < g.result.best.cost)
            DrawDish(app, c, x, top + 72 + 300, width, "CHEAPEST AS STRONG");
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
        Text("Rupees to keep: " + Number(own) + " for your armor and the fountains" +
                 (app.countHidden ? ", " + Number(hidden) + " hidden" : std::string(", hidden not counted")) +
                 "   (you have " + Number(p.rupees) + ")",
             28, y, 0.38f, p.rupees >= p.RupeesNeeded(mask) ? Good : Warn);
        y += 18;

        const float colHave = 640, colOwn = 800, colHidden = 960, colFree = 1100;
        Rect(0, y, ScreenW, 30, BarColor);
        Text("Material", 28, y + 21, 0.34f, TextDim);
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
            Text(Fit(name, 0.38f, 420), 28, ry + 21, 0.38f, named ? TextColor : TextDim);
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
        Text("All of it: " + Number(total) + " rupees, " + Number(app.plan->rupees + total) + " with what you have",
             28, y, 0.40f, Gold);
        y += 14;

        // The pots, identical ones together.
        struct Line
        {
            std::string text;
            int32_t price;
            int32_t raw;
            int count;
        };
        std::vector<Line> lines;
        for (int i = 0; i < s.potCount; i++)
        {
            const SalePot& pot = s.pots[i];
            std::string t;
            for (const std::string& part : IngredientLines(app, pot.items, pot.itemCount))
                t += (t.empty() ? "" : " + ") + part;
            if (!lines.empty() && lines.back().text == t && lines.back().price == pot.price)
                lines.back().count++;
            else
                lines.push_back({t, pot.price, pot.rawPrice, 1});
        }
        for (int i = 0; i < s.rawCount; i++)
            lines.push_back({"Sell raw: " + std::to_string(s.raw[i].count) + "x " + NameOf(app, s.raw[i].item),
                             s.raw[i].price, s.raw[i].price, 0});

        Rect(0, y, ScreenW, 30, BarColor);
        Text("Cook and sell", 28, y + 21, 0.34f, TextDim);
        Text("Each", 1020, y + 21, 0.34f, TextDim, ALIGN_RIGHT);
        Text("Raw", 1140, y + 21, 0.34f, TextDim, ALIGN_RIGHT);
        Text("Pots", 1240, y + 21, 0.34f, TextDim, ALIGN_RIGHT);
        y += 32;
        const float rowH = 30;
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
            const Line& l = lines[i];
            const float ry = y + (i - scroll) * rowH;
            if (i == cursor)
                Rect(0, ry, ScreenW, rowH, RowOn);
            else if (i % 2)
                Rect(0, ry, ScreenW, rowH, RowAlt);
            Text(Fit(l.text, 0.36f, 900), 28, ry + 21, 0.36f, l.count ? TextColor : TextDim);
            Text(Number(l.price), 1020, ry + 21, 0.38f, Gold, ALIGN_RIGHT);
            if (l.count)
            {
                Text(Number(l.raw), 1140, ry + 21, 0.36f, TextDim, ALIGN_RIGHT);
                Text("x" + std::to_string(l.count), 1240, ry + 21, 0.38f, TextColor, ALIGN_RIGHT);
            }
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
        default:
        {
            int pots = 0;
            for (int i = 0; i < app.sale->potCount; i++)
                pots += i == 0 || memcmp(app.sale->pots[i].items, app.sale->pots[i - 1].items,
                                         sizeof(app.sale->pots[i].items)) != 0 ||
                        app.sale->pots[i].price != app.sale->pots[i - 1].price;
            return pots + app.sale->rawCount;
        }
        }
    }
}

int main()
{
    LogInit();
    Log("START: botw-cozinha");
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

        // Shapes and text both go into batches: all the shapes are drawn first, the text over them.
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
    shapes.reset();
    font.Destroy();
    deinitEgl();
    if (romfs)
        romfsExit();
    appletUnlockExit();
    return 0;
}
