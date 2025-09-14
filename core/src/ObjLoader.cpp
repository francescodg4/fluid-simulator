#include "fluid/ObjLoader.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>

namespace fluid {
namespace {

    constexpr std::uint32_t kNoNormal = std::numeric_limits<std::uint32_t>::max();

    /**
     * Open-addressing hash map (position index, normal index) -> output vertex.
     * Much faster than std::unordered_map for the millions of lookups a large OBJ needs.
     */
    class VertexCache {
    public:
        explicit VertexCache(std::size_t expected)
        {
            std::size_t capacity = 1024;
            while (capacity < expected * 2) {
                capacity <<= 1;
            }
            m_keys.assign(capacity, kEmpty);
            m_values.resize(capacity);
        }

        /** Returns {value, inserted}. */
        std::pair<std::uint32_t, bool> findOrInsert(std::uint64_t key, std::uint32_t value)
        {
            if ((m_size + 1) * 2 > m_keys.size()) {
                grow();
            }
            std::size_t mask = m_keys.size() - 1;
            for (std::size_t slot = hash(key) & mask;; slot = (slot + 1) & mask) {
                if (m_keys[slot] == key) {
                    return { m_values[slot], false };
                }
                if (m_keys[slot] == kEmpty) {
                    m_keys[slot] = key;
                    m_values[slot] = value;
                    ++m_size;
                    return { value, true };
                }
            }
        }

    private:
        static constexpr std::uint64_t kEmpty = std::numeric_limits<std::uint64_t>::max();

        static std::uint64_t hash(std::uint64_t x)
        {
            // splitmix64 finaliser
            x ^= x >> 30;
            x *= 0xbf58476d1ce4e5b9ULL;
            x ^= x >> 27;
            x *= 0x94d049bb133111ebULL;
            x ^= x >> 31;
            return x;
        }

        void grow()
        {
            std::vector<std::uint64_t> keys(m_keys.size() * 2, kEmpty);
            std::vector<std::uint32_t> values(keys.size());
            const std::size_t mask = keys.size() - 1;
            for (std::size_t i = 0; i < m_keys.size(); ++i) {
                if (m_keys[i] == kEmpty) {
                    continue;
                }
                std::size_t slot = hash(m_keys[i]) & mask;
                while (keys[slot] != kEmpty) {
                    slot = (slot + 1) & mask;
                }
                keys[slot] = m_keys[i];
                values[slot] = m_values[i];
            }
            m_keys.swap(keys);
            m_values.swap(values);
        }

        std::vector<std::uint64_t> m_keys;
        std::vector<std::uint32_t> m_values;
        std::size_t m_size = 0;
    };

    struct Cursor {
        const char* p;
        const char* end;

        bool atEnd() const { return p >= end; }
        void skipSpaces()
        {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) {
                ++p;
            }
        }
        void skipLine()
        {
            while (p < end && *p != '\n') {
                ++p;
            }
            if (p < end) {
                ++p;
            }
        }
        bool atLineEnd() const { return p >= end || *p == '\n' || *p == '\r' || *p == '#'; }

        float parseFloat()
        {
            skipSpaces();
            if (p < end && *p == '+') {
                ++p;
            }
            float value = 0.0f;
            auto [ptr, ec] = std::from_chars(p, end, value);
            if (ec != std::errc()) {
                // Skip the malformed token.
                while (p < end && *p != ' ' && *p != '\n') {
                    ++p;
                }
                return 0.0f;
            }
            p = ptr;
            return value;
        }

        bool parseInt(long& value)
        {
            if (p < end && *p == '+') {
                ++p;
            }
            auto [ptr, ec] = std::from_chars(p, end, value);
            if (ec != std::errc()) {
                return false;
            }
            p = ptr;
            return true;
        }

        std::string_view restOfLine()
        {
            skipSpaces();
            const char* begin = p;
            while (p < end && *p != '\n' && *p != '\r') {
                ++p;
            }
            const char* last = p;
            while (last > begin && (last[-1] == ' ' || last[-1] == '\t')) {
                --last;
            }
            return { begin, static_cast<std::size_t>(last - begin) };
        }
    };

