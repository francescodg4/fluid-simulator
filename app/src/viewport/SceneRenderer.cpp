#include "viewport/SceneRenderer.hpp"

#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>

#include "logging/Logging.hpp"

#include <algorithm>
#include <cmath>

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

namespace fluid::app {
namespace {

    constexpr GLuint kFieldUnit = 1;
    constexpr GLuint kTransferUnit = 2;

    QVector3D toQt(Vec3f v) { return { v.x, v.y, v.z }; }

    void vertexAttrib(QOpenGLExtraFunctions& gl, GLuint index, int components, int strideFloats, int offsetFloats)
    {
        gl.glEnableVertexAttribArray(index);
        gl.glVertexAttribPointer(index, components, GL_FLOAT, GL_FALSE, strideFloats * static_cast<GLsizei>(sizeof(float)),
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offsetFloats) * sizeof(float)));
    }

} // namespace

SceneRenderer::SceneRenderer() = default;

SceneRenderer::~SceneRenderer() = default;

std::unique_ptr<QOpenGLShaderProgram> SceneRenderer::program(const char* vertex, const char* fragment, QString* error)
{
    auto p = std::make_unique<QOpenGLShaderProgram>();
    const bool ok = p->addShaderFromSourceFile(QOpenGLShader::Vertex, QStringLiteral(":/shaders/%1").arg(QLatin1String(vertex)))
        && p->addShaderFromSourceFile(QOpenGLShader::Fragment, QStringLiteral(":/shaders/%1").arg(QLatin1String(fragment)))
        && p->link();
    if (!ok) {
        if (error) {
            *error += QStringLiteral("%1/%2: %3\n").arg(QLatin1String(vertex), QLatin1String(fragment), p->log());
        }
        return nullptr;
    }
    return p;
}

