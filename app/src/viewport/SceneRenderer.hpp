#pragma once

#include "model/SceneDocument.hpp"
#include "simulation/SimulationTypes.hpp"

#include <fluid/TransferFunction.hpp>

#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLExtraFunctions>
#include <QSize>
#include <QVector3D>

#include <map>
#include <memory>

class QOpenGLShaderProgram;
class QOpenGLFramebufferObject;

namespace fluid::app {

/**
 * OpenGL 3.3 renderer of the 3D scene: vehicle, floor, wind-tunnel box, slice plane, volume
 * ray casting, streamline ribbons and particles. Owns all GPU resources; every method except the
 * setters must be called with the viewport's context current.
 */
class SceneRenderer : protected QOpenGLExtraFunctions {
public:
    struct Frame {
        QMatrix4x4 view;
        QMatrix4x4 projection;
        QVector3D cameraPosition;
        QVector3D viewDirection;
        bool orthographic = false;
        QSize pixelSize;
        float time = 0.0f;
        ViewSettings settings;
        std::vector<char> objectVisible;
        int selectedObject = -1;
        DomainInfo domain;
        TracerSettings tracers;
        bool useLod = false; ///< draw the simplified render proxy
        int samples = 4; ///< MSAA samples of the scene framebuffer
        bool fxaa = false; ///< post-process anti-aliasing (when MSAA is off)
    };

    SceneRenderer();
    ~SceneRenderer();

    bool initialize(QString* error);
    void destroy();
    bool isInitialized() const { return m_initialized; }
    QString rendererInfo() const { return m_info; }
    /** True on CPU rasterizers (llvmpipe, softpipe, SwiftShader...). */
    bool isSoftwareRasterizer() const { return m_software; }

    void setMesh(MeshPtr mesh, MeshPtr lod);
    void setModelMatrix(const QMatrix4x4& model) { m_model = model; }
    void setSnapshot(SnapshotPtr snapshot);
    void setTransferFunction(const TransferFunction& tf);

    /** Renders the frame and composites it into framebuffer @p target (e.g. the widget FBO). */
    void render(const Frame& frame, GLuint target);
    /** The last rendered frame (without overlays), in device pixels. */
    QImage grabFrame();

private:
    struct Material {
        QVector3D color;
        float metallic = 0.0f;
        float roughness = 0.5f;
        float emissive = 0.0f;
    };

    std::unique_ptr<QOpenGLShaderProgram> program(const char* vertex, const char* fragment, QString* error);
    struct GpuMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ibo = 0;
        MeshPtr data;
        bool dirty = false;
        std::vector<std::vector<Material>> materials; // per object, per part
    };

    void ensureTargets(const QSize& size, int samples);
    void uploadMesh(GpuMesh& mesh);
    void uploadPending();
    void drawBackground(const Frame& f);
    void drawFloor(const Frame& f, const QMatrix4x4& viewProj);
    void drawMesh(const Frame& f, const QMatrix4x4& viewProj);
    void drawLines(const Frame& f, const QMatrix4x4& viewProj);
    void drawSlice(const Frame& f, const QMatrix4x4& viewProj);
    void drawVolume(const Frame& f, const QMatrix4x4& viewProj);
    void drawStreamlines(const Frame& f, const QMatrix4x4& viewProj);
    void drawParticles(const Frame& f, const QMatrix4x4& viewProj);
    void bindField(QOpenGLShaderProgram& p, const Frame& f);
    static Material materialFor(const std::string& name);

    bool m_initialized = false;
    bool m_software = false;
    QString m_info;

    std::unique_ptr<QOpenGLShaderProgram> m_background, m_composite, m_mesh, m_floor, m_lines, m_streamline, m_particles, m_slice, m_volume;
    std::unique_ptr<QOpenGLFramebufferObject> m_msaa, m_resolve;
    QSize m_targetSize;
    int m_targetSamples = -1;

    GLuint m_emptyVao = 0;
    GLuint m_quadVao = 0, m_quadVbo = 0;
    GLuint m_cubeVao = 0, m_cubeVbo = 0;
    GLuint m_dynVao = 0, m_dynVbo = 0;
    GLuint m_lineVao = 0, m_lineVbo = 0, m_lineIbo = 0;
    GLuint m_particleVao = 0, m_particleVbo = 0;
    GLuint m_fieldTexture = 0, m_transferTexture = 0;

    GpuMesh m_meshes[2]; // full resolution, level of detail
    QMatrix4x4 m_model;

    SnapshotPtr m_snapshot;
    bool m_snapshotDirty = false;
    GridSpec m_fieldGrid;
    bool m_hasField = false;
    std::shared_ptr<const StreamlineGeometry> m_uploadedLines;
    GLsizei m_lineIndexCount = 0;
    GLsizei m_particleCount = 0;

    std::vector<Rgba> m_transferTable;
    bool m_transferDirty = true;
};

} // namespace fluid::app
