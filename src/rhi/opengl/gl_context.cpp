#include "rhi/opengl/gl_context.h"
#include "rhi/opengl/gl_command_list.h"
#include "rhi/opengl/gl_resources.h"

// ============================================================
//  Command list submission — replays recorded commands on GL thread
// ============================================================

void GLContext::submit(RHICommandList* cmdList)
{
    auto* glCmdList = static_cast<GLCommandList*>(cmdList);

    for (auto& command : glCmdList->commands()) {
        std::visit([this](auto&& c) {
            using T = std::decay_t<decltype(c)>;

            if constexpr (std::is_same_v<T, cmd::BeginPass>)           beginPass(c.fb);
            else if constexpr (std::is_same_v<T, cmd::BeginDefaultPass>) beginDefaultPass();
            else if constexpr (std::is_same_v<T, cmd::EndPass>)        endPass();

            else if constexpr (std::is_same_v<T, cmd::SetViewport>)    setViewport(c.x, c.y, c.w, c.h);
            else if constexpr (std::is_same_v<T, cmd::Clear>)          clear(c.r, c.g, c.b, c.a, c.depth);
            else if constexpr (std::is_same_v<T, cmd::ClearColor>)     clearColor(c.r, c.g, c.b, c.a);
            else if constexpr (std::is_same_v<T, cmd::ClearDepth>)     clearDepth(c.depth);

            else if constexpr (std::is_same_v<T, cmd::SetDepthTest>)       setDepthTest(c.enable, c.fn);
            else if constexpr (std::is_same_v<T, cmd::SetDepthWrite>)      setDepthWrite(c.enable);
            else if constexpr (std::is_same_v<T, cmd::SetSeamlessCubemap>) setSeamlessCubemap(c.enable);
            else if constexpr (std::is_same_v<T, cmd::SetMultisample>)     setMultisample(c.enable);

            else if constexpr (std::is_same_v<T, cmd::BindShader>)      bindShader(c.shader);
            else if constexpr (std::is_same_v<T, cmd::BindVertexInput>) bindVertexInput(c.vi);
            else if constexpr (std::is_same_v<T, cmd::BindTexture>)     bindTexture(c.slot, c.tex);

            else if constexpr (std::is_same_v<T, cmd::Draw>)                  draw(c.pt, c.count, c.first);
            else if constexpr (std::is_same_v<T, cmd::DrawIndexed>)            drawIndexed(c.pt, c.indexCount);
            else if constexpr (std::is_same_v<T, cmd::DrawIndexedInstanced>)   drawIndexedInstanced(c.pt, c.indexCount, c.instances);

            else if constexpr (std::is_same_v<T, cmd::AttachTexture2D>) attachTexture2D(c.fb, c.tex, c.level);
            else if constexpr (std::is_same_v<T, cmd::AttachCubeFace>)  attachCubeFace(c.fb, c.tex, c.face, c.level);

            else if constexpr (std::is_same_v<T, cmd::BeginTimerQuery>) beginTimerQuery(c.q);
            else if constexpr (std::is_same_v<T, cmd::EndTimerQuery>)   endTimerQuery(c.q);

            else if constexpr (std::is_same_v<T, cmd::SetShaderUniform>) {
                bindShader(c.s);
                int iv;
                switch (c.type) {
                    case cmd::UniformType::Int:   std::memcpy(&iv, c.data, sizeof(iv)); c.s->setInt(c.name, iv); break;
                    case cmd::UniformType::Float: c.s->setFloat(c.name, c.data[0]); break;
                    case cmd::UniformType::Vec3:  c.s->setVec3(c.name, c.data); break;
                    case cmd::UniformType::Mat3:  c.s->setMat3(c.name, c.data); break;
                    case cmd::UniformType::Mat4:  c.s->setMat4(c.name, c.data); break;
                }
            }

            else if constexpr (std::is_same_v<T, cmd::UpdateBuffer>)   c.buf->update(c.data.data(), c.data.size());
            else if constexpr (std::is_same_v<T, cmd::ResizeFBDepth>)  c.fb->resizeDepth(c.w, c.h);

        }, command);
    }
}

// ============================================================
//  Pass management
// ============================================================