bool SceneRenderer::initialize(QString* error)
{
    initializeOpenGLFunctions();
    const auto* ctx = QOpenGLContext::currentContext();
    const auto fmt = ctx->format();
    m_info = QStringLiteral("%1 · OpenGL %2.%3 %4")
                 .arg(QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_RENDERER))))
                 .arg(fmt.majorVersion())
                 .arg(fmt.minorVersion())
                 .arg(fmt.profile() == QSurfaceFormat::CoreProfile ? QStringLiteral("core") : QStringLiteral("compat"));
    const QString renderer = QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_RENDERER))).toLower();
    m_software = renderer.contains("llvmpipe") || renderer.contains("softpipe") || renderer.contains("swrast") || renderer.contains("swiftshader")
        || renderer.contains("software");
    logging::get(logging::channel::Render)->info("OpenGL renderer: {}{}", m_info.toStdString(), m_software ? " (software rasterizer: performance mode)" : "");

    QString log;
    m_background = program("fullscreen.vert", "background.frag", &log);
    m_composite = program("fullscreen.vert", "composite.frag", &log);
    m_mesh = program("mesh.vert", "mesh.frag", &log);
    m_floor = program("floor.vert", "floor.frag", &log);
    m_lines = program("lines.vert", "lines.frag", &log);
    m_streamline = program("streamline.vert", "streamline.frag", &log);
    m_particles = program("particles.vert", "particles.frag", &log);
    m_slice = program("slice.vert", "slice.frag", &log);
    m_volume = program("volume.vert", "volume.frag", &log);
    if (!log.isEmpty()) {
        logging::get(logging::channel::Render)->error("Shader compilation failed:\n{}", log.toStdString());
        if (error) {
            *error = log;
        }
        return false;
    }

    glGenVertexArrays(1, &m_emptyVao);

    // Floor quad (xz corners in [-1, 1]).
    const float quad[] = { -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1 };
    glGenVertexArrays(1, &m_quadVao);
    glGenBuffers(1, &m_quadVbo);
    glBindVertexArray(m_quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    vertexAttrib(*this, 0, 2, 2, 0);

    // Unit cube for volume ray casting (36 vertices, outward winding).
    std::vector<float> cube;
    const int faces[6][4][3] = {
        { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } }, { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } },
        { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 } }, { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } },
        { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } }, { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } },
    };
    for (const auto& f : faces) {
        for (const int i : { 0, 1, 2, 0, 2, 3 }) {
            cube.insert(cube.end(), { static_cast<float>(f[i][0]), static_cast<float>(f[i][1]), static_cast<float>(f[i][2]) });
        }
    }
    glGenVertexArrays(1, &m_cubeVao);
    glGenBuffers(1, &m_cubeVbo);
    glBindVertexArray(m_cubeVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cube.size() * sizeof(float)), cube.data(), GL_STATIC_DRAW);
    vertexAttrib(*this, 0, 3, 3, 0);

    // Dynamic positions (domain box lines, rake, slice quad).
    glGenVertexArrays(1, &m_dynVao);
    glGenBuffers(1, &m_dynVbo);
    glBindVertexArray(m_dynVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_dynVbo);
    glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    vertexAttrib(*this, 0, 3, 3, 0);

    // Streamline ribbons.
    glGenVertexArrays(1, &m_lineVao);
    glGenBuffers(1, &m_lineVbo);
    glGenBuffers(1, &m_lineIbo);
    glBindVertexArray(m_lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_lineIbo);
    vertexAttrib(*this, 0, 3, StreamlineGeometry::kFloatsPerVertex, 0);
    vertexAttrib(*this, 1, 3, StreamlineGeometry::kFloatsPerVertex, 3);
    vertexAttrib(*this, 2, 3, StreamlineGeometry::kFloatsPerVertex, 6);
    vertexAttrib(*this, 3, 3, StreamlineGeometry::kFloatsPerVertex, 9);

    // Particles.
    glGenVertexArrays(1, &m_particleVao);
    glGenBuffers(1, &m_particleVbo);
    glBindVertexArray(m_particleVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_particleVbo);
    vertexAttrib(*this, 0, 4, 4, 0);

    // Meshes (full resolution and level of detail).
    for (GpuMesh& mesh : m_meshes) {
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glGenBuffers(1, &mesh.ibo);
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
        vertexAttrib(*this, 0, 3, 6, 0);
        vertexAttrib(*this, 1, 3, 6, 3);
    }
    glBindVertexArray(0);

    // Textures: 3D field (RGBA16F) and transfer function lookup table.
    glGenTextures(1, &m_fieldTexture);
    glBindTexture(GL_TEXTURE_3D, m_fieldTexture);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &m_transferTexture);
    glBindTexture(GL_TEXTURE_2D, m_transferTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_initialized = true;
    return true;
}

void SceneRenderer::destroy()
{
    if (!m_initialized) {
        return;
    }
    const GLuint vaos[] = { m_emptyVao, m_meshes[0].vao, m_meshes[1].vao, m_quadVao, m_cubeVao, m_dynVao, m_lineVao, m_particleVao };
    const GLuint buffers[] = { m_meshes[0].vbo, m_meshes[0].ibo, m_meshes[1].vbo, m_meshes[1].ibo, m_quadVbo, m_cubeVbo, m_dynVbo, m_lineVbo, m_lineIbo, m_particleVbo };
    const GLuint textures[] = { m_fieldTexture, m_transferTexture };
    glDeleteVertexArrays(static_cast<GLsizei>(std::size(vaos)), vaos);
    glDeleteBuffers(static_cast<GLsizei>(std::size(buffers)), buffers);
    glDeleteTextures(static_cast<GLsizei>(std::size(textures)), textures);
    m_background.reset();
    m_composite.reset();
    m_mesh.reset();
    m_floor.reset();
    m_lines.reset();
    m_streamline.reset();
    m_particles.reset();
    m_slice.reset();
    m_volume.reset();
    m_msaa.reset();
    m_resolve.reset();
    m_initialized = false;
}

