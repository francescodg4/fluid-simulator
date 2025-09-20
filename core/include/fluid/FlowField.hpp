#pragma once

#include "fluid/Grid.hpp"

#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace fluid {

/** Macroscopic flow state on a grid with trilinear sampling (in world coordinates). */
struct FlowField {
    GridSpec grid;
    std::vector<Vec3f> velocity;
    std::vector<float> density;
    std::vector<std::uint8_t> solid;

    bool isSolidAt(Vec3f world) const;
    bool insideDomain(Vec3f world) const;
    Vec3f sampleVelocity(Vec3f world) const;
    /** |curl u| via central differences, in lattice units (1 / time step). */
    void vorticityMagnitude(std::vector<float>& out) const;
};

struct StreamlineOptions {
    float stepCells = 0.75f; ///< integration step, in cells
    int maxPoints = 400;
    float minSpeed = 1e-5f;
};

struct StreamlinePoint {
    Vec3f position; ///< world
    float speed; ///< lattice units
    float arcLength; ///< world units from the seed
};

using Streamline = std::vector<StreamlinePoint>;

/** Regular grid of seed points on a rectangle: center +- halfU * u +- halfV * v. */
std::vector<Vec3f> makeRakeSeeds(Vec3f center, Vec3f halfU, Vec3f halfV, int countU, int countV);

/** Traces streamlines through the velocity field with 4th-order Runge-Kutta (fixed arc length steps). */
std::vector<Streamline> traceStreamlines(const FlowField& field, std::span<const Vec3f> seeds, const StreamlineOptions& options = {});

/** Massless tracer particles advected through the field and re-emitted from a seeding rectangle. */
class ParticleSystem {
public:
    struct Particle {
        Vec3f position;
        float age = 0.0f;
    };

    void configure(std::size_t count, Vec3f emitterCenter, Vec3f halfU, Vec3f halfV);
    /** Advances by @p steps lattice time steps (RK2). */
    void advance(const FlowField& field, float steps);
    const std::vector<Particle>& particles() const { return m_particles; }

private:
    void respawn(Particle& p, bool randomAge);

    std::vector<Particle> m_particles;
    Vec3f m_center;
    Vec3f m_halfU;
    Vec3f m_halfV;
    std::mt19937 m_rng { 1234u };
};

} // namespace fluid
