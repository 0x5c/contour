/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019-2020 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <terminal_renderer/Atlas.h>

#include <terminal/Color.h>
#include <terminal/Grid.h> // cell attribs
#include <terminal/Image.h> // ImageFragment

#include <crispy/size.h>
#include <crispy/stdfs.h>

#include <unicode/utf8.h>

#include <array>
#include <memory>

namespace terminal::renderer {

struct RenderCell
{
    std::u32string codepoints; // TODO: I wonder if that would also work for Cell (performance-wise).
    crispy::Point position;
    CellFlags flags;
    RGBColor foregroundColor;
    RGBColor backgroundColor;
    RGBColor decorationColor;
    std::optional<ImageFragment> image;
};

struct AtlasTextureInfo {
    std::string atlasName;
    int atlasInstanceId;
    crispy::Size size;
    atlas::Format format;
    atlas::Buffer buffer;
};

/**
 * Terminal render target interface.
 *
 * @see OpenGLRenderer
 */
class RenderTarget
{
  public:
    virtual ~RenderTarget() = default;

    virtual void setRenderSize(crispy::Size _size) = 0;
    virtual void setMargin(int _left, int _bottom) = 0;

    virtual atlas::TextureAtlasAllocator& monochromeAtlasAllocator() noexcept = 0;
    virtual atlas::TextureAtlasAllocator& coloredAtlasAllocator() noexcept = 0;
    virtual atlas::TextureAtlasAllocator& lcdAtlasAllocator() noexcept = 0;

    std::array<atlas::TextureAtlasAllocator*, 3> allAtlasAllocators() noexcept
    {
        return {
            &monochromeAtlasAllocator(),
            &coloredAtlasAllocator(),
            &lcdAtlasAllocator()
        };
    }

    virtual atlas::AtlasBackend& textureScheduler() = 0;

    virtual void renderRectangle(int _x, int _y, int _width, int _height,
                                 float _r, float _g, float _b, float _a) = 0;

    using ScreenshotCallback = std::function<void(std::vector<uint8_t> const& /*_rgbaBuffer*/, crispy::Size /*_pixelSize*/)>;
    virtual void scheduleScreenshot(ScreenshotCallback _callback) = 0;

    virtual void execute() = 0;

    virtual void clearCache() = 0;

    virtual std::optional<AtlasTextureInfo> readAtlas(atlas::TextureAtlasAllocator const& _allocator, atlas::AtlasID _instanceId) = 0;
};

class Renderable {
  public:
    virtual ~Renderable() = default;

    virtual void clearCache() {}
    virtual void setRenderTarget(RenderTarget& _renderTarget) { renderTarget_ = &_renderTarget; }
    RenderTarget& renderTarget() { return *renderTarget_; }
    constexpr bool renderTargetAvailable() const noexcept { return renderTarget_; }

    atlas::TextureAtlasAllocator& monochromeAtlasAllocator() noexcept { return renderTarget_->monochromeAtlasAllocator(); }
    atlas::TextureAtlasAllocator& coloredAtlasAllocator() noexcept { return renderTarget_->coloredAtlasAllocator(); }
    atlas::TextureAtlasAllocator& lcdAtlasAllocator() noexcept { return renderTarget_->lcdAtlasAllocator(); }

    atlas::AtlasBackend& textureScheduler() { return renderTarget_->textureScheduler(); }

  protected:
    RenderTarget* renderTarget_ = nullptr;
};

} // end namespace

namespace fmt
{
    template <>
    struct formatter<terminal::renderer::RenderCell> {
        template <typename ParseContext>
        constexpr auto parse(ParseContext& ctx)
        {
            return ctx.begin();
        }

        using CellFlags = terminal::CellFlags;
        using RenderCell = terminal::renderer::RenderCell;

        template <typename FormatContext>
        auto format(RenderCell const& cell, FormatContext& ctx)
        {
            std::string s;
            if (cell.flags & CellFlags::CellSequenceStart) s += "S";
            if (cell.flags & CellFlags::CellSequenceEnd) s += "E";
            return format_to(ctx.out(),
                    "flags={} bg={} fg={} text='{}'",
                    s, cell.backgroundColor, cell.foregroundColor,
                    unicode::to_utf8(cell.codepoints));
        }
    };
}
