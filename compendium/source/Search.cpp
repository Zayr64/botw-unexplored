#include "Search.h"

namespace Compendium
{
    namespace
    {
        constexpr int MaxCandidates = 32;

        struct Candidate
        {
            uint32_t hash = 0;
            const Actor* actor = nullptr;
            int32_t count = 0;
        };

        struct Candidates
        {
            int count = 0;
            Candidate items[MaxCandidates];

            bool Has(uint32_t hash) const
            {
                for (int i = 0; i < count; i++)
                    if (items[i].hash == hash)
                        return true;
                return false;
            }

            void Add(const Candidate& c)
            {
                if (count < MaxCandidates && !Has(c.hash))
                    items[count++] = c;
            }
        };

        bool IsFood(const Actor& a)
        {
            return !a.HasTag(Tags::CookEnemy()) && !a.HasTag(Tags::CookOre()) && !a.HasTag(Tags::CookInsect());
        }

        // A fairy in the pouch brings Link back when he falls; it is never suggested as an ingredient.
        bool IsFairy(const Actor& a)
        {
            static const uint32_t fairy = Crc32("Animal_Insect_F");
            return a.hash == fairy;
        }

        // Adds up to `max` stock items that pass `filter`, best first by `better`.
        template <typename Filter, typename Better>
        void AddTop(Candidates& out, const CookData& data, const StockItem* stock, int stockCount, int max,
                    Filter filter, Better better)
        {
            Candidate picked[MaxCandidates];
            int n = 0;
            for (int i = 0; i < stockCount; i++)
            {
                if (stock[i].count <= 0)
                    continue;
                const Actor* a = data.FindActor(stock[i].hash);
                if (!a || IsFairy(*a) || !filter(*a) || out.Has(a->hash))
                    continue;
                Candidate c;
                c.hash = a->hash;
                c.actor = a;
                c.count = stock[i].count;
                // Insert in order, keep the first `max`.
                int pos = n;
                while (pos > 0 && better(*a, *picked[pos - 1].actor))
                    pos--;
                if (pos >= max)
                    continue;
                if (n < max)
                    n++;
                for (int k = n - 1; k > pos; k--)
                    picked[k] = picked[k - 1];
                picked[pos] = c;
            }
            for (int i = 0; i < n; i++)
                out.Add(picked[i]);
        }

        // Higher is better. Compared field by field.
        struct Score
        {
            int32_t keys[3] = {};
        };

        bool Better(const Score& a, int32_t costA, const Score& b, int32_t costB)
        {
            for (int i = 0; i < 3; i++)
                if (a.keys[i] != b.keys[i])
                    return a.keys[i] > b.keys[i];
            return costA < costB;
        }

        Score ScoreOf(const CookResult& r, Effect goal)
        {
            Score s;
            if (goal == EffectNone)
            {
                s.keys[0] = r.life;
                return s;
            }
            s.keys[0] = r.vitality;
            if (goal == LifeMaxUp)
                return s;
            if (goal == GutsRecover || goal == ExGutsMaxUp)
            {
                s.keys[1] = r.life;
                return s;
            }
            s.keys[1] = r.time;
            s.keys[2] = r.life;
            return s;
        }

        // Calls `visit(pick, depth)` for every multiset of 1 to 5 candidates within the stock: `pick` holds `depth`
        // candidate indexes in non-decreasing order.
        template <typename Visit>
        void ForEachPot(const Candidates& c, Visit visit)
        {
            int pick[5] = {};
            int used[MaxCandidates] = {};
            int depth = 0;
            int next = 0;
            while (true)
            {
                if (depth < 5 && next < c.count)
                {
                    if (used[next] < c.items[next].count)
                    {
                        pick[depth] = next;
                        used[next]++;
                        depth++;
                        visit(pick, depth);
                        continue; // go deeper, starting at the same index
                    }
                    next++;
                    continue;
                }
                if (depth == 0)
                    break;
                // Back up one level and move that position to the next index.
                depth--;
                used[pick[depth]]--;
                next = pick[depth] + 1;
            }
        }

        // The pot of `pick`, cooked. False when it is not a dish of the goal (another effect, a failure, random).
        bool CookPot(const CookData& data, const Candidates& c, const int* pick, int depth, Effect goal, FoundDish& out)
        {
            out.itemCount = depth;
            out.cost = 0;
            for (int i = 0; i < depth; i++)
            {
                out.items[i] = c.items[pick[i]].hash;
                if (c.items[pick[i]].actor->hasSellingPrice)
                    out.cost += c.items[pick[i]].actor->sellingPrice;
            }
            out.result = Cook(data, out.items, depth);
            if (out.result.failure || out.result.monsterExtract)
                return false;
            return goal == EffectNone ? out.result.effect != LifeMaxUp : out.result.effect == goal;
        }

