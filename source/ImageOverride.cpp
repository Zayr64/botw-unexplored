#include "ImageOverride.h"

std::string ImageOverride::Candidate(const std::string& path)
{
    const std::string prefix = "romfs:/";
    const std::string suffix = ".png";
    if (path.size() <= prefix.size() + suffix.size() || path.compare(0, prefix.size(), prefix) != 0 ||
        path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0)
        return "";
    const std::string name = path.substr(prefix.size());
    if (name.find('/') != std::string::npos || name.find("..") != std::string::npos)
        return ""; // only images at the top of romfs (not the Korok guide pictures)
    return "sdmc:/switch/botw-unexplored/icons/" + name;
}
