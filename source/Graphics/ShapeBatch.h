#pragma once

#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "Mesh.hpp"
#include "Shader.h"

// Flat coloured shapes for the interface (panels, bars, button caps, rings), drawn in one call.
// Curved edges get a one pixel fringe that fades out, so they look smooth without multisampling.
class ShapeBatch
{
public:
    ShapeBatch();
    ~ShapeBatch();

    void AddRect(float left, float bottom, float right, float top, const glm::vec4& color);
    void AddRoundedRect(float left, float bottom, float right, float top, float radius, const glm::vec4& color);
    void AddCircle(const glm::vec2& center, float radius, const glm::vec4& color);
    void AddRing(const glm::vec2& center, float innerRadius, float outerRadius, const glm::vec4& color);

    // Draws what was added and empties the batch.
    void Render(const glm::mat4& projection);

private:
    void AddConvexPolygon(const std::vector<glm::vec2>& points, const glm::vec4& color);
    bool HasRoomFor(size_t vertices) const;
    uint16_t AddVertex(const glm::vec2& position, const glm::vec4& color);

    Mesh<ColorVertex> m_Mesh;
    Shader m_Shader;
};
