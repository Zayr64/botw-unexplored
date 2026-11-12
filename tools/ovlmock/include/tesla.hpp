// Host fake of the libultrahand (Tesla) API the overlay uses: enough to draw its screens into an image.
#pragma once
#include <switch.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>

inline bool FullMode = true;
inline bool deactivateOriginalFooter = false;
inline std::atomic<bool> launchComboHasTriggered{false};
inline std::atomic<bool> comboReturnToSelfMenu{false};

namespace tsl
{
    // Four bits per channel, like the overlay framebuffer.
    struct Color
    {
        u8 r, g, b, a;
        constexpr Color(u8 r, u8 g, u8 b, u8 a) : r(r), g(g), b(b), a(a) {}
    };

    namespace cfg
    {
        inline u16 FramebufferWidth = 448;
        inline u16 FramebufferHeight = 720;
        inline u16 ScreenWidth = 1920;
        inline u16 ScreenHeight = 1080;
        inline u16 LayerWidth = 672; // the framebuffer scaled to the 1080p screen
        inline u64 launchCombo = KEY_ZL | KEY_ZR | KEY_DOWN;
        inline u64 launchCombo2 = 0;
    }

    namespace gfx
    {
        class Renderer
        {
        public:
            static Renderer& get();
            void setLayerPos(u32 x, u32 y);
            std::pair<s32, s32> drawString(const std::string& text, bool monospace, s32 x, s32 y, u32 fontSize,
                                           const Color& color, ssize_t maxWidth = 0, bool draw = true);
            std::pair<s32, s32> getTextDimensions(const std::string& text, bool monospace, u32 fontSize, ssize_t maxWidth = 0)
            {
                return drawString(text, monospace, 0, 0, fontSize, Color(0, 0, 0, 0), maxWidth, false);
            }
            void drawRect(s32 x, s32 y, s32 w, s32 h, const Color& color);
            void drawCircle(s32 centerX, s32 centerY, u16 radius, bool filled, const Color& color);
        };
    }

    namespace elm
    {
        class Element
        {
        public:
            virtual ~Element() {}
            virtual void draw(gfx::Renderer* renderer) = 0;
        };

        class CustomDrawer : public Element
        {
        public:
            explicit CustomDrawer(std::function<void(gfx::Renderer*, s32, s32, s32, s32)> drawer) : m_drawer(std::move(drawer)) {}
            void setBoundaries(s32 x, s32 y, s32 w, s32 h) { m_x = x; m_y = y; m_w = w; m_h = h; }
            void draw(gfx::Renderer* renderer) override { m_drawer(renderer, m_x, m_y, m_w, m_h); }

        private:
            std::function<void(gfx::Renderer*, s32, s32, s32, s32)> m_drawer;
            s32 m_x = 0, m_y = 0, m_w = 0, m_h = 0;
        };

        // Title, subtitle, content and the Back / OK footer.
        class OverlayFrame : public Element
        {
        public:
            OverlayFrame(const std::string& title, const std::string& subtitle) : m_title(title), m_subtitle(subtitle) {}
            void setContent(CustomDrawer* content) { m_content.reset(content); }
            void draw(gfx::Renderer* renderer) override;

        private:
            std::string m_title, m_subtitle;
            std::unique_ptr<CustomDrawer> m_content;
        };

        // With FullMode off (Status Monitor's mini mode) it draws nothing but its content.
        class HeaderOverlayFrame : public Element
        {
        public:
            HeaderOverlayFrame(const std::string&, const std::string&) {}
            void setContent(CustomDrawer* content) { m_content.reset(content); }
            void draw(gfx::Renderer* renderer) override;

        private:
            std::unique_ptr<CustomDrawer> m_content;
        };
    }

    class Gui
    {
    public:
        virtual ~Gui() {}
        virtual elm::Element* createUI() = 0;
        virtual void update() {}
        virtual bool handleInput(u64, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) { return false; }
    };

    class Overlay
    {
    public:
        virtual ~Overlay() {}
        static Overlay* get();
        virtual void initServices() {}
        virtual void exitServices() {}
        virtual void onShow() {}
        virtual std::unique_ptr<Gui> loadInitialGui() = 0;
        void close();

    protected:
        template <typename T>
        std::unique_ptr<Gui> initially() { return std::make_unique<T>(); }
    };

    void setNextOverlay(const std::string& path, const std::string& args);

    namespace hlp
    {
        void requestForeground(bool enabled);
    }

    int MockLoop(Overlay* overlay);

    template <typename T>
    int loop(int, char**)
    {
        T overlay;
        return MockLoop(&overlay);
    }
}
