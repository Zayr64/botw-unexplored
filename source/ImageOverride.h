#pragma once

#include <string>

// Optional sharper copies of the app's images: a PNG with the same name in sdmc:/switch/botw-unexplored/icons/
// replaces a top-level romfs image. It is drawn at the romfs image's size, so it may have any resolution with the
// same aspect ratio. Nothing in that folder ships with the app.
namespace ImageOverride
{
    // The SD path that would replace this image, or "" for images that cannot be replaced.
    std::string Candidate(const std::string& path);
}
