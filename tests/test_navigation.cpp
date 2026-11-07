#include <cassert>
#include <vector>

#include "Navigation.h"

int main()
{
    const Navigation::Viewport viewport = {{10.0f, -10.0f}, 5.0f, 3.0f};
    const Navigation::Point clamped = Navigation::ClampToViewport({100.0f, -100.0f}, viewport);
    assert(clamped.x == 15.0f && clamped.y == -13.0f);

    std::vector<Navigation::Candidate> candidates = {
        {{1.0f, 0.0f}, true, true}, {{2.0f, 0.0f}, false, false}, {{3.0f, 0.0f}, true, false}
    };
    Navigation::Point result;
    assert(Navigation::FindNearest(candidates, {0.0f, 0.0f}, true, result));
    assert(result.x == 3.0f);
    assert(Navigation::FindNearest(candidates, {0.0f, 0.0f}, false, result));
    assert(result.x == 1.0f);
    {
        const std::vector<Navigation::Candidate> walk = {
            {{5.0f, 0.0f}, true, false}, {{1.0f, 0.0f}, true, true}, {{2.0f, 0.0f}, true, false},
            {{-2.0f, 0.0f}, true, false}, {{9.0f, 0.0f}, false, false}
        };
        Navigation::Step step;
        // Missing and visible ones by distance: index 2 and 3 tie at 2 (lower index first), then 0.
        assert(Navigation::FindNextByDistance(walk, {0.0f, 0.0f}, true, step, step) && step.index == 2);
        assert(Navigation::FindNextByDistance(walk, {0.0f, 0.0f}, true, step, step) && step.index == 3);
        assert(Navigation::FindNextByDistance(walk, {0.0f, 0.0f}, true, step, step) && step.index == 0);
        assert(step.distanceSquared == 25.0f);
        // Wraps to the nearest again.
        assert(Navigation::FindNextByDistance(walk, {0.0f, 0.0f}, true, step, step) && step.index == 2);
        // Nothing to visit.
        const std::vector<Navigation::Candidate> none = {{{1.0f, 0.0f}, true, true}};
        Navigation::Step empty;
        assert(!Navigation::FindNextByDistance(none, {0.0f, 0.0f}, true, Navigation::Step(), empty));
    }
    for (Navigation::Candidate& candidate : candidates) candidate.visible = false;
    assert(!Navigation::FindNearest(candidates, {0.0f, 0.0f}, true, result));
}
