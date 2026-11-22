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

        // Cooks every multiset of up to 5 candidates, keeping the best and the cheapest as strong.
        void Enumerate(const CookData& data, const Candidates& c, Effect goal, SearchResult& out)
        {
            int pick[5] = {};
            int used[MaxCandidates] = {};
            Score bestScore;
            Score cheapestScore;

            auto visit = [&](int depth) {
                uint32_t items[5];
                int32_t cost = 0;
                for (int i = 0; i < depth; i++)
                {
                    items[i] = c.items[pick[i]].hash;
                    if (c.items[pick[i]].actor->hasSellingPrice)
                        cost += c.items[pick[i]].actor->sellingPrice;
                }
                const CookResult r = Cook(data, items, depth);
                out.evaluated++;
                if (r.failure || r.monsterExtract)
                    return;
                if (goal == EffectNone ? r.effect == LifeMaxUp : r.effect != goal)
                    return;
                const Score s = ScoreOf(r, goal);
                FoundDish d;
                for (int i = 0; i < depth; i++)
                    d.items[i] = items[i];
                d.itemCount = depth;
                d.result = r;
                d.cost = cost;
                if (!out.found || Better(s, cost, bestScore, out.best.cost))
                {
                    out.best = d;
                    bestScore = s;
                }
                out.found = true;
            };

            // Every non-decreasing sequence of candidate indexes of length 1 to 5 (a multiset), within the stock.
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
                        visit(depth);
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

            if (!out.found)
                return;
            // Second pass for the cheapest dish as strong as the best: same first key.
            const int32_t target = ScoreOf(out.best.result, goal).keys[0];
            bool have = false;
            depth = 0;
            next = 0;
            for (int i = 0; i < MaxCandidates; i++)
                used[i] = 0;
            auto consider = [&](int d) {
                uint32_t items[5];
                int32_t cost = 0;
                for (int i = 0; i < d; i++)
                {
                    items[i] = c.items[pick[i]].hash;
                    if (c.items[pick[i]].actor->hasSellingPrice)
                        cost += c.items[pick[i]].actor->sellingPrice;
                }
                if (have && cost > out.cheapest.cost)
                    return;
                const CookResult r = Cook(data, items, d);
                if (r.failure || r.monsterExtract)
                    return;
                if (goal == EffectNone ? r.effect == LifeMaxUp : r.effect != goal)
                    return;
                const Score s = ScoreOf(r, goal);
                if (s.keys[0] != target)
                    return;
                if (have && cost == out.cheapest.cost && !Better(s, cost, cheapestScore, cost))
                    return;
                FoundDish dish;
                for (int i = 0; i < d; i++)
                    dish.items[i] = items[i];
                dish.itemCount = d;
                dish.result = r;
                dish.cost = cost;
                out.cheapest = dish;
                cheapestScore = s;
                have = true;
            };
            while (true)
            {
                if (depth < 5 && next < c.count)
                {
                    if (used[next] < c.items[next].count)
                    {
                        pick[depth] = next;
                        used[next]++;
                        depth++;
                        consider(depth);
                        continue;
                    }
                    next++;
                    continue;
                }
                if (depth == 0)
                    break;
                depth--;
                used[pick[depth]]--;
                next = pick[depth] + 1;
            }
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
