#pragma once

#include "Graphics/Texture2D.h"
#include "Graphics/Mesh.hpp"
#include "Graphics/Shader.h" 
#include "Graphics/Font.h"
#include "Graphics/Quad.h"

#include <glm/vec2.hpp>
#include <glm/mat4x4.hpp>

#include <string>

#include <switch.h>

#include "Chests.h"
#include "Data.h"
#include "Walls.h"

template <typename T> class MapObject;

class MapLocation;
class Legend;
class Dialog;
class LineRenderer;
class KorokDialog;
class ObjectInfo;
class ImageViewer;

namespace Map
{
    bool Init();

    // recenter: move the map to where Link is (the first time a save is shown).
    void UpdateMapObjects(bool recenter = true);
    bool OpenNearestObject(const glm::vec2& mapPosition);
    void CloseInfoPanels();
    void MarkSelectedObjectFound();
    void FocusMissing(int step); // +1: next of the nearest missing objects, -1: previous
    void FocusNearestToCenter(); // the missing object nearest to the screen center
    void ClampCursorToViewport();

    // Reads the progress again: from the running game (opened from the Album) or from the save.
    void Sync();
    // Where the progress shown comes from ("Live from the game", "Save backup - 07 Nov 17:21"...).
    std::string SourceText(glm::vec3& dotColor, bool& hasDot);
    // "Nearest missing 2 of 5" while the open card is one of the nearest to Link (ZL/ZR); empty otherwise.
    std::string NearestRankText();
    // Found only because the user marked it (the save shown is an old backup).
    bool IsManuallyMarked(Data::ObjectType type, uint32_t hash);

    // The map can show one region alone (Regions.h): only its objects are drawn, counted and picked.
    extern int m_RegionFilter; // -1 for the whole map
    bool InRegionFilter(int region);
    // Chooses the region shown and brings it into view; Regions::None for the whole map.
    void SetRegionFilter(int region);

    // The map is drawn shifted to the right while the legend or a card covers the left of the screen.
    glm::vec2 MapToScreen(const glm::vec2& mapPosition);
    glm::vec2 ScreenToMap(const glm::vec2& screenPosition);

    void Update();
    void Render();

    bool IsInView(glm::vec2 position, float margin);

    void Destroy();

    // Compatibility with out-of-tree callers that still use the historical typo.
    inline void Destory() { Destroy(); }

    const float m_DefaultZoom = 0.125f * 2;
    const float m_CameraWidth = 1280.0f;
    const float m_CameraHeight = 720.0f;
    const float m_ScreenLeft = -m_CameraWidth / 2.0f;
    const float m_ScreenRight = m_CameraWidth / 2.0f;
    const float m_ScreenTop = m_CameraHeight / 2.0f;
    const float m_ScreenBottom = -m_CameraHeight / 2.0f;

    extern TexturedQuad m_MapBackground;

    extern Font m_Font;
    extern LineRenderer* m_LineRenderer;

    extern TexturedQuad m_MasterModeIcon; 

    extern float m_Zoom;
    extern float m_ViewShift; // pixels

    extern glm::mat4 m_ProjectionMatrix;
    extern glm::mat4 m_ViewMatrix;

    extern glm::vec2 m_CameraPosition;
    extern glm::vec2 m_PrevCameraPosition;

    extern int m_PrevTouchCount;
    extern glm::vec2 m_PrevTouchPosition;
    extern glm::vec2 m_StartDragPos;
    extern bool m_IsDragging;
    extern bool m_ShouldExit;
    extern bool m_LoadMasterMode;
    extern glm::vec2 m_CursorPosition;
    extern glm::vec2 m_TargetCameraPosition;
    extern bool m_HasTargetCameraPosition;
    extern bool m_CursorFollowsTarget;

    extern PadState* m_Pad;

    extern MapObject<Data::Korok>* m_Koroks;
    extern MapObject<Data::Shrine>* m_Shrines;
    extern MapObject<Data::DLCShrine>* m_DLCShrines;
    extern MapObject<Data::Hinox>* m_Hinoxes;
    extern MapObject<Data::Talus>* m_Taluses;
    extern MapObject<Data::Molduga>* m_Moldugas;
    extern MapObject<Data::ArmorChestIcon>* m_ArmorChests;       // one per entry of Data::ArmorChests
    extern MapObject<Data::WeaponChestIcon>* m_WeaponChests;     // one per entry of Data::WeaponChests
    extern MapObject<Data::MaterialChestIcon>* m_MaterialChests; // one per entry of Data::MaterialChests
    extern MapObject<Data::WallIcon>* m_Walls;                 // one per entry of Data::Walls

    extern MapLocation* m_Locations;

    extern Legend* m_Legend;
    extern KorokDialog* m_KorokDialog;
    extern ObjectInfo* m_ObjectInfo;
    extern ImageViewer* m_ImageViewer;
    extern Dialog* m_NoSavefileDialog;
    extern Dialog* m_GameRunningDialog;
    extern Dialog* m_MasterModeDialog;
};
