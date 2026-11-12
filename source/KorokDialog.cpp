#include "KorokDialog.h"

#include <algorithm>

#include "Data.h"
#include "Graphics/Texture2D.h"
#include "Map.h"
#include "MapObject.hpp"
#include "SavefileIO.h"
#include "Localization.h"
#include "Ui.h"

KorokDialog::KorokDialog()
{
    m_Icon.Create("romfs:/korokseed.png");
}

void KorokDialog::Render()
{
    if (!m_IsOpen || m_KorokIndex < 0 || m_KorokIndex >= Data::KoroksCount)
        return;

    const float left = Ui::PanelLeft();
    const float inner = Ui::PanelInner();
    const glm::vec2& mapPosition = Map::m_Koroks[m_KorokIndex].m_Position;

    Ui::PanelBackground();

    const std::string title = Localization::Get(Localization::Text::KorokSeed) + " #" + std::to_string(m_Seed);
    float top = Ui::CardHeader(&m_Icon, 40.0f, title, Map::NearestRankText());

    const bool found = Map::m_Koroks[m_KorokIndex].m_Found;
    if (found)
        top = Ui::StatusRow(top, Localization::Get(Map::IsManuallyMarked(Data::ObjectType::Korok, Map::m_Koroks[m_KorokIndex].m_ObjectData->hash)
                                                       ? Localization::Text::MarkedByYou : Localization::Text::Found),
                            Ui::Colors::Good);
    else
        top = Ui::StatusRow(top, Localization::Get(Localization::Text::NotFound), Ui::Colors::Warn, false);

    const std::string distance = Ui::DistanceText(mapPosition);
    if (!distance.empty())
        top = Ui::Paragraph(top, distance, 0.40f, Ui::Colors::TextDim);
    top -= 8.0f;

    // The photo of the guide, as wide as the panel
    if (HasImage())
    {
        const float width = static_cast<float>(m_Image->m_Texture->m_Width);
        const float height = static_cast<float>(m_Image->m_Texture->m_Height);
        m_Image->m_Scale = std::min(inner / width, 240.0f / height);
        m_Image->m_Position = glm::vec2(left + width * m_Image->m_Scale / 2.0f, top - height * m_Image->m_Scale / 2.0f);
        top -= height * m_Image->m_Scale + 18.0f;
    }

    Ui::Paragraph(top, m_Text, 0.42f, Ui::Colors::Text);
    Ui::Coordinates(mapPosition);

    Ui::FlushShapes();
    m_Icon.Render();
    if (HasImage())
    {
        m_Image->m_ProjectionMatrix = &Map::m_ProjectionMatrix;
        m_Image->m_ViewMatrix = nullptr;
        m_Image->Render();
    }
}

void KorokDialog::SetOpen(bool open)
{
    if (open)
        Map::CloseInfoPanels();
    m_IsOpen = open;
}

void KorokDialog::SetSeed(int seed, int korokIndex)
{
    m_Seed = seed;
    m_Text = Data::KorokInfos.at(seed).text;
    m_KorokIndex = korokIndex;

    // Set image
    std::string seedStr = std::to_string(seed);
    if (seedStr.length() == 1)
        seedStr = "00" + seedStr;
    if (seedStr.length() == 2)
        seedStr = "0" + seedStr;

    // Create a new image, so delete the old one first
    if (m_Image)
        delete m_Image;

    m_Image = nullptr;

    // Don't show images if they don't exist
    // Check the romfs:/ (for release builds) and the sdmc:/ (for development builds)
    std::string path = "";
    if (SavefileIO::DirectoryExists("romfs:/guide") && SavefileIO::FileExists("romfs:/guide/Korok" + seedStr + ".jpg"))
        path = "romfs:/guide/Korok" + seedStr + ".jpg";
    else if (SavefileIO::DirectoryExists("sdmc:/switch/botw-unexplored/guide") && SavefileIO::FileExists("sdmc:/switch/botw-unexplored/guide/Korok" + seedStr + ".jpg"))
        path = "sdmc:/switch/botw-unexplored/guide/Korok" + seedStr + ".jpg";

    // Neither exists
    if (path == "")
        return;

    m_Image = new TexturedQuad();
    m_Image->Create(path);
}

KorokDialog::~KorokDialog()
{
    delete m_Image;
    m_Image = nullptr;
}

bool KorokDialog::HasImage() const
{
    return m_Image && m_Image->m_Texture && m_Image->m_Texture->m_Width > 0 && m_Image->m_Texture->m_Height > 0;
}

bool KorokDialog::IsPositionOn(const glm::vec2& position) const
{
    return position.x >= Map::m_ScreenLeft && position.x <= Map::m_ScreenLeft + Ui::PanelWidth &&
           position.y >= Map::m_ScreenBottom && position.y <= Map::m_ScreenTop;
}

bool KorokDialog::IsImagePositionOn(const glm::vec2& position) const
{
    if (!HasImage())
        return false;

    const float halfWidth = m_Image->m_Texture->m_Width * m_Image->m_Scale / 2.0f;
    const float halfHeight = m_Image->m_Texture->m_Height * m_Image->m_Scale / 2.0f;
    return position.x >= m_Image->m_Position.x - halfWidth && position.x <= m_Image->m_Position.x + halfWidth &&
           position.y >= m_Image->m_Position.y - halfHeight && position.y <= m_Image->m_Position.y + halfHeight;
}
