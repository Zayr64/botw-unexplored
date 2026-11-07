#include "Navigation.h"

#include <algorithm>
#include <limits>
#include <utility>

Navigation::Point Navigation::ClampToViewport(Point cursor, const Viewport& viewport)
{
    cursor.x = std::max(viewport.center.x - viewport.halfWidth, std::min(viewport.center.x + viewport.halfWidth, cursor.x));
    cursor.y = std::max(viewport.center.y - viewport.halfHeight, std::min(viewport.center.y + viewport.halfHeight, cursor.y));
    return cursor;
}

bool Navigation::FindNearest(const std::vector<Candidate>& candidates, Point origin, bool missingOnly, Point& result)
{
    float bestDistanceSquared = std::numeric_limits<float>::max();
    bool foundCandidate = false;
    for (const Candidate& candidate : candidates)
    {
        if (!candidate.visible || (missingOnly && candidate.found))
            continue;
        const float dx = candidate.position.x - origin.x;
        const float dy = candidate.position.y - origin.y;
        const float distanceSquared = dx * dx + dy * dy;
        if (distanceSquared < bestDistanceSquared)
        {
            bestDistanceSquared = distanceSquared;
            result = candidate.position;
            foundCandidate = true;
        }
    }
    return foundCandidate;
}

std::vector<int> Navigation::NearestByDistance(const std::vector<Candidate>& candidates, Point origin, bool missingOnly, int count)
{
    std::vector<std::pair<float, int>> ranked;
    for (int index = 0; index < static_cast<int>(candidates.size()); ++index)
    {
        const Candidate& candidate = candidates[index];
        if (!candidate.visible || (missingOnly && candidate.found))
            continue;
        const float dx = candidate.position.x - origin.x;
        const float dy = candidate.position.y - origin.y;
        ranked.push_back({dx * dx + dy * dy, index});
    }
    const size_t keep = std::min(ranked.size(), static_cast<size_t>(std::max(0, count)));
    std::partial_sort(ranked.begin(), ranked.begin() + keep, ranked.end());

    std::vector<int> nearest;
    for (size_t i = 0; i < keep; ++i)
        nearest.push_back(ranked[i].second);
    return nearest;
}