void SceneRenderer::setMesh(MeshPtr mesh, MeshPtr lod)
{
    m_meshes[0].data = std::move(mesh);
    m_meshes[1].data = lod ? std::move(lod) : m_meshes[0].data;
    for (GpuMesh& gpu : m_meshes) {
        gpu.dirty = true;
        gpu.materials.clear();
        if (gpu.data) {
            for (const MeshObject& o : gpu.data->objects) {
                std::vector<Material> parts;
                for (const SubMesh& part : o.parts) {
                    parts.push_back(materialFor(part.material));
                }
                gpu.materials.push_back(std::move(parts));
            }
        }
    }
}

void SceneRenderer::setSnapshot(SnapshotPtr snapshot)
{
    m_snapshot = std::move(snapshot);
    m_snapshotDirty = true;
}

void SceneRenderer::setTransferFunction(const TransferFunction& tf)
{
    m_transferTable = tf.bake(256);
    m_transferDirty = true;
}

SceneRenderer::Material SceneRenderer::materialFor(const std::string& raw)
{
    const QString name = QString::fromStdString(raw).toLower();
    // Colours are linear RGB.
    if (name.contains("red")) {
        return { { 0.42f, 0.012f, 0.018f }, 0.55f, 0.18f, 0.0f };
    }
    if (name.contains("window") || name.contains("glass")) {
        return { { 0.01f, 0.013f, 0.02f }, 0.9f, 0.04f, 0.0f };
    }
    if (name.contains("mirror")) {
        return { { 0.7f, 0.72f, 0.76f }, 1.0f, 0.08f, 0.0f };
    }
    if (name.contains("light")) {
        return { { 0.9f, 0.9f, 0.85f }, 0.0f, 0.3f, 0.7f };
    }
    if (name.contains("gloss_black")) {
        return { { 0.012f, 0.012f, 0.014f }, 0.4f, 0.12f, 0.0f };
    }
    if (name.contains("black") || name.contains("rubber")) {
        return { { 0.014f, 0.014f, 0.017f }, 0.0f, 0.8f, 0.0f };
    }
    if (name.startsWith("material")) {
        return { { 0.05f, 0.052f, 0.058f }, 0.6f, 0.35f, 0.0f };
    }
    return { { 0.35f, 0.37f, 0.42f }, 0.1f, 0.5f, 0.0f };
}

void SceneRenderer::ensureTargets(const QSize& size, int samples)
{
    if (m_msaa && m_targetSize == size && m_targetSamples == samples) {
        return;
    }
    m_targetSize = size;
    m_targetSamples = samples;
    QOpenGLFramebufferObjectFormat msaa;
    msaa.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    msaa.setSamples(samples);
    msaa.setInternalTextureFormat(GL_RGBA8);
    m_msaa = std::make_unique<QOpenGLFramebufferObject>(size, msaa);
    QOpenGLFramebufferObjectFormat resolve;
    resolve.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    resolve.setInternalTextureFormat(GL_RGBA8);
    m_resolve = std::make_unique<QOpenGLFramebufferObject>(size, resolve);
}

void SceneRenderer::uploadMesh(GpuMesh& mesh)
{
    mesh.dirty = false;
    std::vector<float> interleaved;
    if (mesh.data) {
        interleaved.resize(mesh.data->positions.size() * 6);
        for (std::size_t i = 0; i < mesh.data->positions.size(); ++i) {
            const Vec3f p = mesh.data->positions[i];
            const Vec3f n = mesh.data->normals[i];
            std::copy_n(&p.x, 3, &interleaved[i * 6]);
            std::copy_n(&n.x, 3, &interleaved[i * 6 + 3]);
        }
    }
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(interleaved.size() * sizeof(float)), interleaved.data(), GL_STATIC_DRAW);
    const auto& indices = mesh.data ? mesh.data->indices : std::vector<std::uint32_t> {};
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
}

