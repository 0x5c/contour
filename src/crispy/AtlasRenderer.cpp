/**
 * This file is part of the "contour" project.
 *   Copyright (c) 2020 Christian Parpart <christian@parpart.family>
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
#include <crispy/AtlasRenderer.h>
#include <crispy/Atlas.h>

#include <QtGui/QOpenGLExtraFunctions>
#include <QtGui/QOpenGLTexture>

#include <algorithm>
#include <iostream>

using namespace std;

// TODO: check for GL_OUT_OF_MEMORY in GL allocation/store functions

namespace crispy::atlas {

struct Renderer::ExecutionScheduler
{
    std::vector<CreateAtlas> createAtlases;
    std::vector<UploadTexture> uploadTextures;
    std::vector<RenderTexture> renderTextures;
    std::vector<GLfloat> vecs;
    std::vector<DestroyAtlas> destroyAtlases;

    void operator()(CreateAtlas const& _atlas)
    {
        createAtlases.emplace_back(_atlas);
    }

    void operator()(UploadTexture const& _texture)
    {
        uploadTextures.emplace_back(_texture);
    }

    void operator()(RenderTexture const& _texture)
    {
        renderTextures.emplace_back(_texture);
    }

    void operator()(DestroyAtlas const& _atlas)
    {
        destroyAtlases.push_back(_atlas);
    }

    void clear()
    {
        createAtlases.clear();
        uploadTextures.clear();
        vecs.clear();
        destroyAtlases.clear();
    }
};

Renderer::Renderer() :
    scheduler_{std::make_unique<ExecutionScheduler>()}
{
    initializeOpenGLFunctions();

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    // verbex buffer
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);

    // texture coordinates buffer
    glGenBuffers(1, &texCoordsBuffer_);
    glBindBuffer(GL_ARRAY_BUFFER, texCoordsBuffer_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_INT, GL_FALSE, 0, nullptr);

    // texture Id buffer
    glGenBuffers(1, &texIdBuffer_);
    glBindBuffer(GL_ARRAY_BUFFER, texIdBuffer_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_INT, GL_FALSE, 0, nullptr);
}

Renderer::~Renderer()
{
    for ([[maybe_unused]] auto [_, textureId] : atlasMap_)
        glDeleteTextures(1, &textureId);

    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &vbo_);
    glDeleteBuffers(1, &texCoordsBuffer_);
    glDeleteBuffers(1, &texIdBuffer_);
}

unsigned Renderer::maxTextureDepth()
{
    GLint value;
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &value);
    return static_cast<unsigned>(value);
}

unsigned Renderer::maxTextureSize()
{
    GLint value = {};
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
    return static_cast<unsigned>(value);
}

void Renderer::setProjection(QMatrix4x4 const& _projection)
{
    projection_ = _projection;
}

void Renderer::schedule(std::vector<Command> const& _commands)
{
    for (Command const& command : _commands)
        visit(*scheduler_, command);
}

void Renderer::schedule(std::vector<DestroyAtlas> const& _commands)
{
    for (auto const& command : _commands)
        (*scheduler_)(command);
}

/// Executes all prepared commands in proper order
///
/// First call execute(CommandList&) in order to prepare and fill command queue.
void Renderer::execute()
{
    cout << "atlas::Renderer::execute()\n";

    using namespace std;
    using namespace std::placeholders;

    // potentially create new atlases
    for_each(scheduler_->createAtlases.begin(),
             scheduler_->createAtlases.end(),
             bind(&Renderer::createAtlas, this, _1));

    // potentially upload any new textures
    for_each(scheduler_->uploadTextures.begin(),
             scheduler_->uploadTextures.end(),
             bind(&Renderer::uploadTexture, this, _1));

    // order and prepare texture geometry
    sort(scheduler_->renderTextures.begin(),
         scheduler_->renderTextures.end(),
         [](RenderTexture const& a, RenderTexture const& b) { return a.texture.get().atlas < b.texture.get().atlas; });

    for_each(scheduler_->renderTextures.begin(),
             scheduler_->renderTextures.end(),
             bind(&Renderer::renderTexture, this, _1));

    // upload vertices and render (iff there is anything to render)
    if (!scheduler_->renderTextures.empty())
    {
        // upload vertices
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     scheduler_->vecs.size() * sizeof(GLfloat),
                     scheduler_->vecs.data(),
                     GL_STATIC_DRAW);

        glBindVertexArray(vao_);

        glDrawArrays(GL_TRIANGLES, 0, scheduler_->vecs.size() * 6);

        // TODO: Instead of on glDrawArrays (and many if's in the shader for each GL_TEXTUREi),
        //       make a loop over each GL_TEXTUREi and draw a sub range of the vertices and a
        //       fixed GL_TEXTURE0. - will this be noticable faster?
    }

    // destroy any pending atlases that were meant to be destroyed
    for_each(scheduler_->destroyAtlases.begin(),
             scheduler_->destroyAtlases.end(),
             bind(&Renderer::destroyAtlas, this, _1));

    // reset execution state
    scheduler_->clear();
}

void Renderer::createAtlas(CreateAtlas const& _atlas)
{
    cout << "atlas::Renderer::createAtlas: " << _atlas << endl;

    constexpr GLuint internalFormat = GL_R8; //GL_RED; // TODO: configurable

    GLuint textureId{};
    glGenTextures(1, &textureId);
    bindTexture2DArray(textureId);

    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, internalFormat, _atlas.width, _atlas.height, _atlas.depth);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    atlasMap_[_atlas.atlas] = textureId;
}

void Renderer::uploadTexture(UploadTexture const& _texture)
{
    cout << "atlas::Renderer::uploadTexture: " << _texture << endl;

    auto const textureId = atlasMap_[_texture.atlas];
    auto const x0 = _texture.x;
    auto const y0 = _texture.y;
    auto const z0 = _texture.z;
    auto const internalFormat = GL_RED; // TODO: configure me

    auto constexpr target = GL_TEXTURE_2D_ARRAY;
    auto constexpr levelOfDetail = 0;
    auto constexpr depth = 1;
    auto constexpr type = GL_UNSIGNED_BYTE;

    bindTexture2DArray(textureId);

    glTexSubImage3D(target, levelOfDetail, x0, y0, z0, _texture.width, _texture.height, depth,
                    internalFormat, type, _texture.data.data());
}

void Renderer::renderTexture(RenderTexture const& _render)
{
    cout << "atlas::Renderer::renderTexture: " << _render << endl;

    if (auto const it = atlasMap_.find(_render.texture.get().atlas); it != atlasMap_.end())
    {
        GLuint const textureId = it->second;
        setActiveTexture(_render.texture.get().atlas);
        bindTexture2DArray(textureId);

        GLfloat const xpos = _render.x;
        GLfloat const ypos = _render.y;
        GLfloat const w = _render.width;
        GLfloat const h = _render.height;

        GLfloat const vertices[6][4] = {
            { xpos,     ypos + h,   0.0, 0.0 },
            { xpos,     ypos,       0.0, 1.0 },
            { xpos + w, ypos,       1.0, 1.0 },
            { xpos,     ypos + h,   0.0, 0.0 },
            { xpos + w, ypos,       1.0, 1.0 },
            { xpos + w, ypos + h,   1.0, 0.0 }
        };

        // TODO: extend host buffers for vertex/TexCoords/TexID data.
        // scheduler_.vecs.push_back(...)
        // ...
        (void) vertices; // TODO
    }
}

void Renderer::destroyAtlas(DestroyAtlas const& _atlas)
{
    if (auto const it = atlasMap_.find(_atlas.atlas); it != atlasMap_.end())
    {
        GLuint const textureId = it->second;
        atlasMap_.erase(it);
        glDeleteTextures(1, &textureId);
    }
}

void Renderer::bindTexture2DArray(GLuint _textureId)
{
    if (currentTextureId_ != _textureId)
    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, _textureId);
        currentTextureId_ = _textureId;
    }
}

void Renderer::setActiveTexture(unsigned _id)
{
    if (currentActiveTexture_ != _id)
    {
        glActiveTexture(GL_TEXTURE0 + _id);
        currentActiveTexture_ = _id;
    }
}

} // end namespace
