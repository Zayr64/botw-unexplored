#pragma once

#include <glad/glad.h>

#include <string>

class Texture2D
{
public:
	Texture2D();
	Texture2D(const std::string& filepath);

	void Load(const std::string& filepath);
	
	void Bind();
	void Unbind();

	// The video memory this image takes (0 until it is known), added to what all the images alive take.
	void Count(size_t bytes);
	static size_t LiveBytes();

	~Texture2D();

public:
	unsigned int m_Texture = 0;

	int m_Width = 0;
	int m_Height = 0;
	int m_NrChannels = 0;
	size_t m_Bytes = 0;
};

