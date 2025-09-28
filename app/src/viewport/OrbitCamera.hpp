#pragma once

#include <fluid/Math.hpp>

#include <QMatrix4x4>
#include <QVector3D>

namespace fluid::app {

/** Turntable camera (Blender style): yaw about +Y, pitch towards the pole, orbiting a target. */
class OrbitCamera {
public:
    struct State {
        QVector3D target { 0, 0, 0 };
        float distance = 10.0f;
        float yaw = -35.0f; ///< degrees; 0 looks from +Z towards -Z
        float pitch = 18.0f; ///< degrees
        float orthoBlend = 0.0f; ///< 0 = perspective, 1 = orthographic (animated)
    };

    State& state() { return m_state; }
    const State& state() const { return m_state; }
    void setState(const State& s) { m_state = s; }

    float fieldOfView() const { return m_fovY; }
    bool isOrthographic() const { return m_state.orthoBlend > 0.5f; }

    QVector3D position() const;
    QVector3D forward() const;
    QVector3D right() const;
    QVector3D up() const;
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix(float aspect) const;

    void orbit(float dxPixels, float dyPixels);
    void pan(float dxPixels, float dyPixels, float viewportHeight);
    void dolly(float steps);
    /** State that frames @p box from the current direction. */
    State framed(const Aabb& box) const;

    static State interpolate(const State& a, const State& b, float t);

private:
    State m_state;
    float m_fovY = 38.0f;
};

} // namespace fluid::app
