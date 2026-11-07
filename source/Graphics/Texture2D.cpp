#include "Texture2D.h"

#include <glad/glad.h>
#include <stb/stb_image.h>

#include "../Log.h"
#include "../ImageOverride.h"
#include <unistd.h>

Texture2D::Texture2D()
{

}

Texture2D::Texture2D(const std::string& filepath)
{
	Load(filepath);
}

void Texture2D::Load(const std::string& filepath)
{
	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, &m_Texture);
	glBindTexture(GL_TEXTURE_2D, m_Texture);

	// Set the texture wrapping/filtering options on the currently bound texture object.
	// Mipmaps keep images clean when they are drawn smaller than their size (icons, the map zoomed out).
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	stbi_set_flip_vertically_on_load(true);

	// A sharper copy on the SD card replaces the image, drawn at the size of the original.
	int logicalWidth = 0, logicalHeight = 0, logicalChannels = 0;
	unsigned char* data = nullptr;
	const std::string overridePath = ImageOverride::Candidate(filepath);
	if (!overridePath.empty() && access(overridePath.c_str(), R_OK) == 0 &&
		stbi_info(filepath.c_str(), &logicalWidth, &logicalHeight, &logicalChannels))
	{
		data = stbi_load(overridePath.c_str(), &m_Width, &m_Height, &m_NrChannels, 0);
		if (data)
			Log("Texture from SD", overridePath);
		else
			Log("Could not load texture from SD, using the built-in one", overridePath);
	}
	const bool overridden = data != nullptr;
	if (!data)
		data = stbi_load(filepath.c_str(), &m_Width, &m_Height, &m_NrChannels, 0);

	if (data)
	{
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // rows of RGB images are not always a multiple of 4 bytes
		if (m_NrChannels == 3)
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_Width, m_Height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
		if (m_NrChannels == 4)
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glGenerateMipmap(GL_TEXTURE_2D);
		if (overridden)
		{
			m_Width = logicalWidth;
			m_Height = logicalHeight;
		}
	}
	else
	{
		if (filepath != "")
			Log("Failed to load texture", filepath);
		else
			Log("Failed to load texture (empty path)");
	}

	stbi_image_free(data);
}

void Texture2D::Bind()
{
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_Texture);
}

void Texture2D::Unbind()
{
	glBindTexture(GL_TEXTURE_2D, 0);
}

Texture2D::~Texture2D()
{
    if (m_Texture != 0)
    {
        glDeleteTextures(1, &m_Texture);
        m_Texture = 0;
    }
}
