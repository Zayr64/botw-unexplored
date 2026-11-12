#include "ShapeBatch.h"

#include <cmath>

#include <glm/geometric.hpp>

namespace
{
    constexpr size_t MaxVertices = 12000;
    constexpr float Fringe = 1.0f; // width, in pixels, of the fading edge
    constexpr float Pi = 3.14159265358979f;

    int SegmentsFor(float radius)
    {
        const int segments = static_cast<int>(radius * 0.9f) + 8;
        return segments > 48 ? 48 : segments;
    }
}

ShapeBatch::ShapeBatch()
{
    const std::string vertexShaderSource = R"text(
        #version 330 core

        layout (location = 0) in vec3 position;
        layout (location = 1) in vec4 color;

        out vec4 passColor;

        uniform mat4 u_ProjectionMatrix = mat4(1.0);

        void main()
        {
            gl_Position = u_ProjectionMatrix * vec4(position, 1.0);
            passColor = color;
        }
    )text";

    const std::string fragmentShaderSource = R"text(
        #version 330 core

        layout (location = 0) out vec4 color;

        in vec4 passColor;

        void main()
        {
            color = passColor;
        }
    )text";

    m_Shader = ShaderLoader::CreateShaderFromSource(vertexShaderSource, fragmentShaderSource);
    m_Mesh.m_UseDynamicBuffer = true;
    m_Mesh.m_DynamicBufferSize = sizeof(ColorVertex) * MaxVertices;
    m_Mesh.CreateEmptyBuffer();
}

ShapeBatch::~ShapeBatch()
{
    m_Mesh.Destroy();
    m_Shader.Delete();
}

bool ShapeBatch::HasRoomFor(size_t vertices) const
{
    return const_cast<Mesh<ColorVertex>&>(m_Mesh).GetVertices().size() + vertices <= MaxVertices;
}

uint16_t ShapeBatch::AddVertex(const glm::vec2& position, const glm::vec4& color)
{
    ColorVertex vertex;
    vertex.position = glm::vec3(position, 0.0f);
    vertex.color = color;
    m_Mesh.AddVertex(vertex);
    return static_cast<uint16_t>(m_Mesh.GetVertices().size() - 1);
}

void ShapeBatch::AddRect(float left, float bottom, float right, float top, const glm::vec4& color)
{
    if (!HasRoomFor(4) || right <= left || top <= bottom)
        return;
    const uint16_t a = AddVertex(glm::vec2(left, bottom), color);
    const uint16_t b = AddVertex(glm::vec2(right, bottom), color);
    const uint16_t c = AddVertex(glm::vec2(right, top), color);
    const uint16_t d = AddVertex(glm::vec2(left, top), color);
    for (const uint16_t index : {a, b, c, a, c, d})
        m_Mesh.AddIndex(index);
}

// Counter-clockwise points. The fill stops half a fringe inside the outline; the fringe fades to nothing.
void ShapeBatch::AddConvexPolygon(const std::vector<glm::vec2>& points, const glm::vec4& color)
{
    const size_t count = points.size();
    if (count < 3 || !HasRoomFor(count * 2))
        return;

    const glm::vec4 transparent(color.r, color.g, color.b, 0.0f);
    std::vector<uint16_t> inner(count), outer(count);
    for (size_t i = 0; i < count; ++i)
    {
        const glm::vec2& previous = points[(i + count - 1) % count];
        const glm::vec2& current = points[i];
        const glm::vec2& next = points[(i + 1) % count];
        // Outward normals of the two edges that meet here (the polygon is counter-clockwise).
        glm::vec2 edgeIn = current - previous, edgeOut = next - current;
        glm::vec2 normalIn(edgeIn.y, -edgeIn.x), normalOut(edgeOut.y, -edgeOut.x);
        const float lengthIn = glm::length(normalIn), lengthOut = glm::length(normalOut);
        if (lengthIn > 0.0f) normalIn /= lengthIn;
        if (lengthOut > 0.0f) normalOut /= lengthOut;
        glm::vec2 normal = normalIn + normalOut;
        const float length = glm::length(normal);
        normal = length > 0.0001f ? normal / length : normalOut;
        inner[i] = AddVertex(current - normal * (Fringe * 0.5f), color);
        outer[i] = AddVertex(current + normal * (Fringe * 0.5f), transparent);
    }
    for (size_t i = 1; i + 1 < count; ++i)
        for (const uint16_t index : {inner[0], inner[i], inner[i + 1]})
            m_Mesh.AddIndex(index);
    for (size_t i = 0; i < count; ++i)
    {
        const size_t next = (i + 1) % count;
        for (const uint16_t index : {inner[i], outer[i], outer[next], inner[i], outer[next], inner[next]})
            m_Mesh.AddIndex(index);
    }
}

