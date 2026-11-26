#include "Level.h"

#include <cstdio>
#include <cstring>

#include "TextLine.h"

namespace Compendium
{
    bool LevelData::Load(const char* text, size_t size, char* error, size_t errorSize)
    {
        flagCount = speciesCount = 0;
        bool ok = true;
        bool setting = false;
        ForEachLine(text, size, [&](const char* kind, TextLine& line, int lineNumber) {
            if (strcmp(kind, "LEVELSET") == 0)
            {
                ok = line.Float(enemyPower) && line.Float(weaponPower);
                setting = ok;
            }
            else if (strcmp(kind, "LEVELFLAG") == 0)
            {
                if (flagCount >= MaxLevelFlags)
                {
                    snprintf(error, errorSize, "line %d: more than %d level flags", lineNumber, MaxLevelFlags);
                    return ok = false;
                }
                LevelFlag& f = flags[flagCount];
                f = LevelFlag();
                ok = line.Hex(f.flag) && line.Word(f.actor, sizeof(f.actor)) && line.Float(f.point) &&
                     line.OptionalInt(f.max);
                if (ok)
                {
                    f.max = ReachableKills(f.actor, f.max);
                    flagCount++;
                }
            }
            else if (strcmp(kind, "LEVELSPECIES") == 0)
            {
                if (speciesCount >= MaxSpecies)
                {
                    snprintf(error, errorSize, "line %d: more than %d species", lineNumber, MaxSpecies);
                    return ok = false;
                }
                Species& s = species[speciesCount];
                s = Species();
                int32_t n = 0;
                ok = line.Word(s.name, sizeof(s.name)) && line.Int(n) && n >= 0 && n <= MaxRanks;
                s.rankCount = ok ? n : 0;
                for (int i = 0; ok && i < n; i++)
                    ok = line.Word(s.ranks[i].actor, sizeof(s.ranks[i].actor)) && line.Float(s.ranks[i].value);
                if (ok)
                    speciesCount++;
            }
            if (!ok && error[0] == '\0')
                snprintf(error, errorSize, "line %d: bad %s line", lineNumber, kind);
            return ok;
        });
        if (ok && (!setting || flagCount == 0))
        {
            snprintf(error, errorSize, "no LEVEL lines (cook-data.txt from an older gen_cook_data.py?)");
            return false;
        }
        return ok;
    }

    int32_t ReachableKills(const char* actor, int32_t flagMax)
    {
        static const char* const onceOnly[] = {"Enemy_Assassin_Senior", "Enemy_GanonBeast", "Priest_Boss_Normal"};
        for (const char* name : onceOnly)
            if (strcmp(actor, name) == 0)
                return 1;
        return flagMax;
    }

    int32_t Kills(const SaveFile& save, const char* actor)
    {
        char flag[96];
        snprintf(flag, sizeof(flag), "Defeated_%s_Num", actor);
        int32_t kills = 0;
        if (!save.GetS32(flag, kills))
            return 0;
        return kills > 0 ? kills : 0;
    }

    void PlanLevel(const LevelData& data, const SaveFile& save, LevelPlan& out)
    {
        out = LevelPlan();
        float sum = 0.0f;
        for (int i = 0; i < data.flagCount; i++)
        {
            int32_t kills = 0;
            if (!save.GetS32(data.flags[i].flag, kills))
                kills = 0;
            out.maxPoints += data.flags[i].point * static_cast<float>(data.flags[i].max);
            if (kills > 0)
            {
                const float total = data.flags[i].point * static_cast<float>(kills);
                sum += total;
                out.flags[i].kills = kills;
                out.flags[i].total = total;
                out.defeatedKinds++;
            }
        }
        out.points = sum;
        out.enemyLevel = sum * data.enemyPower;
        out.weaponLevel = sum * data.weaponPower;

        for (int s = 0; s < data.speciesCount; s++)
        {
            const Species& sp = data.species[s];
            SpeciesState& st = out.species[s];
            for (int r = 0; r < sp.rankCount; r++)
                if (sp.ranks[r].value <= out.enemyLevel)
                    st.current = r;
            for (int r = sp.rankCount - 1; r >= 0; r--)
                if (Kills(save, sp.ranks[r].actor) > 0)
                    st.knownRank = r;
            st.currentKnown = st.current >= 0 && Kills(save, sp.ranks[st.current].actor) > 0;
            st.nextKnown = st.current + 1 < sp.rankCount && Kills(save, sp.ranks[st.current + 1].actor) > 0;
        }
    }
}
