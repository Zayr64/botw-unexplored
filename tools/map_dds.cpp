// Turns a map image (the chunks of map-hd/ or romfs/map-lowres.png) into a DDS with BC1 (DXT1) compression and every
// mipmap level, the way the app uploads it: rows from the bottom up (the app loads its textures flipped).
//
//   map_dds <in.jpg|png> <out.dds> [--preview <decoded.bmp>] [--halved <halved.bmp>]
//
// --preview writes level 0 decoded back from BC1, --halved the image at half size as the app makes it from the Album
// (2 x 2 means), both top row first, to compare with the original. BC1 keeps 4 bits a pixel (a 1024 x 1024 chunk with
// its mipmaps takes 0.7 MB on the GPU instead of 5.6 MB as RGB, which the GPU stores as RGBA).
//
// Build: g++ -O2 -std=c++14 -Iinclude tools/map_dds.cpp -o map_dds
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    struct Image
    {
        int width = 0, height = 0;
        std::vector<float> rgb; // width * height * 3, top row first
    };

    struct Color
    {
        float r, g, b;
    };

    uint16_t To565(const Color& c)
    {
        const int r = std::min(31, std::max(0, static_cast<int>(std::lround(c.r * 31.0f / 255.0f))));
        const int g = std::min(63, std::max(0, static_cast<int>(std::lround(c.g * 63.0f / 255.0f))));
        const int b = std::min(31, std::max(0, static_cast<int>(std::lround(c.b * 31.0f / 255.0f))));
        return static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }

    Color From565(uint16_t v)
    {
        const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        return {static_cast<float>((r << 3) | (r >> 2)), static_cast<float>((g << 2) | (g >> 4)),
                static_cast<float>((b << 3) | (b >> 2))};
    }

    // The four colors of a block in 4-color mode (c0 > c1).
    void Palette(uint16_t c0, uint16_t c1, Color out[4])
    {
        out[0] = From565(c0);
        out[1] = From565(c1);
        // As the hardware does it, rounded the usual way: (2 a + b) / 3
        out[2] = {(2 * out[0].r + out[1].r) / 3.0f, (2 * out[0].g + out[1].g) / 3.0f, (2 * out[0].b + out[1].b) / 3.0f};
        out[3] = {(out[0].r + 2 * out[1].r) / 3.0f, (out[0].g + 2 * out[1].g) / 3.0f, (out[0].b + 2 * out[1].b) / 3.0f};
    }

    float Distance(const Color& a, const Color& b)
    {
        const float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
        return dr * dr + dg * dg + db * db;
    }

    // Indices and error of 16 pixels against two endpoints; swaps them into 4-color mode.
    float Assign(const Color px[16], uint16_t& c0, uint16_t& c1, uint8_t idx[16])
    {
        if (c0 < c1)
            std::swap(c0, c1);
        if (c0 == c1)
        {
            // One color: every index 0 (4-color mode needs c0 > c1; with c0 == c1 index 0 is still c0)
            const Color c = From565(c0);
            float err = 0.0f;
            for (int i = 0; i < 16; i++)
            {
                idx[i] = 0;
                err += Distance(px[i], c);
            }
            return err;
        }
        Color pal[4];
        Palette(c0, c1, pal);
        float err = 0.0f;
        for (int i = 0; i < 16; i++)
        {
            int best = 0;
            float bestD = Distance(px[i], pal[0]);
            for (int k = 1; k < 4; k++)
            {
                const float d = Distance(px[i], pal[k]);
                if (d < bestD)
                {
                    bestD = d;
                    best = k;
                }
            }
            idx[i] = static_cast<uint8_t>(best);
            err += bestD;
        }
        return err;
    }

    // Endpoints that best fit the pixels for given indices (least squares), or false if degenerate.
    bool Fit(const Color px[16], const uint8_t idx[16], Color& e0, Color& e1)
    {
        static const float w0[4] = {1.0f, 0.0f, 2.0f / 3.0f, 1.0f / 3.0f};
        float aa = 0, bb = 0, ab = 0;
        Color ax = {0, 0, 0}, bx = {0, 0, 0};
        for (int i = 0; i < 16; i++)
        {
            const float a = w0[idx[i]], b = 1.0f - a;
            aa += a * a;
            bb += b * b;
            ab += a * b;
            ax.r += a * px[i].r; ax.g += a * px[i].g; ax.b += a * px[i].b;
            bx.r += b * px[i].r; bx.g += b * px[i].g; bx.b += b * px[i].b;
        }
        const float det = aa * bb - ab * ab;
        if (std::fabs(det) < 1e-6f)
            return false;
        const float f = 1.0f / det;
        e0 = {(ax.r * bb - bx.r * ab) * f, (ax.g * bb - bx.g * ab) * f, (ax.b * bb - bx.b * ab) * f};
        e1 = {(bx.r * aa - ax.r * ab) * f, (bx.g * aa - ax.g * ab) * f, (bx.b * aa - ax.b * ab) * f};
        return true;
    }

    // One 4 x 4 block to its 8 bytes: endpoints along the main axis of the colors, then two least-squares passes.
    void EncodeBlock(const Color px[16], uint8_t out[8])
    {
        Color mean = {0, 0, 0};
        for (int i = 0; i < 16; i++)
        {
            mean.r += px[i].r; mean.g += px[i].g; mean.b += px[i].b;
        }
        mean.r /= 16; mean.g /= 16; mean.b /= 16;
        float cov[6] = {0, 0, 0, 0, 0, 0}; // rr rg rb gg gb bb
        for (int i = 0; i < 16; i++)
        {
            const float r = px[i].r - mean.r, g = px[i].g - mean.g, b = px[i].b - mean.b;
            cov[0] += r * r; cov[1] += r * g; cov[2] += r * b; cov[3] += g * g; cov[4] += g * b; cov[5] += b * b;
        }
        Color axis = {1.0f, 1.0f, 1.0f};
        for (int it = 0; it < 8; it++)
        {
            const Color n = {cov[0] * axis.r + cov[1] * axis.g + cov[2] * axis.b,
                             cov[1] * axis.r + cov[3] * axis.g + cov[4] * axis.b,
                             cov[2] * axis.r + cov[4] * axis.g + cov[5] * axis.b};
            const float len = std::sqrt(n.r * n.r + n.g * n.g + n.b * n.b);
            if (len < 1e-6f)
                break;
            axis = {n.r / len, n.g / len, n.b / len};
        }
        float lo = 0.0f, hi = 0.0f;
        for (int i = 0; i < 16; i++)
        {
            const float t = (px[i].r - mean.r) * axis.r + (px[i].g - mean.g) * axis.g + (px[i].b - mean.b) * axis.b;
            lo = std::min(lo, t);
            hi = std::max(hi, t);
        }
        Color e0 = {mean.r + axis.r * hi, mean.g + axis.g * hi, mean.b + axis.b * hi};
        Color e1 = {mean.r + axis.r * lo, mean.g + axis.g * lo, mean.b + axis.b * lo};

        uint16_t bestC0 = To565(e0), bestC1 = To565(e1);
        uint8_t bestIdx[16];
        float bestErr = Assign(px, bestC0, bestC1, bestIdx);
        for (int pass = 0; pass < 2 && bestErr > 0.0f; pass++)
        {
            Color f0, f1;
            if (!Fit(px, bestIdx, f0, f1))
                break;
            uint16_t c0 = To565(f0), c1 = To565(f1);
            uint8_t idx[16];
            const float err = Assign(px, c0, c1, idx);
            if (err >= bestErr)
                break;
            bestErr = err;
            bestC0 = c0;
            bestC1 = c1;
            std::memcpy(bestIdx, idx, 16);
        }
        uint32_t bits = 0;
        for (int i = 0; i < 16; i++)
            bits |= static_cast<uint32_t>(bestIdx[i]) << (2 * i);
        out[0] = bestC0 & 0xff; out[1] = bestC0 >> 8;
        out[2] = bestC1 & 0xff; out[3] = bestC1 >> 8;
        out[4] = bits & 0xff; out[5] = (bits >> 8) & 0xff; out[6] = (bits >> 16) & 0xff; out[7] = bits >> 24;
    }

    // BC1 of an image whose rows go bottom up (row 0 is the bottom one), and its decoded copy.
    std::vector<uint8_t> Encode(const Image& img, Image* decoded)
    {
        const int bw = (img.width + 3) / 4, bh = (img.height + 3) / 4;
        std::vector<uint8_t> out(static_cast<size_t>(bw) * bh * 8);
        if (decoded)
        {
            decoded->width = img.width;
            decoded->height = img.height;
            decoded->rgb.assign(img.rgb.size(), 0.0f);
        }
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++)
            {
                Color px[16];
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 4; x++)
                    {
                        const int sx = std::min(img.width - 1, bx * 4 + x), sy = std::min(img.height - 1, by * 4 + y);
                        const float* p = &img.rgb[(static_cast<size_t>(sy) * img.width + sx) * 3];
                        px[y * 4 + x] = {p[0], p[1], p[2]};
                    }
                uint8_t* block = &out[(static_cast<size_t>(by) * bw + bx) * 8];
                EncodeBlock(px, block);
                if (decoded)
                {
                    Color pal[4];
                    const uint16_t c0 = block[0] | (block[1] << 8), c1 = block[2] | (block[3] << 8);
                    Palette(c0, c1, pal);
                    const uint32_t bits = block[4] | (block[5] << 8) | (block[6] << 16) | (static_cast<uint32_t>(block[7]) << 24);
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++)
                        {
                            const int dx = bx * 4 + x, dy = by * 4 + y;
                            if (dx >= img.width || dy >= img.height)
                                continue;
                            const Color& c = pal[(bits >> (2 * (y * 4 + x))) & 3];
                            float* d = &decoded->rgb[(static_cast<size_t>(dy) * img.width + dx) * 3];
                            d[0] = c.r; d[1] = c.g; d[2] = c.b;
                        }
                }
            }
        return out;
    }

    // Half the size, each pixel the mean of a 2 x 2 block (an odd last row or column alone), as MapHdGrid::Halve.
    Image Halve(const Image& img)
    {
        Image out;
        out.width = (img.width + 1) / 2;
        out.height = (img.height + 1) / 2;
        out.rgb.resize(static_cast<size_t>(out.width) * out.height * 3);
        for (int y = 0; y < out.height; y++)
            for (int x = 0; x < out.width; x++)
            {
                const int x0 = x * 2, y0 = y * 2;
                const int x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
                for (int c = 0; c < 3; c++)
                {
                    const auto at = [&](int px, int py) { return img.rgb[(static_cast<size_t>(py) * img.width + px) * 3 + c]; };
                    out.rgb[(static_cast<size_t>(y) * out.width + x) * 3 + c] =
                        (at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1)) / 4.0f;
                }
            }
        return out;
    }

    // The next mipmap level: half the size rounded down, as OpenGL wants the chain (an odd last row or column joins
    // the pixel before it), each pixel the mean of its block.
    Image NextLevel(const Image& img)
    {
        Image out;
        out.width = std::max(1, img.width / 2);
        out.height = std::max(1, img.height / 2);
        out.rgb.assign(static_cast<size_t>(out.width) * out.height * 3, 0.0f);
        for (int y = 0; y < out.height; y++)
            for (int x = 0; x < out.width; x++)
            {
                const int x0 = x * 2, y0 = y * 2;
                const int x1 = (x == out.width - 1) ? img.width - 1 : std::min(x0 + 1, img.width - 1);
                const int y1 = (y == out.height - 1) ? img.height - 1 : std::min(y0 + 1, img.height - 1);
                for (int c = 0; c < 3; c++)
                {
                    float sum = 0.0f;
                    int n = 0;
                    for (int sy = y0; sy <= y1; sy++)
                        for (int sx = x0; sx <= x1; sx++)
                        {
                            sum += img.rgb[(static_cast<size_t>(sy) * img.width + sx) * 3 + c];
                            n++;
                        }
                    out.rgb[(static_cast<size_t>(y) * out.width + x) * 3 + c] = sum / static_cast<float>(n);
                }
            }
        return out;
    }

    Image Flip(const Image& img)
    {
        Image out = img;
        const size_t row = static_cast<size_t>(img.width) * 3;
        for (int y = 0; y < img.height; y++)
            std::memcpy(&out.rgb[y * row], &img.rgb[(img.height - 1 - y) * row], row * sizeof(float));
        return out;
    }

    void Put32(std::vector<uint8_t>& out, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }

    bool WriteBmp(const char* path, const Image& img) // img top row first
    {
        const int pad = (4 - (img.width * 3) % 4) % 4;
        const uint32_t dataSize = static_cast<uint32_t>((img.width * 3 + pad) * img.height);
        std::vector<uint8_t> out = {'B', 'M'};
        Put32(out, 54 + dataSize); Put32(out, 0); Put32(out, 54);
        Put32(out, 40); Put32(out, static_cast<uint32_t>(img.width)); Put32(out, static_cast<uint32_t>(img.height));
        out.push_back(1); out.push_back(0); out.push_back(24); out.push_back(0);
        Put32(out, 0); Put32(out, dataSize); Put32(out, 2835); Put32(out, 2835); Put32(out, 0); Put32(out, 0);
        for (int y = img.height - 1; y >= 0; y--) // BMP rows bottom up
        {
            for (int x = 0; x < img.width; x++)
            {
                const float* p = &img.rgb[(static_cast<size_t>(y) * img.width + x) * 3];
                for (int c = 2; c >= 0; c--)
                    out.push_back(static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, std::round(p[c])))));
            }
            for (int i = 0; i < pad; i++)
                out.push_back(0);
        }
        FILE* f = std::fopen(path, "wb");
        if (!f)
            return false;
        const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
        return std::fclose(f) == 0 && ok;
    }

    double Psnr(const Image& a, const Image& b)
    {
        double sum = 0.0;
        for (size_t i = 0; i < a.rgb.size(); i++)
        {
            const double d = std::round(a.rgb[i]) - std::round(b.rgb[i]);
            sum += d * d;
        }
        const double mse = sum / a.rgb.size();
        return mse <= 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: map_dds <in.jpg|png> <out.dds> [--preview <decoded.bmp>] [--halved <halved.bmp>]\n");
        return 2;
    }
    const char* preview = nullptr;
    const char* halved = nullptr;
    for (int i = 3; i + 1 < argc; i += 2)
    {
        if (std::strcmp(argv[i], "--preview") == 0)
            preview = argv[i + 1];
        else if (std::strcmp(argv[i], "--halved") == 0)
            halved = argv[i + 1];
    }
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(argv[1], &w, &h, &channels, 3);
    if (!pixels)
    {
        std::fprintf(stderr, "map_dds: cannot read %s\n", argv[1]);
        return 1;
    }
    Image top; // top row first, as read
    top.width = w;
    top.height = h;
    top.rgb.assign(pixels, pixels + static_cast<size_t>(w) * h * 3);
    stbi_image_free(pixels);

    // Every level, bottom row first as the app uploads it.
    std::vector<std::vector<uint8_t>> levels;
    Image level = Flip(top);
    Image decoded0;
    while (true)
    {
        levels.push_back(Encode(level, levels.empty() ? &decoded0 : nullptr));
        if (level.width == 1 && level.height == 1)
            break;
        level = NextLevel(level);
    }

    std::vector<uint8_t> dds = {'D', 'D', 'S', ' '};
    Put32(dds, 124);
    Put32(dds, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000); // caps, height, width, pixel format, mipmaps, linear size
    Put32(dds, static_cast<uint32_t>(h));
    Put32(dds, static_cast<uint32_t>(w));
    Put32(dds, static_cast<uint32_t>(levels[0].size()));
    Put32(dds, 0);
    Put32(dds, static_cast<uint32_t>(levels.size()));
    for (int i = 0; i < 11; i++)
        Put32(dds, 0);
    Put32(dds, 32); Put32(dds, 0x4); // pixel format: size, FOURCC
    dds.push_back('D'); dds.push_back('X'); dds.push_back('T'); dds.push_back('1');
    for (int i = 0; i < 5; i++)
        Put32(dds, 0);
    Put32(dds, 0x8 | 0x1000 | 0x400000); // complex, texture, mipmap
    for (int i = 0; i < 4; i++)
        Put32(dds, 0);
    size_t total = 0;
    for (const auto& l : levels)
    {
        dds.insert(dds.end(), l.begin(), l.end());
        total += l.size();
    }
    FILE* f = std::fopen(argv[2], "wb");
    if (!f || std::fwrite(dds.data(), 1, dds.size(), f) != dds.size() || std::fclose(f) != 0)
    {
        std::fprintf(stderr, "map_dds: cannot write %s\n", argv[2]);
        return 1;
    }
    const Image decodedTop = Flip(decoded0);
    std::printf("%s: %d x %d, %zu levels, %zu bytes of BC1, PSNR %.2f dB\n", argv[2], w, h, levels.size(), total,
                Psnr(top, decodedTop));
    if (preview && !WriteBmp(preview, decodedTop))
        return 1;
    if (halved && !WriteBmp(halved, Halve(top)))
        return 1;
    return 0;
}