void ShapeBatch::AddRoundedRect(float left, float bottom, float right, float top, float radius, const glm::vec4& color)
{
    if (right <= left || top <= bottom)
        return;
    const float limit = std::fmin(right - left, top - bottom) * 0.5f;
    if (radius > limit)
        radius = limit;
    if (radius < 0.5f)
    {
        AddRect(left, bottom, right, top, color);
        return;
    }

    const int steps = SegmentsFor(radius) / 4 + 1;
    const glm::vec2 centers[4] = {glm::vec2(right - radius, top - radius), glm::vec2(left + radius, top - radius),
                                  glm::vec2(left + radius, bottom + radius), glm::vec2(right - radius, bottom + radius)};
    std::vector<glm::vec2> points;
    points.reserve(static_cast<size_t>(steps + 1) * 4);
    for (int corner = 0; corner < 4; ++corner)
        for (int step = 0; step <= steps; ++step)
        {
            const float angle = (static_cast<float>(corner) + static_cast<float>(step) / static_cast<float>(steps)) * Pi * 0.5f;
            points.push_back(centers[corner] + glm::vec2(std::cos(angle), std::sin(angle)) * radius);
        }
    AddConvexPolygon(points, color);
}

void ShapeBatch::AddCircle(const glm::vec2& center, float radius, const glm::vec4& color)
{
    if (radius <= 0.0f)
        return;
    const int segments = SegmentsFor(radius);
    std::vector<glm::vec2> points;
    points.reserve(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i)
    {
        const float angle = static_cast<float>(i) / static_cast<float>(segments) * Pi * 2.0f;
        points.push_back(center + glm::vec2(std::cos(angle), std::sin(angle)) * radius);
    }
    AddConvexPolygon(points, color);
}

void ShapeBatch::AddRing(const glm::vec2& center, float innerRadius, float outerRadius, const glm::vec4& color)
{
    if (outerRadius <= innerRadius || innerRadius < 0.0f)
        return;
    const int segments = SegmentsFor(outerRadius);
    if (!HasRoomFor(static_cast<size_t>(segments) * 4))
        return;

    const glm::vec4 transparent(color.r, color.g, color.b, 0.0f);
    // Per direction, from the outside in: fading edge, ring, ring, fading edge.
    std::vector<uint16_t> indices(static_cast<size_t>(segments) * 4);
    for (int i = 0; i < segments; ++i)
    {
        const float angle = static_cast<float>(i) / static_cast<float>(segments) * Pi * 2.0f;
        const glm::vec2 direction(std::cos(angle), std::sin(angle));
        indices[i * 4 + 0] = AddVertex(center + direction * (outerRadius + Fringe * 0.5f), transparent);
        indices[i * 4 + 1] = AddVertex(center + direction * (outerRadius - Fringe * 0.5f), color);
        indices[i * 4 + 2] = AddVertex(center + direction * (innerRadius + Fringe * 0.5f), color);
        indices[i * 4 + 3] = AddVertex(center + direction * (innerRadius - Fringe * 0.5f), transparent);
    }
    for (int i = 0; i < segments; ++i)
    {
        const int next = (i + 1) % segments;
        for (int band = 0; band < 3; ++band)
        {
            const uint16_t a = indices[i * 4 + band], b = indices[i * 4 + band + 1];
            const uint16_t c = indices[next * 4 + band + 1], d = indices[next * 4 + band];
            for (const uint16_t index : {a, b, c, a, c, d})
                m_Mesh.AddIndex(index);
        }
    }
}

void ShapeBatch::Render(const glm::mat4& projection)
{
    if (m_Mesh.GetVertices().empty())
        return;

    m_Shader.Bind();
    m_Shader.SetUniform("u_ProjectionMatrix", projection);
    m_Mesh.Update();
    m_Mesh.Render();
    m_Shader.Unbind();
    m_Mesh.Clear();
}
