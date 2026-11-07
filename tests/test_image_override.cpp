#include <cassert>

#include "ImageOverride.h"

int main()
{
    assert(ImageOverride::Candidate("romfs:/shrine.png") == "sdmc:/switch/botw-unexplored/icons/shrine.png");
    assert(ImageOverride::Candidate("romfs:/korokseed.png") == "sdmc:/switch/botw-unexplored/icons/korokseed.png");
    assert(ImageOverride::Candidate("romfs:/guide/Korok12.jpg").empty());
    assert(ImageOverride::Candidate("romfs:/guide/x.png").empty());
    assert(ImageOverride::Candidate("romfs:/map-lowres.jpg").empty());
    assert(ImageOverride::Candidate("sdmc:/switch/x.png").empty());
    assert(ImageOverride::Candidate("romfs:/.png").empty());
    assert(ImageOverride::Candidate("romfs:/..png").empty());
    assert(ImageOverride::Candidate("").empty());
    return 0;
}
