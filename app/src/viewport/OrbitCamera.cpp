#include "viewport/OrbitCamera.hpp"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace fluid::app {

QVector3D OrbitCamera::forward() const
{
    const float yaw = qDegreesToRadians(m_state.yaw);
    const float pitch = qDegreesToRadians(m_state.pitch);
    const QVector3D offset(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    return -offset.normalized();
}

QVector3D OrbitCamera::position() const
{
    return m_state.target - forward() * m_state.distance;
}

QVector3D OrbitCamera::right() const
{
    const float yaw = qDegreesToRadians(m_state.yaw);
    return QVector3D(std::cos(yaw), 0.0f, -std::sin(yaw));
}

QVector3D OrbitCamera::up() const
{
    return QVector3D::crossProduct(right(), forward()).normalized();
}

QMatrix4x4 OrbitCamera::viewMatrix() const
{
    QMatrix4x4 view;
    view.lookAt(position(), m_state.target, up());
    return view;
}

QMatrix4x4 OrbitCamera::projectionMatrix(float aspect) const
{
    const float nearPlane = std::max(0.01f, m_state.distance * 0.01f);
    const float farPlane = m_state.distance * 60.0f + 200.0f;
    QMatrix4x4 perspective;
    perspective.perspective(m_fovY, aspect, nearPlane, farPlane);
    if (m_state.orthoBlend <= 0.0f) {
        return perspective;
    }
    // Orthographic frustum sized to match the perspective view at the target distance.
    const float halfH = m_state.distance * std::tan(qDegreesToRadians(m_fovY * 0.5f));
    QMatrix4x4 ortho;
    ortho.ortho(-halfH * aspect, halfH * aspect, -halfH, halfH, -farPlane, farPlane);
    if (m_state.orthoBlend >= 1.0f) {
        return ortho;
    }
    // Blend the matrices for a smooth (if not strictly projective) transition.
    QMatrix4x4 blended;
    for (int i = 0; i < 16; ++i) {
        blended.data()[i] = perspective.constData()[i] * (1.0f - m_state.orthoBlend) + ortho.constData()[i] * m_state.orthoBlend;
    }
    return blended;
}

void OrbitCamera::orbit(float dxPixels, float dyPixels)
{
    m_state.yaw -= dxPixels * 0.35f;
    m_state.pitch = std::clamp(m_state.pitch + dyPixels * 0.35f, -89.5f, 89.5f);
}

void OrbitCamera::pan(float dxPixels, float dyPixels, float viewportHeight)
{
    const float worldPerPixel = 2.0f * m_state.distance * std::tan(qDegreesToRadians(m_fovY * 0.5f)) / std::max(1.0f, viewportHeight);
    m_state.target += (-right() * dxPixels + up() * dyPixels) * worldPerPixel;
}

void OrbitCamera::dolly(float steps)
{
    m_state.distance = std::clamp(m_state.distance * std::pow(0.88f, steps), 0.2f, 500.0f);
}

OrbitCamera::State OrbitCamera::framed(const Aabb& box) const
{
    State s = m_state;
    if (!box.valid()) {
        return s;
    }
    const Vec3f c = box.center();
    s.target = QVector3D(c.x, c.y, c.z);
    const float radius = 0.5f * length(box.size());
    s.distance = radius / std::sin(qDegreesToRadians(m_fovY * 0.5f)) * 0.92f;
    return s;
}

OrbitCamera::State OrbitCamera::interpolate(const State& a, const State& b, float t)
{
    const float e = t * t * (3.0f - 2.0f * t); // smoothstep easing
    State s;
    s.target = a.target + (b.target - a.target) * e;
    s.distance = a.distance * std::pow(b.distance / a.distance, e);
    float dyaw = std::fmod(b.yaw - a.yaw + 540.0f, 360.0f) - 180.0f; // shortest arc
    s.yaw = a.yaw + dyaw * e;
    s.pitch = a.pitch + (b.pitch - a.pitch) * e;
    s.orthoBlend = a.orthoBlend + (b.orthoBlend - a.orthoBlend) * e;
    return s;
}

} // namespace fluid::app
