#pragma once

// A named place of the map. The app does not draw them, but the game counts the visited ones in its map
// completion, and so does the app.
class MapLocation
{
public:
    bool m_Found = false;
};