void SceneRenderer::uploadPending()
{

    if (m_transferDirty && !m_transferTable.empty()) {
        m_transferDirty = false;
        std::vector<std::uint8_t> rgba(m_transferTable.size() * 4);
        for (std::size_t i = 0; i < m_transferTable.size(); ++i) {
            const Rgba& c = m_transferTable[i];
            rgba[i * 4 + 0] = static_cast<std::uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
            rgba[i * 4 + 1] = static_cast<std::uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
            rgba[i * 4 + 2] = static_cast<std::uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
            rgba[i * 4 + 3] = static_cast<std::uint8_t>(std::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        glBindTexture(GL_TEXTURE_2D, m_transferTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(m_transferTable.size()), 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (m_snapshotDirty) {
        m_snapshotDirty = false;
        if (!m_snapshot || m_snapshot->volume.empty()) {
            m_hasField = false;
            m_lineIndexCount = 0;
            m_particleCount = 0;
            m_uploadedLines.reset();
            return;
        }
        const GridSpec& g = m_snapshot->grid;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_3D, m_fieldTexture);
        if (!(g.nx == m_fieldGrid.nx && g.ny == m_fieldGrid.ny && g.nz == m_fieldGrid.nz) || !m_hasField) {
            glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, g.nx, g.ny, g.nz, 0, GL_RGBA, GL_FLOAT, m_snapshot->volume.data());
        } else {
            glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, g.nx, g.ny, g.nz, GL_RGBA, GL_FLOAT, m_snapshot->volume.data());
        }
        glBindTexture(GL_TEXTURE_3D, 0);
        m_fieldGrid = g;
        m_hasField = true;

        if (m_snapshot->streamlines != m_uploadedLines) {
            m_uploadedLines = m_snapshot->streamlines;
            glBindVertexArray(m_lineVao);
            glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
            if (m_uploadedLines) {
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_uploadedLines->vertices.size() * sizeof(float)), m_uploadedLines->vertices.data(), GL_DYNAMIC_DRAW);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_uploadedLines->indices.size() * sizeof(std::uint32_t)), m_uploadedLines->indices.data(), GL_DYNAMIC_DRAW);
                m_lineIndexCount = static_cast<GLsizei>(m_uploadedLines->indices.size());
            } else {
                m_lineIndexCount = 0;
            }
            glBindVertexArray(0);
        }

        glBindBuffer(GL_ARRAY_BUFFER, m_particleVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_snapshot->particles.size() * sizeof(float)), m_snapshot->particles.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_particleCount = static_cast<GLsizei>(m_snapshot->particles.size() / 4);
    }
}

void SceneRenderer::render(const Frame& f, GLuint target)
{
    if (!m_initialized || f.pixelSize.isEmpty()) {
        return;
    }
    ensureTargets(f.pixelSize, f.samples);
    uploadPending();
    GpuMesh& mesh = m_meshes[f.useLod ? 1 : 0];
    if (mesh.dirty) {
        uploadMesh(mesh); // lazily: the LOD is only uploaded when it is actually used
    }

    const QMatrix4x4 viewProj = f.projection * f.view;
    m_msaa->bind();
    glViewport(0, 0, f.pixelSize.width(), f.pixelSize.height());
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    drawBackground(f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    if (f.settings.showFloor) {
        drawFloor(f, viewProj);
    }
    drawMesh(f, viewProj);
    if (m_hasField && f.settings.showSlice) {
        drawSlice(f, viewProj);
    }
    if (m_hasField && f.settings.showVolume) {
        drawVolume(f, viewProj);
    }
    drawLines(f, viewProj);
    if (m_hasField && f.tracers.streamlines) {
        drawStreamlines(f, viewProj);
    }
    if (m_hasField && f.tracers.particles) {
        drawParticles(f, viewProj);
    }

    // Resolve MSAA and composite into the caller's framebuffer.
    QOpenGLFramebufferObject::blitFramebuffer(m_resolve.get(), m_msaa.get(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, f.pixelSize.width(), f.pixelSize.height());
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    m_composite->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_resolve->texture());
    m_composite->setUniformValue("uImage", 0);
    m_composite->setUniformValue("uFxaa", f.fxaa ? 1 : 0);
    m_composite->setUniformValue("uTexel", QVector2D(1.0f / f.pixelSize.width(), 1.0f / f.pixelSize.height()));
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Leave a clean state for QPainter.
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDepthMask(GL_TRUE);
}

QImage SceneRenderer::grabFrame()
{
    return m_resolve ? m_resolve->toImage() : QImage();
}

void SceneRenderer::drawBackground(const Frame& f)
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    m_background->bind();
    m_background->setUniformValue("uViewport", QVector2D(f.pixelSize.width(), f.pixelSize.height()));
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
}

