#pragma once

#include <string>

#include "Chests.h"
#include "ObjectTypes.h"
#include "Graphics/Quad.h"

class ObjectInfo
{
public:
    ObjectInfo();
    // `chest`: for a chest, what the card may tell about it.
    void SetObject(Data::ObjectType type, uint32_t completionHash, const glm::vec2& mapPosition,
                   const std::string& name, bool* found, const Data::Chest* chest = nullptr);
    // What is inside a chest is told a step at a time, when asked: nothing, the kind of thing, the item. The
    // group of the rare materials already tells the kind, so there the item comes at once.
    bool CanReveal() const { return m_Chest != nullptr && m_Reveal < 2; }
    bool NextRevealIsItem() const;
    void RevealMore();
    void SetOpen(bool open);
    void Render();
    bool IsPositionOn(const glm::vec2& position) const;
    glm::vec2 GetMapPosition() const;
    ~ObjectInfo();

    bool m_IsOpen = false;
    Data::ObjectType m_Type = Data::ObjectType::Korok;
    uint32_t m_CompletionHash = 0;
    bool* m_Found = nullptr;

private:
    TexturedQuad* m_Icon = nullptr;
    const Data::Chest* m_Chest = nullptr;
    int m_Reveal = 0;
    glm::vec2 m_MapPosition = glm::vec2(0.0f);
    std::string m_Name;
};