void GLContext::beginPass(RHIFramebuffer* fb)
{
    auto* glFB = static_cast<GLFramebuffer*>(fb);
    glBindFramebuffer(GL_FRAMEBUFFER, glFB->glFBO());
}

void GLContext::beginDefaultPass()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLContext::endPass()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================
//  State
// ============================================================

void GLContext::setViewport(int x, int y, int w, int h)
{
    glViewport(x, y, w, h);
}

void GLContext::clear(float r, float g, float b, float a, float depth)
{
    glClearColor(r, g, b, a);
    glClearDepth(static_cast<double>(depth));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GLContext::clearColor(float r, float g, float b, float a)
{
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void GLContext::clearDepth(float depth)
{
    glClearDepth(static_cast<double>(depth));
    glClear(GL_DEPTH_BUFFER_BIT);
}

void GLContext::setDepthTest(bool enable, CompareFunc fn)
{
    if (enable) {
        glEnable(GL_DEPTH_TEST);
        switch (fn) {
            case CompareFunc::Less:      glDepthFunc(GL_LESS); break;
            case CompareFunc::LessEqual: glDepthFunc(GL_LEQUAL); break;
            case CompareFunc::Always:    glDepthFunc(GL_ALWAYS); break;
        }
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void GLContext::setDepthWrite(bool enable)
{
    glDepthMask(enable ? GL_TRUE : GL_FALSE);
}

void GLContext::setSeamlessCubemap(bool enable)
{
    if (enable) glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    else        glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

void GLContext::setMultisample(bool enable)
{
    if (enable) glEnable(GL_MULTISAMPLE);
    else        glDisable(GL_MULTISAMPLE);
}

// ============================================================
//  Binding
// ============================================================

void GLContext::bindShader(RHIShader* shader)
{
    auto* glS = static_cast<GLShader*>(shader);
    glUseProgram(glS->glId());
}

void GLContext::bindVertexInput(RHIVertexInput* vi)
{
    auto* glVI = static_cast<GLVertexInput*>(vi);
    glBindVertexArray(glVI->glVAO());
}

void GLContext::bindTexture(int slot, RHITexture* tex)
{
    auto* glTex = static_cast<GLTexture*>(tex);
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(glTex->glTarget(), glTex->glId());
}

// ============================================================
//  Draw
// ============================================================

void GLContext::draw(PrimitiveType pt, int count, int first)
{
    glDrawArrays(toGLPrimitive(pt), first, count);
}

void GLContext::drawIndexed(PrimitiveType pt, int indexCount)
{
    glDrawElements(toGLPrimitive(pt), indexCount, GL_UNSIGNED_INT, nullptr);
}

void GLContext::drawIndexedInstanced(PrimitiveType pt, int indexCount, int instances)
{
    glDrawElementsInstanced(toGLPrimitive(pt), indexCount,
                            GL_UNSIGNED_INT, nullptr, instances);
}

// ============================================================
//  Framebuffer attachment
// ============================================================

void GLContext::attachTexture2D(RHIFramebuffer* /*fb*/, RHITexture* tex, int level)
{
    auto* glTex = static_cast<GLTexture*>(tex);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, glTex->glId(), level);
}

void GLContext::attachCubeFace(RHIFramebuffer* /*fb*/, RHITexture* tex, int face, int level)
{
    auto* glTex = static_cast<GLTexture*>(tex);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                           glTex->glId(), level);
}

// ============================================================
//  Timer queries
// ============================================================

void GLContext::beginTimerQuery(RHITimerQuery* q)
{
    auto* glQ = static_cast<GLTimerQuery*>(q);
    glBeginQuery(GL_TIME_ELAPSED, glQ->currentQuery());
}

void GLContext::endTimerQuery(RHITimerQuery* q)
{
    (void)q;
    glEndQuery(GL_TIME_ELAPSED);
}

float GLContext::getTimerResultMs(RHITimerQuery* q)
{
    auto* glQ = static_cast<GLTimerQuery*>(q);
    if (!glQ->ready()) return 0.0f;

    GLuint64 elapsed = 0;
    glGetQueryObjectui64v(glQ->previousQuery(), GL_QUERY_RESULT, &elapsed);
    float ms = static_cast<float>(elapsed) / 1e6f;

    glQ->swap();
    glQ->markReady();
    return ms;
}