void SceneRenderer::drawFloor(const Frame& f, const QMatrix4x4& viewProj)
{
    const Aabb& car = f.domain.vehicleBounds;
    const Aabb tunnel = f.domain.grid.worldBounds();
    const Vec3f center = car.valid() ? car.center() : Vec3f {};
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    m_floor->bind();
    m_floor->setUniformValue("uViewProj", viewProj);
    m_floor->setUniformValue("uCenter", QVector3D(center.x, 0.0f, center.z));
    m_floor->setUniformValue("uExtent", 80.0f);
    m_floor->setUniformValue("uCameraPos", f.cameraPosition);
    m_floor->setUniformValue("uFadeDistance", std::max(18.0f, f.cameraPosition.distanceToPoint(QVector3D(center.x, 0, center.z)) * 1.6f));
    m_floor->setUniformValue("uFootprint", car.valid() ? QVector4D(car.min.x, car.min.z, car.max.x, car.max.z) : QVector4D(0, 0, 0, 0));
    m_floor->setUniformValue("uTunnel", f.domain.valid() ? QVector4D(tunnel.min.x, tunnel.min.z, tunnel.max.x, tunnel.max.z) : QVector4D(0, 0, 0, 0));
    glBindVertexArray(m_quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
}

void SceneRenderer::bindField(QOpenGLShaderProgram& p, const Frame& f)
{
    glActiveTexture(GL_TEXTURE0 + kFieldUnit);
    glBindTexture(GL_TEXTURE_3D, m_fieldTexture);
    glActiveTexture(GL_TEXTURE0 + kTransferUnit);
    glBindTexture(GL_TEXTURE_2D, m_transferTexture);
    glActiveTexture(GL_TEXTURE0);
    p.setUniformValue("uField", static_cast<GLint>(kFieldUnit));
    p.setUniformValue("uTransfer", static_cast<GLint>(kTransferUnit));
    p.setUniformValue("uChannel", static_cast<GLint>(f.settings.field));
    const ScalarRange r = f.settings.range();
    p.setUniformValue("uRange", QVector2D(r.min, r.max == r.min ? r.min + 1e-3f : r.max));
    p.setUniformValue("uGridOrigin", toQt(m_fieldGrid.origin));
    p.setUniformValue("uGridExtent", toQt(m_fieldGrid.extent()));
}

void SceneRenderer::drawMesh(const Frame& f, const QMatrix4x4& viewProj)
{
    const GpuMesh& gpu = m_meshes[f.useLod ? 1 : 0];
    if (!gpu.data) {
        return;
    }
    m_mesh->bind();
    m_mesh->setUniformValue("uModel", m_model);
    m_mesh->setUniformValue("uNormalMatrix", m_model.normalMatrix());
    m_mesh->setUniformValue("uViewProj", viewProj);
    m_mesh->setUniformValue("uCameraPos", f.cameraPosition);
    m_mesh->setUniformValue("uViewDir", f.viewDirection);
    m_mesh->setUniformValue("uOrtho", f.orthographic ? 1.0f : 0.0f);
    m_mesh->setUniformValue("uShading", static_cast<GLint>(f.settings.shading));
    m_mesh->setUniformValue("uHasField", m_hasField ? 1 : 0);
    bindField(*m_mesh, f);

    glBindVertexArray(gpu.vao);
    for (std::size_t oi = 0; oi < gpu.data->objects.size(); ++oi) {
        if (oi >= f.objectVisible.size() || !f.objectVisible[oi]) {
            continue;
        }
        const MeshObject& object = gpu.data->objects[oi];
        m_mesh->setUniformValue("uHighlight", static_cast<int>(oi) == f.selectedObject ? 1.0f : 0.0f);
        for (std::size_t pi = 0; pi < object.parts.size(); ++pi) {
            const SubMesh& part = object.parts[pi];
            const Material& m = gpu.materials[oi][pi];
            m_mesh->setUniformValue("uBaseColor", m.color);
            m_mesh->setUniformValue("uMetallic", m.metallic);
            m_mesh->setUniformValue("uRoughness", m.roughness);
            m_mesh->setUniformValue("uEmissive", m.emissive);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(part.indexCount), GL_UNSIGNED_INT,
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(part.firstIndex) * sizeof(std::uint32_t)));
        }
    }
    glBindVertexArray(0);
}