        // Every pot of the candidates, keeping the best and the cheapest as strong.
        void Enumerate(const CookData& data, const Candidates& c, Effect goal, SearchResult& out)
        {
            Score bestScore;
            ForEachPot(c, [&](const int* pick, int depth) {
                FoundDish d;
                out.evaluated++;
                if (!CookPot(data, c, pick, depth, goal, d))
                    return;
                const Score s = ScoreOf(d.result, goal);
                if (!out.found || Better(s, d.cost, bestScore, out.best.cost))
                {
                    out.best = d;
                    bestScore = s;
                }
                out.found = true;
            });
            if (!out.found)
                return;

            // The cheapest dish as strong as the best: same first key.
            const int32_t target = bestScore.keys[0];
            Score cheapestScore;
            bool have = false;
            ForEachPot(c, [&](const int* pick, int depth) {
                FoundDish d;
                if (!CookPot(data, c, pick, depth, goal, d))
                    return;
                const Score s = ScoreOf(d.result, goal);
                if (s.keys[0] != target)
                    return;
                if (have && (d.cost > out.cheapest.cost ||
                             (d.cost == out.cheapest.cost && !Better(s, d.cost, cheapestScore, d.cost))))
                    return;
                out.cheapest = d;
                cheapestScore = s;
                have = true;
            });
        }

        // The value a target is set on: hearts (quarters) for EffectNone, the effect's strength otherwise.
        int32_t TargetKey(const CookResult& r, Effect goal)
        {
            return goal == EffectNone ? r.life : r.vitality;
        }

        // The cheapest pot reaching `minimum`; among equals, the one going least over it, then the one with fewer
        // items. Without any, the strongest pot.
        void EnumerateTarget(const CookData& data, const Candidates& c, Effect goal, int32_t minimum, TargetResult& out)
        {
            FoundDish strongest;
            bool haveStrongest = false;
            ForEachPot(c, [&](const int* pick, int depth) {
                FoundDish d;
                out.evaluated++;
                if (!CookPot(data, c, pick, depth, goal, d))
                    return;
                const int32_t key = TargetKey(d.result, goal);
                if (key >= minimum)
                {
                    const int32_t over = key - minimum;
                    const int32_t bestOver = out.reached ? TargetKey(out.dish.result, goal) - minimum : 0;
                    if (!out.reached || d.cost < out.dish.cost ||
                        (d.cost == out.dish.cost && (over < bestOver ||
                                                     (over == bestOver && d.itemCount < out.dish.itemCount))))
                    {
                        out.dish = d;
                        out.reached = true;
                    }
                }
                else if (!haveStrongest || key > TargetKey(strongest.result, goal) ||
                         (key == TargetKey(strongest.result, goal) && d.cost < strongest.cost))
                {
                    strongest = d;
                    haveStrongest = true;
                }
            });
            out.found = out.reached || haveStrongest;
            if (!out.reached && haveStrongest)
                out.dish = strongest;
        }

        // Helpers that can only add to a dish without bringing another effect.
        void AddHelpers(Candidates& c, const CookData& data, const StockItem* stock, int stockCount)
        {
            // Spices that add time (dragon parts, star fragment, acorn) or hearts.
            AddTop(c, data, stock, stockCount, 3,
                   [](const Actor& a) { return a.HasTag(Tags::CookSpice()) && !a.HasTag(Tags::CookEnemy()) && a.spiceTime > 0; },
                   [](const Actor& a, const Actor& b) { return a.spiceTime > b.spiceTime; });
            AddTop(c, data, stock, stockCount, 2,
                   [](const Actor& a) { return a.HasTag(Tags::CookSpice()) && !a.HasTag(Tags::CookEnemy()) && a.spiceHitPoint > 0; },
                   [](const Actor& a, const Actor& b) { return a.spiceHitPoint > b.spiceHitPoint; });
            // Monster parts: an elixir needs one; they add time. The ones adding most time, and the cheapest.
            AddTop(c, data, stock, stockCount, 2,
                   [&](const Actor& a) { return a.HasTag(Tags::CookEnemy()) && a.hash != data.monsterExtract; },
                   [](const Actor& a, const Actor& b) { return a.spiceTime > b.spiceTime; });
            AddTop(c, data, stock, stockCount, 1,
                   [&](const Actor& a) { return a.HasTag(Tags::CookEnemy()) && a.hash != data.monsterExtract; },
                   [](const Actor& a, const Actor& b) { return a.sellingPrice < b.sellingPrice; });
            // Plain food with the most hearts.
            AddTop(c, data, stock, stockCount, 3,
                   [](const Actor& a) { return IsFood(a) && a.effectLevel <= 0 && a.hitPointRecover > 0; },
                   [](const Actor& a, const Actor& b) { return a.hitPointRecover > b.hitPointRecover; });
        }
    }

