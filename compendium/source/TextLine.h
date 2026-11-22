#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Whitespace-separated words of one line of cook-data.txt.
namespace Compendium
{
    class TextLine
    {
    public:
        TextLine(const char* begin, const char* end) : m_p(begin), m_end(end) {}

        bool Word(char* out, size_t size)
        {
            while (m_p < m_end && (*m_p == ' ' || *m_p == '\t' || *m_p == '\r'))
                m_p++;
            if (m_p >= m_end)
                return false;
            size_t n = 0;
            while (m_p < m_end && *m_p != ' ' && *m_p != '\t' && *m_p != '\r')
            {
                if (n + 1 < size)
                    out[n++] = *m_p;
                m_p++;
            }
            out[n] = '\0';
            return true;
        }

        // The rest of the line, trimmed.
        void Rest(char* out, size_t size)
        {
            while (m_p < m_end && *m_p == ' ')
                m_p++;
            const char* end = m_end;
            while (end > m_p && (end[-1] == '\r' || end[-1] == ' '))
                end--;
            size_t n = static_cast<size_t>(end - m_p);
            if (n >= size)
                n = size - 1;
            memcpy(out, m_p, n);
            out[n] = '\0';
            m_p = m_end;
        }

        bool Hex(uint32_t& out)
        {
            char w[16];
            if (!Word(w, sizeof(w)))
                return false;
            char* end = nullptr;
            out = static_cast<uint32_t>(strtoul(w, &end, 16));
            return end && *end == '\0';
        }

        // A hex hash, or "-" for none (0).
        bool OptionalHex(uint32_t& out)
        {
            char w[16];
            if (!Word(w, sizeof(w)))
                return false;
            if (strcmp(w, "-") == 0)
            {
                out = 0;
                return true;
            }
            char* end = nullptr;
            out = static_cast<uint32_t>(strtoul(w, &end, 16));
            return end && *end == '\0';
        }

        bool Int(int32_t& out)
        {
            char w[16];
            if (!Word(w, sizeof(w)))
                return false;
            char* end = nullptr;
            out = static_cast<int32_t>(strtol(w, &end, 10));
            return end && *end == '\0';
        }

        // An integer or "-" for a value the game's files leave out.
        bool OptionalInt(int32_t& out, bool& present)
        {
            char w[16];
            if (!Word(w, sizeof(w)))
                return false;
            if (strcmp(w, "-") == 0 || strcmp(w, "?") == 0)
            {
                present = false;
                out = 0;
                return true;
            }
            char* end = nullptr;
            out = static_cast<int32_t>(strtol(w, &end, 10));
            present = true;
            return end && *end == '\0';
        }

        bool OptionalInt(int32_t& out)
        {
            bool present = false;
            return OptionalInt(out, present);
        }

        // A float stored as the hex of its 32 bits, so the value is exactly the game's.
        bool Float(float& out)
        {
            uint32_t bits = 0;
            if (!Hex(bits))
                return false;
            memcpy(&out, &bits, sizeof(out));
            return true;
        }

    private:
        const char* m_p;
        const char* m_end;
    };

    // Calls `onLine(kind, line, lineNumber)` for each line that is not empty or a comment.
    template <typename F>
    void ForEachLine(const char* text, size_t size, F onLine)
    {
        const char* p = text;
        const char* end = text + size;
        int lineNumber = 0;
        while (p < end)
        {
            const char* eol = static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(end - p)));
            if (!eol)
                eol = end;
            lineNumber++;
            TextLine line(p, eol);
            p = eol + 1;
            char kind[24];
            if (!line.Word(kind, sizeof(kind)) || kind[0] == '#')
                continue;
            if (!onLine(kind, line, lineNumber))
                return;
        }
    }
}