void SceneRenderer::drawLines(const Frame& f, const QMatrix4x4& viewProj)
{
    if (!f.domain.valid()) {
        return;
    }
    std::vector<float> lines;
    auto segment = [&](Vec3f a, Vec3f b) { lines.insert(lines.end(), { a.x, a.y, a.z, b.x, b.y, b.z }); };
    auto boxEdges = [&](const Aabb& box) {
        const Vec3f lo = box.min, hi = box.max;
        const Vec3f c[8] = { { lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, hi.y, lo.z }, { lo.x, hi.y, lo.z },
            { lo.x, lo.y, hi.z }, { hi.x, lo.y, hi.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z } };
        const int e[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        for (const auto& edge : e) {
            segment(c[edge[0]], c[edge[1]]);
        }
    };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_lines->bind();
    m_lines->setUniformValue("uViewProj", viewProj);
    glBindVertexArray(m_dynVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_dynVbo);

    auto flush = [&](const QVector4D& color) {
        if (lines.empty()) {
            return;
        }
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(float)), lines.data(), GL_STREAM_DRAW);
        m_lines->setUniformValue("uColor", color);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size() / 3));
        lines.clear();
    };

    if (f.settings.showDomain) {
        boxEdges(f.domain.grid.worldBounds());
        flush(QVector4D(0.35f, 0.6f, 1.0f, 0.55f));
    }
    if (f.settings.showRake && (f.tracers.streamlines || f.tracers.particles)) {
        const Aabb& car = f.domain.vehicleBounds;
        const Vec3f size = car.size();
        const float x = car.min.x + static_cast<float>(f.tracers.rakePosition) * size.x;
        const float top = static_cast<float>(f.tracers.rakeHeight) * size.y;
        const float bottom = 0.04f * size.y;
        const float hz = 0.5f * static_cast<float>(f.tracers.rakeWidth) * size.z;
        const float cz = car.center().z;
        const Vec3f a { x, bottom, cz - hz }, b { x, bottom, cz + hz }, c { x, top, cz + hz }, d { x, top, cz - hz };
        segment(a, b);
        segment(b, c);
        segment(c, d);
        segment(d, a);
        flush(QVector4D(1.0f, 0.72f, 0.2f, 0.9f));
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void SceneRenderer::drawSlice(const Frame& f, const QMatrix4x4& viewProj)
{
    const Aabb box = m_fieldGrid.worldBounds();
    const float t = static_cast<float>(std::clamp(f.settings.slicePosition, 0.0, 1.0));
    Vec3f c[4];
    switch (f.settings.sliceAxis) {
    case SliceAxis::X: {
        const float x = box.min.x + t * (box.max.x - box.min.x);
        c[0] = { x, box.min.y, box.min.z };
        c[1] = { x, box.min.y, box.max.z };
        c[2] = { x, box.max.y, box.max.z };
        c[3] = { x, box.max.y, box.min.z };
        break;
    }
    case SliceAxis::Y: {
        const float y = box.min.y + t * (box.max.y - box.min.y);
        c[0] = { box.min.x, y, box.min.z };
        c[1] = { box.max.x, y, box.min.z };
        c[2] = { box.max.x, y, box.max.z };
        c[3] = { box.min.x, y, box.max.z };
        break;
    }
    case SliceAxis::Z: {
        const float z = box.min.z + t * (box.max.z - box.min.z);
        c[0] = { box.min.x, box.min.y, z };
        c[1] = { box.max.x, box.min.y, z };
        c[2] = { box.max.x, box.max.y, z };
        c[3] = { box.min.x, box.max.y, z };
        break;
    }
    }
    const float quad[] = { c[0].x, c[0].y, c[0].z, c[1].x, c[1].y, c[1].z, c[2].x, c[2].y, c[2].z,
        c[0].x, c[0].y, c[0].z, c[2].x, c[2].y, c[2].z, c[3].x, c[3].y, c[3].z };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_slice->bind();
    m_slice->setUniformValue("uViewProj", viewProj);
    m_slice->setUniformValue("uOpacity", static_cast<float>(f.settings.sliceOpacity));
    bindField(*m_slice, f);
    glBindVertexArray(m_dynVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_dynVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void SceneRenderer::drawVolume(const Frame& f, const QMatrix4x4& viewProj)
{
    const Aabb box = m_fieldGrid.worldBounds();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT); // back faces: works with the camera inside the box too
    glDisable(GL_DEPTH_TEST);
    m_volume->bind();
    m_volume->setUniformValue("uViewProj", viewProj);
    m_volume->setUniformValue("uBoxMin", toQt(box.min));
    m_volume->setUniformValue("uBoxMax", toQt(box.max));
    m_volume->setUniformValue("uCameraPos", f.cameraPosition);
    m_volume->setUniformValue("uViewDir", f.viewDirection);
    m_volume->setUniformValue("uOrtho", f.orthographic ? 1.0f : 0.0f);
    m_volume->setUniformValue("uDensity", static_cast<float>(f.settings.volumeDensity) * 4.0f);
    m_volume->setUniformValue("uStep", m_fieldGrid.dx * 0.6f);
    bindField(*m_volume, f);
    glBindVertexArray(m_cubeVao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void SceneRenderer::drawStreamlines(const Frame& f, const QMatrix4x4& viewProj)
{
    if (m_lineIndexCount == 0) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_streamline->bind();
    m_streamline->setUniformValue("uViewProj", viewProj);
    m_streamline->setUniformValue("uViewport", QVector2D(f.pixelSize.width(), f.pixelSize.height()));
    m_streamline->setUniformValue("uWidth", static_cast<float>(f.settings.streamlineWidth * f.pixelSize.height() / 900.0 + 0.6));
    m_streamline->setUniformValue("uTime", f.time);
    m_streamline->setUniformValue("uAnimate", f.settings.animateStreamlines ? 1.0f : 0.0f);
    m_streamline->setUniformValue("uOpacity", static_cast<float>(f.settings.streamlineOpacity));
    // Streamlines are always coloured by speed (their natural scalar).
    Frame speed = f;
    speed.settings.field = ScalarField::Speed;
    bindField(*m_streamline, speed);
    glBindVertexArray(m_lineVao);
    glDrawElements(GL_TRIANGLES, m_lineIndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void SceneRenderer::drawParticles(const Frame& f, const QMatrix4x4& viewProj)
{
    if (m_particleCount == 0) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glDepthMask(GL_FALSE);
    m_particles->bind();
    m_particles->setUniformValue("uViewProj", viewProj);
    m_particles->setUniformValue("uSize", static_cast<float>(f.settings.particleSize * f.pixelSize.height() / 900.0));
    Frame speed = f;
    speed.settings.field = ScalarField::Speed;
    bindField(*m_particles, speed);
    glBindVertexArray(m_particleVao);
    glDrawArrays(GL_POINTS, 0, m_particleCount);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_BLEND);
}

} // namespace fluid::app
