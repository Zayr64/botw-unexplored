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
        const std::vector<Navigation::Candidate> objects = {
            {{5.0f, 0.0f}, true, false}, {{1.0f, 0.0f}, true, true}, {{2.0f, 0.0f}, true, false},
            {{-2.0f, 0.0f}, true, false}, {{9.0f, 0.0f}, false, false}, {{0.0f, 7.0f}, true, false},
            {{0.0f, -3.0f}, true, false}, {{8.0f, 0.0f}, true, false}
        };
        // Missing and visible ones by distance: 2 and 3 tie at 2 (candidate order), then 6, 0, 5, 7.
        assert((Navigation::NearestByDistance(objects, {0.0f, 0.0f}, true, 5) == std::vector<int>{2, 3, 6, 0, 5}));
        // Fewer than asked: all of them.
        assert((Navigation::NearestByDistance(objects, {0.0f, 0.0f}, true, 20) == std::vector<int>{2, 3, 6, 0, 5, 7}));
        // Found ones count when asked.
        assert((Navigation::NearestByDistance(objects, {0.0f, 0.0f}, false, 2) == std::vector<int>{1, 2}));
        // Another origin, another order.
        assert((Navigation::NearestByDistance(objects, {8.0f, 0.0f}, true, 2) == std::vector<int>{7, 0}));
        assert(Navigation::NearestByDistance(objects, {0.0f, 0.0f}, true, 0).empty());
        const std::vector<Navigation::Candidate> none = {{{1.0f, 0.0f}, true, true}};
        assert(Navigation::NearestByDistance(none, {0.0f, 0.0f}, true, 5).empty());
    }
    for (Navigation::Candidate& candidate : candidates) candidate.visible = false;
    assert(!Navigation::FindNearest(candidates, {0.0f, 0.0f}, true, result));
}
