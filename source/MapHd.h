#pragma once

#include <glm/vec2.hpp>
#include <glm/mat4x4.hpp>

// Optional high-resolution map drawn over the low-resolution one when zoomed in. The chunks are not part of the
// app: they are read from sdmc:/switch/botw-unexplored/map-hd/ and only used when the app runs as an application
// (title override or forwarder), which has the memory for them. From the Album the low-resolution map is kept.
namespace MapHd
{
    void Init();
    void Update(const glm::vec2& cameraPosition, float zoom, float halfWidth, float halfHeight);
    void Render(glm::mat4* projectionMatrix, glm::mat4* viewMatrix);
    void Destroy();
}
