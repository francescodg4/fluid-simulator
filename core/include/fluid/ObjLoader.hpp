#pragma once

#include "fluid/Mesh.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace fluid {

struct ObjLoadOptions {
    /** Called periodically with progress in [0, 1]. Returning false cancels the load. */
    std::function<bool(float)> progress;
};

struct ObjLoadError {
    enum class Code {
        FileNotFound,
        ReadFailed,
        Cancelled,
        Empty,
    };
    Code code;
    std::string message;
};

/**
 * Parses Wavefront OBJ text. Polygons are fan-triangulated, (position, normal) pairs are
 * de-duplicated into shared vertices and missing normals are generated (area weighted).
 */
std::expected<TriangleMesh, ObjLoadError> parseObj(std::string_view text, const ObjLoadOptions& options = {});

/** Reads and parses an OBJ file. */
std::expected<TriangleMesh, ObjLoadError> loadObj(const std::filesystem::path& path, const ObjLoadOptions& options = {});

} // namespace fluid
