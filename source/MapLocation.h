#pragma once

#include "Graphics/Texture2D.h"
#include "Graphics/Mesh.hpp"
#include "Graphics/Shader.h" 
#include "Graphics/Font.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <vector>

#include <switch.h>

namespace Data
{
    struct Location;
};

class MapLocation
{
public:
    Data::Location* m_LocationData = nullptr;

    glm::vec2 m_Position;
    glm::vec3 m_Color = glm::vec3(1.0f, 1.0f, 1.0f);

    bool m_Found = false;
    // Visited, but something is still left to do there (a tower not activated, a fairy not awakened).
    bool m_Pending = false;
    // The region of the map it is in (Regions.h), or -1.
    int m_Region = -1;
    float m_Scale = 1.0f;

public:
    MapLocation();

    void Init();

    void Update();
    // The names are drawn twice: first a dark edge (shadow), then the text itself.
    void Render(bool shadow);

    static void BeginLabelPass();

    ~MapLocation();

private:
    static std::vector<glm::vec4> m_LabelBounds;
};
