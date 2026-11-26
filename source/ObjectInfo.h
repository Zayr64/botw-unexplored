#pragma once

#include <string>

#include "Chests.h"
#include "QuestMap.h"
#include "ObjectTypes.h"
#include "Graphics/Quad.h"

class ObjectInfo
{
public:
    ObjectInfo();
    // `chest`: for a chest, its group (the title) and what it holds (shown at once, since zayr19).
    // `quest`: for a quest, its kind, name, place and description.
    void SetObject(Data::ObjectType type, uint32_t completionHash, const glm::vec2& mapPosition,
                   const std::string& name, bool* found, const Data::Chest* chest = nullptr,
                   const QuestMap::Entry* quest = nullptr);
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
    const QuestMap::Entry* m_Quest = nullptr;
    glm::vec2 m_MapPosition = glm::vec2(0.0f);
    std::string m_Name;
};
