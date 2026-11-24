#include "Icons.h"

#include <cstdio>
#include <sys/stat.h>

#include <glad/glad.h>

#include "Graphics/Shader.h"
#include "Graphics/Texture2D.h"
#include "Log.h"

namespace Icons
{
    namespace
    {
        const char* Folder = "sdmc:/switch/botw-compendium/icons/";

        struct Draw
        {
            Texture2D* texture;
            float x, top, size;
            glm::vec4 tint;
        };

        Shader s_shader;
        unsigned int s_vao = 0;
        unsigned int s_vbo = 0;
        bool s_ready = false;
        std::map<std::string, Texture2D*> s_textures; // nullptr: the file is missing
        std::vector<Draw> s_queue;

        Texture2D* Get(const std::string& path)
        {
            auto it = s_textures.find(path);
            if (it != s_textures.end())
                return it->second;
            Texture2D* texture = nullptr;
            struct stat info;
            if (stat(path.c_str(), &info) == 0)
            {
                texture = new Texture2D(path);
                if (texture->m_Width <= 0)
                {
                    delete texture;
                    texture = nullptr;
                }
            }
            s_textures[path] = texture;
            return texture;
        }

        void Queue(const std::string& path, float x, float top, float size, const glm::vec4& tint)
        {
            if (!s_ready)
                return;
            Texture2D* texture = Get(path);
            if (texture)
                s_queue.push_back({texture, x, top, size, tint});
        }
    }

    void Init()
    {
        const std::string vertex = R"text(
            #version 330 core
            layout (location = 0) in vec2 corner;
            uniform mat4 u_Projection;
            uniform vec4 u_Rect;
            out vec2 uv;
            void main()
            {
                uv = corner;
                gl_Position = u_Projection * vec4(u_Rect.xy + corner * u_Rect.zw, 0.0, 1.0);
            }
        )text";
        const std::string fragment = R"text(
            #version 330 core
            in vec2 uv;
            uniform sampler2D tex;
            uniform vec4 u_Tint;
            out vec4 color;
            void main()
            {
                color = texture(tex, uv) * u_Tint;
            }
        )text";
        s_shader = ShaderLoader::CreateShaderFromSource(vertex, fragment);
        const float corners[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        glGenVertexArrays(1, &s_vao);
        glGenBuffers(1, &s_vbo);
        glBindVertexArray(s_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glBindVertexArray(0);
        s_ready = true;
    }

    void Destroy()
    {
        for (auto& entry : s_textures)
            delete entry.second;
        s_textures.clear();
        s_queue.clear();
        if (s_ready)
        {
            glDeleteBuffers(1, &s_vbo);
            glDeleteVertexArrays(1, &s_vao);
            s_shader.Delete();
            s_ready = false;
        }
    }

    void Item(const std::string& actor, float x, float top, float size, const glm::vec4& tint)
    {
        Queue(std::string(Folder) + "items/" + actor + ".png", x, top, size, tint);
    }

    void Ui(const std::string& name, float x, float top, float size, const glm::vec4& tint)
    {
        Queue(std::string(Folder) + "ui/" + name + ".png", x, top, size, tint);
    }

    void Enemy(const std::string& actor, float x, float top, float size)
    {
        Queue(std::string(Folder) + "enemies/" + actor + ".jpg", x, top, size, glm::vec4(1.0f));
    }

    void Flush(const glm::mat4& projection, float screenHeight)
    {
        if (!s_ready || s_queue.empty())
        {
            s_queue.clear();
            return;
        }
        s_shader.Bind();
        s_shader.SetUniform("u_Projection", projection);
        s_shader.SetUniform("tex", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(s_vao);
        for (const Draw& d : s_queue)
        {
            // Keep the image's proportions inside the square.
            const float w = d.texture->m_Width >= d.texture->m_Height ? d.size
                                                                      : d.size * d.texture->m_Width / d.texture->m_Height;
            const float h = d.texture->m_Height >= d.texture->m_Width ? d.size
                                                                       : d.size * d.texture->m_Height / d.texture->m_Width;
            const float x = d.x + (d.size - w) / 2;
            const float bottom = screenHeight - d.top - d.size + (d.size - h) / 2;
            s_shader.SetUniform("u_Rect", glm::vec4(x, bottom, w, h));
            s_shader.SetUniform("u_Tint", d.tint);
            glBindTexture(GL_TEXTURE_2D, d.texture->m_Texture);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        }
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        s_shader.Unbind();
        s_queue.clear();
    }
}