    /** Resolves 1-based (or negative, relative) OBJ indices; returns false when out of range. */
    bool resolveIndex(long raw, std::size_t count, std::uint32_t& out)
    {
        long index = raw > 0 ? raw - 1 : static_cast<long>(count) + raw;
        if (raw == 0 || index < 0 || static_cast<std::size_t>(index) >= count) {
            return false;
        }
        out = static_cast<std::uint32_t>(index);
        return true;
    }

    class ObjParser {
    public:
        ObjParser(std::string_view text, const ObjLoadOptions& options)
            : m_text(text)
            , m_options(options)
            , m_cache(text.size() / 96 + 16)
        {
        }

        std::expected<TriangleMesh, ObjLoadError> run()
        {
            Cursor c { m_text.data(), m_text.data() + m_text.size() };
            const char* nextProgress = c.p;
            constexpr std::size_t kProgressStride = 4 << 20;

            m_srcPositions.reserve(m_text.size() / 64);
            m_srcNormals.reserve(m_text.size() / 128);

            while (!c.atEnd()) {
                if (m_options.progress && c.p >= nextProgress) {
                    const float fraction = static_cast<float>(c.p - m_text.data()) / static_cast<float>(m_text.size());
                    if (!m_options.progress(fraction)) {
                        return std::unexpected(ObjLoadError { ObjLoadError::Code::Cancelled, "Loading cancelled" });
                    }
                    nextProgress = c.p + kProgressStride;
                }

                c.skipSpaces();
                if (c.atEnd()) {
                    break;
                }
                const char ch = *c.p;
                const char next = c.p + 1 < c.end ? c.p[1] : '\0';
                if (ch == 'v' && (next == ' ' || next == '\t')) {
                    c.p += 2;
                    const float x = c.parseFloat();
                    const float y = c.parseFloat();
                    const float z = c.parseFloat();
                    m_srcPositions.push_back({ x, y, z });
                } else if (ch == 'v' && next == 'n') {
                    c.p += 2;
                    const float x = c.parseFloat();
                    const float y = c.parseFloat();
                    const float z = c.parseFloat();
                    m_srcNormals.push_back(normalized({ x, y, z }));
                } else if (ch == 'f' && (next == ' ' || next == '\t')) {
                    c.p += 2;
                    parseFace(c);
                } else if (ch == 'o' && (next == ' ' || next == '\t')) {
                    c.p += 1;
                    beginObject(std::string(c.restOfLine()));
                } else if (m_text.compare(static_cast<std::size_t>(c.p - m_text.data()), 7, "usemtl ") == 0) {
                    c.p += 6;
                    beginMaterial(std::string(c.restOfLine()));
                }
                c.skipLine();
            }

            finishObject();
            if (m_mesh.indices.empty()) {
                return std::unexpected(ObjLoadError { ObjLoadError::Code::Empty, "The file contains no faces" });
            }
            generateMissingNormals();
            m_mesh.updateObjectBounds();
            if (m_options.progress) {
                m_options.progress(1.0f);
            }
            return std::move(m_mesh);
        }

    private:
        void beginObject(std::string name)
        {
            finishObject();
            MeshObject object;
            object.name = name.empty() ? "Object" : std::move(name);
            object.firstIndex = static_cast<std::uint32_t>(m_mesh.indices.size());
            m_mesh.objects.push_back(std::move(object));
            m_objectOpen = true;
            beginMaterial(m_material);
        }

        void beginMaterial(std::string material)
        {
            m_material = std::move(material);
            if (!m_objectOpen) {
                return;
            }
            MeshObject& object = m_mesh.objects.back();
            closePart(object);
            SubMesh part;
            part.material = m_material;
            part.firstIndex = static_cast<std::uint32_t>(m_mesh.indices.size());
            object.parts.push_back(std::move(part));
        }

        void closePart(MeshObject& object)
        {
            if (object.parts.empty()) {
                return;
            }
            SubMesh& part = object.parts.back();
            part.indexCount = static_cast<std::uint32_t>(m_mesh.indices.size()) - part.firstIndex;
            if (part.indexCount == 0) {
                object.parts.pop_back();
            }
        }

        void finishObject()
        {
            if (!m_objectOpen) {
                return;
            }
            MeshObject& object = m_mesh.objects.back();
            closePart(object);
            object.indexCount = static_cast<std::uint32_t>(m_mesh.indices.size()) - object.firstIndex;
            if (object.indexCount == 0) {
                m_mesh.objects.pop_back();
            }
            m_objectOpen = false;
        }

