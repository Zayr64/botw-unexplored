#pragma once

#include <string>

#include "Graphics/Quad.h"

// The card of a Korok seed: where it is from Link, the guide's photo and how to get it.
class KorokDialog
{
public:
    KorokDialog();

    void Render();

    void SetSeed(int seed, int korokIndex);
    void SetOpen(bool open);

    bool IsPositionOn(const glm::vec2& position) const;
    bool IsImagePositionOn(const glm::vec2& position) const;
    bool HasImage() const;

    ~KorokDialog();

public:
    TexturedQuad m_Icon;
    TexturedQuad* m_Image = nullptr;

    std::string m_Text;
    int m_Seed = -1;
    int m_KorokIndex = -1;

    bool m_IsOpen = false;
};