    SearchResult FindBestForEffect(const CookData& data, const StockItem* stock, int stockCount, Effect effect)
    {
        SearchResult out;
        Candidates c;
        // Everything with the effect (food and critters).
        AddTop(c, data, stock, stockCount, 16,
               [&](const Actor& a) { return !a.HasTag(Tags::CookEnemy()) && a.effectLevel > 0 && a.effectType == effect; },
               [](const Actor& a, const Actor& b) { return a.effectLevel > b.effectLevel; });
        if (c.count == 0)
            return out;
        AddHelpers(c, data, stock, stockCount);
        Enumerate(data, c, effect, out);
        return out;
    }

    TargetResult FindCheapestReaching(const CookData& data, const StockItem* stock, int stockCount, Effect goal,
                                      int32_t minimum)
    {
        TargetResult out;
        Candidates c;
        if (goal == EffectNone)
        {
            // Hearts: the foods with the most hearts and those with the most hearts per rupee, plus spices adding
            // hearts. Yellow-heart foods are left out (they make a different dish, FindCheapestReaching(LifeMaxUp)).
            auto food = [](const Actor& a) { return IsFood(a) && a.hitPointRecover > 0 && a.effectType != LifeMaxUp; };
            AddTop(c, data, stock, stockCount, 6, food,
                   [](const Actor& a, const Actor& b) { return a.hitPointRecover > b.hitPointRecover; });
            AddTop(c, data, stock, stockCount, 6, food, [](const Actor& a, const Actor& b) {
                // hearts per rupee, compared without division
                return a.hitPointRecover * (b.sellingPrice > 0 ? b.sellingPrice : 1) >
                       b.hitPointRecover * (a.sellingPrice > 0 ? a.sellingPrice : 1);
            });
            AddTop(c, data, stock, stockCount, 2,
                   [](const Actor& a) { return a.HasTag(Tags::CookSpice()) && !a.HasTag(Tags::CookEnemy()) && a.spiceHitPoint > 0; },
                   [](const Actor& a, const Actor& b) { return a.spiceHitPoint > b.spiceHitPoint; });
        }
        else
        {
            auto withEffect = [&](const Actor& a) {
                return !a.HasTag(Tags::CookEnemy()) && a.effectLevel > 0 && a.effectType == goal;
            };
            AddTop(c, data, stock, stockCount, 8, withEffect,
                   [](const Actor& a, const Actor& b) { return a.effectLevel > b.effectLevel; });
            AddTop(c, data, stock, stockCount, 8, withEffect,
                   [](const Actor& a, const Actor& b) { return a.sellingPrice < b.sellingPrice; });
            if (c.count == 0)
                return out;
            // Monster parts, for elixirs: the cheapest.
            AddTop(c, data, stock, stockCount, 1,
                   [&](const Actor& a) { return a.HasTag(Tags::CookEnemy()) && a.hash != data.monsterExtract; },
                   [](const Actor& a, const Actor& b) { return a.sellingPrice < b.sellingPrice; });
        }
        if (c.count == 0)
            return out;
        EnumerateTarget(data, c, goal, minimum, out);
        return out;
    }

    SearchResult FindMostHearts(const CookData& data, const StockItem* stock, int stockCount)
    {
        SearchResult out;
        Candidates c;
        AddTop(c, data, stock, stockCount, 8,
               [](const Actor& a) { return IsFood(a) && a.hitPointRecover > 0 && a.effectType != LifeMaxUp; },
               [](const Actor& a, const Actor& b) { return a.hitPointRecover > b.hitPointRecover; });
        AddTop(c, data, stock, stockCount, 2,
               [](const Actor& a) { return a.HasTag(Tags::CookSpice()) && !a.HasTag(Tags::CookEnemy()) && a.spiceHitPoint > 0; },
               [](const Actor& a, const Actor& b) { return a.spiceHitPoint > b.spiceHitPoint; });
        if (c.count == 0)
            return out;
        Enumerate(data, c, EffectNone, out);
        return out;
    }
}
