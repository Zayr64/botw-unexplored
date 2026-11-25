#pragma once

#include <glm/vec2.hpp>

#include "Mesh.hpp"
#include "Shader.h"

class LineRenderer
{
public:
    explicit LineRenderer(int maxLines = 300);

    void AddLine(glm::vec2 start, glm::vec2 end, float width = 1.0f, glm::vec4 color = glm::vec4(1.0f));
    // A line `width` wide centred on the segment from `start` to `end` (AddLine draws it to one side).
    void AddSegment(glm::vec2 start, glm::vec2 end, float width, glm::vec4 color);
    // No room for one more line: draw (RenderLines) before adding it.
    bool IsFull() { return m_Mesh.GetVertices().size() + 4 > static_cast<size_t>(MAX_LINES) * 4; }

    void RenderLines(glm::mat4 projMat, glm::mat4 viewMat = glm::mat4(1.0f));

    ~LineRenderer();

public:
    Mesh<ColorVertex> m_Mesh;
    Shader m_Shader; 

    const int MAX_LINES;
};