        void parseFace(Cursor& c)
        {
            if (!m_objectOpen) {
                beginObject("Default");
            }
            m_face.clear();
            for (;;) {
                c.skipSpaces();
                if (c.atLineEnd()) {
                    break;
                }
                long rawPosition = 0;
                if (!c.parseInt(rawPosition)) {
                    break;
                }
                long rawNormal = 0;
                if (c.p < c.end && *c.p == '/') {
                    ++c.p;
                    long ignoredTexcoord = 0;
                    if (c.p < c.end && *c.p != '/') {
                        c.parseInt(ignoredTexcoord);
                    }
                    if (c.p < c.end && *c.p == '/') {
                        ++c.p;
                        c.parseInt(rawNormal);
                    }
                }
                std::uint32_t position = 0;
                if (!resolveIndex(rawPosition, m_srcPositions.size(), position)) {
                    continue;
                }
                std::uint32_t normal = kNoNormal;
                if (rawNormal != 0) {
                    resolveIndex(rawNormal, m_srcNormals.size(), normal);
                }
                m_face.push_back(vertexFor(position, normal));
            }
            for (std::size_t i = 2; i < m_face.size(); ++i) {
                m_mesh.indices.push_back(m_face[0]);
                m_mesh.indices.push_back(m_face[i - 1]);
                m_mesh.indices.push_back(m_face[i]);
            }
        }

        std::uint32_t vertexFor(std::uint32_t position, std::uint32_t normal)
        {
            const std::uint64_t key = (static_cast<std::uint64_t>(position) << 32) | normal;
            const auto candidate = static_cast<std::uint32_t>(m_mesh.positions.size());
            auto [index, inserted] = m_cache.findOrInsert(key, candidate);
            if (inserted) {
                m_mesh.positions.push_back(m_srcPositions[position]);
                m_mesh.normals.push_back(normal == kNoNormal ? Vec3f {} : m_srcNormals[normal]);
                if (normal == kNoNormal) {
                    m_missingNormals = true;
                }
            }
            return index;
        }

        void generateMissingNormals()
        {
            if (!m_missingNormals) {
                return;
            }
            std::vector<Vec3f> accumulated(m_mesh.positions.size());
            for (std::size_t i = 0; i + 2 < m_mesh.indices.size(); i += 3) {
                const std::uint32_t a = m_mesh.indices[i];
                const std::uint32_t b = m_mesh.indices[i + 1];
                const std::uint32_t c = m_mesh.indices[i + 2];
                const Vec3f n = cross(m_mesh.positions[b] - m_mesh.positions[a], m_mesh.positions[c] - m_mesh.positions[a]);
                accumulated[a] += n;
                accumulated[b] += n;
                accumulated[c] += n;
            }
            for (std::size_t i = 0; i < m_mesh.normals.size(); ++i) {
                if (dot(m_mesh.normals[i], m_mesh.normals[i]) == 0.0f) {
                    m_mesh.normals[i] = normalized(accumulated[i]);
                }
            }
        }

        std::string_view m_text;
        const ObjLoadOptions& m_options;
        TriangleMesh m_mesh;
        VertexCache m_cache;
        std::vector<Vec3f> m_srcPositions;
        std::vector<Vec3f> m_srcNormals;
        std::vector<std::uint32_t> m_face;
        std::string m_material;
        bool m_objectOpen = false;
        bool m_missingNormals = false;
    };

} // namespace

std::expected<TriangleMesh, ObjLoadError> parseObj(std::string_view text, const ObjLoadOptions& options)
{
    return ObjParser(text, options).run();
}

std::expected<TriangleMesh, ObjLoadError> loadObj(const std::filesystem::path& path, const ObjLoadOptions& options)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::unexpected(ObjLoadError { ObjLoadError::Code::FileNotFound, "File not found: " + path.string() });
    }
    std::ifstream in(path, std::ios::binary);
    const auto size = std::filesystem::file_size(path, ec);
    if (!in || ec) {
        return std::unexpected(ObjLoadError { ObjLoadError::Code::ReadFailed, "Cannot read: " + path.string() });
    }
    std::string text(size, '\0');
    if (!in.read(text.data(), static_cast<std::streamsize>(size))) {
        return std::unexpected(ObjLoadError { ObjLoadError::Code::ReadFailed, "Cannot read: " + path.string() });
    }
    return parseObj(text, options);
}

} // namespace fluid
