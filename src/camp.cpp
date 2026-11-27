#include "camp.hpp"
#include "driving.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double tileSize = campTerrainTileSize;
constexpr double roadHalfWidth = 4.65;
constexpr double campInterval = 256.0;
constexpr double terrainStep = 4.0;
constexpr double trackStep = 0.28;
constexpr double grassRecovery = 9.0;

double smooth(double lo, double hi, double value)
{
    const double t = std::clamp((value - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double mix(double a, double b, double t)
{
    return a + (b - a) * t;
}

std::int64_t cell(double value, double size)
{
    return static_cast<std::int64_t>(std::floor(value / size));
}

} // namespace

float CampWorld::random(std::int64_t x, std::int64_t z, std::uint32_t salt) const
{
    std::uint64_t hash = static_cast<std::uint64_t>(x) * 0x9e3779b97f4a7c15ull;
    hash ^= static_cast<std::uint64_t>(z) * 0xbf58476d1ce4e5b9ull + seed_ + salt * 0x94d049bb133111ebull;
    hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ull;
    hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebull;
    return static_cast<float>((hash ^ (hash >> 31)) >> 40) / 16777216.0f;
}

double CampWorld::noise(double x, double z, std::uint32_t salt) const
{
    const auto ix = cell(x, 1.0);
    const auto iz = cell(z, 1.0);
    const double u = x - static_cast<double>(ix);
    const double v = z - static_cast<double>(iz);
    const auto fade = [](double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); };
    const auto gradient = [&](int dx, int dz) {
        constexpr std::array<glm::dvec2, 8> directions{{
            {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.707, 0.707}, {-0.707, 0.707},
            {0.707, -0.707}, {-0.707, -0.707},
        }};
        return glm::dot(directions[static_cast<int>(random(ix + dx, iz + dz, salt) * 8.0f)],
                        glm::dvec2(u - dx, v - dz));
    };
    return mix(mix(gradient(0, 0), gradient(1, 0), fade(u)),
               mix(gradient(0, 1), gradient(1, 1), fade(u)), fade(v));
}

double CampWorld::roadCenter(double x) const
{
    return 72.0 * noise(x / 142.0, 0.0, 3) + 16.0 * noise(x / 56.0, 0.0, 5);
}

double CampWorld::roadHeight(double x) const
{
    return 18.0 * noise(x / 233.0, 0.0, 7) + 4.0 * noise(x / 75.0, 0.0, 11);
}

CampWorld::Campsite CampWorld::campsiteAt(double x) const
{
    const auto section = cell(x, campInterval);
    const double start = static_cast<double>(section) * campInterval;
    const double center = start + (section == 0 ? 49.0 : 63.0 + 120.0 * random(section, 0, 13));
    const double side = section == 0 || random(section, 0, 17) > 0.5f ? 1.0 : -1.0;
    return {center, side, side * 23.0};
}

float CampWorld::gravel(double x, double offset, const Campsite& site) const
{
    const double dx = x - site.x;
    const double across = offset * site.side;
    const double pad = std::hypot(dx / 14.0, (offset - site.offset) / 10.0);
    const double driveway = (1.0 - smooth(2.0, 4.6, std::abs(dx + (23.0 - across) * 0.47))) *
                            smooth(3.0, 6.0, across) * (1.0 - smooth(22.0, 27.0, across));
    return static_cast<float>(std::max(1.0 - smooth(0.78, 1.18, pad), driveway));
}

double CampWorld::height(double x, double z, double offset) const
{
    const double road = roadHeight(x);
    if (std::abs(offset) <= roadHalfWidth + 1.2) {
        return road;
    }
    const double hillside = smooth(5.85, 34.0, std::abs(offset));
    const double far = smooth(22.0, 112.0, std::abs(offset));
    const double peaks = 1.0 - std::abs(noise(x / 230.0, z / 202.0, 23));
    const double relief = 28.0 * noise(x / 121.0, z / 101.0, 29) +
                           9.0 * noise(x / 44.0, z / 39.0, 31) +
                           2.7 * noise(x / 19.0, z / 17.0, 37);
    const double ground = road + hillside * (relief + far * (57.0 * peaks * peaks - 16.0));
    const Campsite site = campsiteAt(x);
    const double clearing = gravel(x, offset, site);
    const double padHeight = mix(road, roadHeight(site.x) + 0.27, smooth(6.0, 16.0, offset * site.side));
    return mix(ground, padHeight, clearing);
}

CampSurface CampWorld::surface(const glm::vec3& position) const
{
    const double x = origin_.x + position.x;
    const double z = origin_.y + position.z;
    const double offset = z - roadCenter(x);
    const float pavement = static_cast<float>(1.0 - smooth(roadHalfWidth, roadHalfWidth + 0.8, std::abs(offset)));
    const float packed = gravel(x, offset, campsiteAt(x));
    return {static_cast<float>(meshHeight(x, z)), glm::mix(glm::mix(0.63f, 0.79f, packed), 1.0f, pavement)};
}

double CampWorld::meshHeight(double x, double z, glm::vec2* slope) const
{
    const double start = std::floor(x / terrainStep) * terrainStep;
    const double end = start + terrainStep;
    const double t = (x - start) / terrainStep;
    const double centerA = roadCenter(start);
    const double centerB = roadCenter(end);
    const double offset = z - mix(centerA, centerB, t);
    double low = std::floor(offset / terrainStep) * terrainStep;
    double high = low + terrainStep;
    if (std::abs(offset) < 8.0) {
        constexpr std::array<double, 5> roadColumns{-8.0, -roadHalfWidth, 0.0, roadHalfWidth, 8.0};
        const auto next = std::upper_bound(roadColumns.begin(), roadColumns.end(), offset);
        low = *(next - 1);
        high = *next;
    }
    const double across = (offset - low) / (high - low);
    const double a = height(start, centerA + low, low);
    const double c = height(end, centerB + high, high);
    // Use the same triangle diagonal as the visible terrain mesh.
    double along, side;
    if (across >= t) {
        const double b = height(start, centerA + high, high);
        along = c - b;
        side = b - a;
    } else {
        const double d = height(end, centerB + low, low);
        along = d - a;
        side = c - d;
    }
    if (slope) {
        const double dz = side / (high - low);
        *slope = {static_cast<float>((along - dz * (centerB - centerA)) / terrainStep), static_cast<float>(dz)};
    }
    return a + along * t + side * across;
}

std::vector<CampGrassPlant> CampWorld::buildGrass(CampTile tile, int samples, GrassBand band, bool horizon) const
{
    const glm::dvec2 origin{tile[0] * campGrassTileSize, tile[1] * campGrassTileSize};
    const double spacing = campGrassTileSize / samples;
    std::vector<CampGrassPlant> plants;
    for (int i = 0; i < samples; ++i) {
        const auto gx = tile[0] * samples + i;
        for (int j = 0; j < samples; ++j) {
            const auto gz = tile[1] * samples + j;
            // A plant keeps its band even as the player moves or revisits a tile.
            const float density = random(gx, gz, 751);
            const GrassBand plantBand = horizon && density < 0.005f ? GrassBand::Horizon
                : density < 0.04f ? GrassBand::Far
                : density < 0.20f ? GrassBand::Middle : GrassBand::Near;
            if (plantBand != band) continue;
            const double localX = (i + 0.08 + 0.84 * random(gx, gz, 701)) * spacing;
            const double localZ = (j + 0.08 + 0.84 * random(gx, gz, 709)) * spacing;
            const double x = origin.x + localX;
            const double z = origin.y + localZ;
            const double offset = z - roadCenter(x);
            if (std::abs(offset) < roadHalfWidth + 0.37 || gravel(x, offset, campsiteAt(x)) > 0.12f) continue;
            const double coverage = 0.67 + 0.65 * noise(x / 13.0, z / 13.0, 719);
            if (random(gx, gz, 727) > coverage) continue;
            glm::vec2 slope;
            const float y = static_cast<float>(meshHeight(x, z, &slope));
            if (glm::dot(slope, slope) > 1.3f) continue;
            const float size = random(gx, gz, 733);
            plants.push_back({{static_cast<float>(localX), y - 0.012f, static_cast<float>(localZ)}, slope,
                0.22f + 0.77f * size, 0.15f + 0.14f * size, 6.2831853f * random(gx, gz, 739), random(gx, gz, 743)});
        }
    }
    return plants;
}

std::size_t CampWorld::TrackCellHash::operator()(const CampTile& tile) const noexcept
{
    const auto x = static_cast<std::uint64_t>(tile[0]) * 0x9e3779b97f4a7c15ull;
    const auto z = static_cast<std::uint64_t>(tile[1]) * 0xbf58476d1ce4e5b9ull;
    return static_cast<std::size_t>(x ^ (x >> 32) ^ z ^ (z >> 32));
}

CampGrassTrack CampWorld::grassTrack(glm::dvec2 position) const
{
    if (grassTracks_.empty()) return {};
    const auto match = grassTracks_.find({cell(position.x, trackStep), cell(position.y, trackStep)});
    if (match == grassTracks_.end()) return {};
    auto track = match->second.track;
    track.flattened *= static_cast<float>(1.0 - smooth(2.3, grassRecovery, grassTime_ - match->second.pressedAt));
    return track;
}

void CampWorld::trampleGrass(const CarAsset& asset, const CarState& car, float dt)
{
    grassTime_ += dt;
    if (grassTime_ >= nextGrassCleanup_) {
        nextGrassCleanup_ = grassTime_ + 1.0;
        for (auto it = grassTracks_.begin(); it != grassTracks_.end();) {
            if (grassTime_ - it->second.pressedAt >= grassRecovery) it = grassTracks_.erase(it);
            else ++it;
        }
    }

    const auto frame = campGroundFrame(car);
    const double radius = std::max(0.37f, asset.wheelHalfWidth * 1.75f);
    for (std::size_t i = 0; i < car.wheels.size(); ++i) {
        const auto& hub = asset.hubs[i];
        const auto contact = car.position + frame * glm::vec3(hub.x, car.wheels[i].groundTravel, hub.z);
        const glm::dvec2 point = origin_ + glm::dvec2(contact.x, contact.z);
        const glm::dvec2 previous = hasGrassContact_ ? lastGrassContact_[i] : point;
        const glm::dvec2 segment = point - previous;
        const double lengthSquared = glm::dot(segment, segment);
        lastGrassContact_[i] = point;
        if (!hasGrassContact_) grassDirection_[i] = glm::normalize(glm::vec2(frame[0].x, frame[0].z));
        if (lengthSquared > 0.000025) grassDirection_[i] = glm::vec2(segment / std::sqrt(lengthSquared));
        if (std::abs(point.y - roadCenter(point.x)) < roadHalfWidth - radius &&
            std::abs(previous.y - roadCenter(previous.x)) < roadHalfWidth - radius) continue;
        const auto first = glm::min(previous, point) - radius;
        const auto last = glm::max(previous, point) + radius;
        for (auto x = cell(first.x, trackStep); x <= cell(last.x, trackStep); ++x) {
            for (auto z = cell(first.y, trackStep); z <= cell(last.y, trackStep); ++z) {
                const glm::dvec2 center{(x + 0.5) * trackStep, (z + 0.5) * trackStep};
                const double t = lengthSquared > 0.0
                    ? std::clamp(glm::dot(center - previous, segment) / lengthSquared, 0.0, 1.0) : 0.0;
                const double distance = glm::length(center - (previous + segment * t));
                const float strength = static_cast<float>(1.0 - smooth(radius * 0.65, radius, distance));
                if (strength <= 0.0f) continue;
                const auto [stamp, added] = grassTracks_.try_emplace(CampTile{x, z}, TrackCell{
                    {grassDirection_[i], strength}, grassTime_});
                if (!added) {
                    const auto& older = stamp->second;
                    const double active = older.track.flattened *
                        (1.0 - smooth(2.3, grassRecovery, grassTime_ - older.pressedAt));
                    if (strength >= active) stamp->second = {{grassDirection_[i], strength}, grassTime_};
                }
            }
        }
    }
    hasGrassContact_ = true;
}

glm::vec3 CampWorld::spawn() const
{
    const glm::vec3 position = localOrigin({0.0, roadCenter(0.0)});
    return {position.x, surface(position).height, position.z};
}

float CampWorld::spawnHeading() const
{
    return static_cast<float>(-std::atan2(roadCenter(1.0) - roadCenter(-1.0), 2.0));
}

glm::vec3 CampWorld::rebase(glm::vec3& position)
{
    const glm::vec3 shift{std::trunc(position.x / 1024.0f) * 1024.0f, 0.0f,
                          std::trunc(position.z / 1024.0f) * 1024.0f};
    origin_ += glm::dvec2(shift.x, shift.z);
    position -= shift;
    return shift;
}

CampTile CampWorld::tileAt(const glm::vec3& position) const
{
    const double x = origin_.x + position.x;
    const double offset = origin_.y + position.z - roadCenter(x);
    return {cell(x, tileSize), cell(offset + tileSize * 0.5, tileSize)};
}

glm::vec3 CampWorld::localOrigin(glm::dvec2 origin) const
{
    const glm::dvec2 local = origin - origin_;
    return {static_cast<float>(local.x), 0.0f, static_cast<float>(local.y)};
}

glm::dvec2 CampWorld::chunkOrigin(CampTile tile) const
{
    const double x = static_cast<double>(tile[0]) * tileSize;
    return {x, roadCenter(x) + (static_cast<double>(tile[1]) - 0.5) * tileSize};
}

CampChunk CampWorld::buildChunk(CampTile tile, int stride) const
{
    const double firstX = static_cast<double>(tile[0]) * tileSize;
    const double firstOffset = (static_cast<double>(tile[1]) - 0.5) * tileSize;
    CampChunk chunk;
    chunk.origin = chunkOrigin(tile);
    auto& terrain = chunk.surfaces[0];
    auto& asphalt = chunk.surfaces[1];
    auto& gravelMesh = chunk.surfaces[2];
    auto& paint = chunk.surfaces[3];
    terrain.surface.color = {0.19f, 0.255f, 0.208f};
    asphalt.surface.color = {0.135f, 0.147f, 0.161f};
    gravelMesh.surface.color = {0.29f, 0.26f, 0.205f};
    paint.surface.color = {0.65f, 0.58f, 0.36f};

    const auto makeVertex = [&](double x, double offset, float lift = 0.0f) {
        const double z = roadCenter(x) + offset;
        const double y = height(x, z, offset);
        constexpr double span = 0.65;
        const double dx = height(x + span, z, z - roadCenter(x + span)) -
                          height(x - span, z, z - roadCenter(x - span));
        const double dz = height(x, z + span, offset + span) - height(x, z - span, offset - span);
        return Vertex{{static_cast<float>(x - chunk.origin.x), static_cast<float>(y) + lift,
                       static_cast<float>(z - chunk.origin.y)},
                      glm::normalize(glm::vec3(-dx, 2.0 * span, -dz))};
    };
    const auto quad = [](MeshPart& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
        mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
    };
    chunk.props = buildProps(tile, chunk.origin, stride);
    if (stride > 1 && tile[1] != 0) {
        const double spacing = stride * terrainStep;
        const int cells = 32 / stride;
        auto& vertices = terrain.vertices;
        for (int row = 0; row <= cells; ++row) {
            for (int column = 0; column <= cells; ++column) {
                vertices.push_back(makeVertex(firstX + row * spacing, firstOffset + column * spacing));
            }
        }
        const auto vertex = [&](double x, double offset) {
            const auto index = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(makeVertex(x, offset));
            return index;
        };
        std::vector<std::uint32_t> perimeter;
        perimeter.reserve(4 * stride);
        std::vector<std::size_t> rowStarts;
        rowStarts.reserve(cells + 1);
        for (int row = 0; row < cells; ++row) {
            rowStarts.push_back(terrain.indices.size());
            for (int column = 0; column < cells; ++column) {
                const auto a = static_cast<std::uint32_t>(row * (cells + 1) + column);
                const auto b = a + 1, c = b + cells + 1, d = c - 1;
                const bool top = row == 0, bottom = row == cells - 1;
                const bool left = column == 0, right = column == cells - 1;
                if (!top && !bottom && !left && !right) {
                    quad(terrain, a, b, c, d);
                    continue;
                }
                // Keep the fine vertices along each outer edge so adjacent LODs share the same seam.
                const double x = firstX + row * spacing, z = firstOffset + column * spacing;
                perimeter.clear();
                perimeter.push_back(a);
                for (int i = 1; top && i < stride; ++i) perimeter.push_back(vertex(x, z + i * terrainStep));
                perimeter.push_back(b);
                for (int i = 1; right && i < stride; ++i) perimeter.push_back(vertex(x + i * terrainStep, z + spacing));
                perimeter.push_back(c);
                for (int i = 1; bottom && i < stride; ++i) perimeter.push_back(vertex(x + spacing, z + spacing - i * terrainStep));
                perimeter.push_back(d);
                for (int i = 1; left && i < stride; ++i) perimeter.push_back(vertex(x + spacing - i * terrainStep, z));
                const auto middle = vertex(x + spacing * 0.5, z + spacing * 0.5);
                for (std::size_t i = 0; i < perimeter.size(); ++i) {
                    terrain.indices.insert(terrain.indices.end(), {middle, perimeter[i], perimeter[(i + 1) % perimeter.size()]});
                }
            }
        }
        rowStarts.push_back(terrain.indices.size());
        const auto cross2 = [](glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; };
        // Distant trees follow the visible coarse surface. Collision still uses the full terrain.
        for (auto& prop : chunk.props) {
            const int row = std::clamp(static_cast<int>(prop.center.x / spacing), 0, cells - 1);
            for (auto i = rowStarts[row]; i < rowStarts[row + 1]; i += 3) {
                const auto& a = vertices[terrain.indices[i]].position;
                const auto& b = vertices[terrain.indices[i + 1]].position;
                const auto& c = vertices[terrain.indices[i + 2]].position;
                if (prop.center.z < std::min({a.z, b.z, c.z}) || prop.center.z > std::max({a.z, b.z, c.z})) continue;
                const glm::vec2 ab(b.x - a.x, b.z - a.z), ac(c.x - a.x, c.z - a.z);
                const glm::vec2 delta(prop.center.x - a.x, prop.center.z - a.z);
                const float area = cross2(ab, ac);
                const float alongB = cross2(delta, ac) / area;
                const float alongC = cross2(ab, delta) / area;
                if (alongB >= -0.0001f && alongC >= -0.0001f && alongB + alongC <= 1.0001f) {
                    prop.center.y = a.y + (b.y - a.y) * alongB + (c.y - a.y) * alongC + prop.size.y * 0.5f;
                    break;
                }
            }
        }
        return chunk;
    }

    std::vector<double> offsets;
    for (int i = 0; i <= 32; ++i) {
        const double offset = firstOffset + i * terrainStep;
        if (tile[1] == 0 && std::abs(offset) == 4.0) continue;
        offsets.push_back(offset);
        if (tile[1] == 0 && offset == -8.0) offsets.push_back(-roadHalfWidth);
        if (tile[1] == 0 && offset == 0.0) offsets.push_back(roadHalfWidth);
    }
    // All surface materials share the same sampling grid, including both sides of each chunk seam.
    const auto columns = static_cast<std::uint32_t>(offsets.size());
    auto& vertices = terrain.vertices;
    vertices.reserve(33 * columns);
    for (int row = 0; row <= 32; ++row) {
        const double x = firstX + row * terrainStep;
        for (const double offset : offsets) vertices.push_back(makeVertex(x, offset));
        if (row == 32) continue;
        const Campsite site = campsiteAt(x + terrainStep * 0.5);
        for (std::uint32_t column = 0; column + 1 < columns; ++column) {
            const double offset = (offsets[column] + offsets[column + 1]) * 0.5;
            MeshPart& mesh = std::abs(offset) < roadHalfWidth ? asphalt
                : gravel(x + terrainStep * 0.5, offset, site) > 0.55f ? gravelMesh : terrain;
            const auto start = static_cast<std::uint32_t>(row) * columns + column;
            quad(mesh, start, start + 1, start + columns + 1, start + columns);
        }
        if (tile[1] == 0 && stride == 1 && row % 2 == 0) {
            const auto start = static_cast<std::uint32_t>(paint.vertices.size());
            paint.vertices.insert(paint.vertices.end(), {makeVertex(x, -0.08, 0.026f), makeVertex(x, 0.08, 0.026f),
                makeVertex(x + terrainStep, 0.08, 0.026f), makeVertex(x + terrainStep, -0.08, 0.026f)});
            quad(paint, start, start + 1, start + 2, start + 3);
        }
    }
    // Compact material-specific meshes before uploading.
    for (MeshPart* mesh : {&asphalt, &gravelMesh}) {
        std::vector<std::uint32_t> remap(vertices.size(), UINT32_MAX);
        for (auto& index : mesh->indices) {
            auto& mapped = remap[index];
            if (mapped == UINT32_MAX) {
                mapped = static_cast<std::uint32_t>(mesh->vertices.size());
                mesh->vertices.push_back(vertices[index]);
            }
            index = mapped;
        }
    }
    return chunk;
}

std::vector<CampPlacement> CampWorld::buildProps(CampTile tile, glm::dvec2 origin, int stride) const
{
    std::vector<CampPlacement> props;
    const double firstX = origin.x;
    const double firstOffset = (static_cast<double>(tile[1]) - 0.5) * tileSize;
    const auto place = [&](CampProp kind, double x, double offset, glm::vec3 size, float lift = 0.0f) {
        const double z = roadCenter(x) + offset;
        props.push_back({kind, {static_cast<float>(x - origin.x),
            static_cast<float>(meshHeight(x, z)) + size.y * 0.5f + lift,
            static_cast<float>(z - origin.y)}, size, lift < 0.1f});
    };
    constexpr int treeRows = 11;
    for (int i = 0; i < treeRows * treeRows; ++i) {
        if (stride > 1 && random(tile[0], tile[1], 490u + i) * stride > 1.0f) continue;
        const double x = firstX + tileSize / treeRows * (i / treeRows + 0.12 + 0.76 * random(tile[0], tile[1], 100u + 3u * i));
        const double offset = firstOffset + tileSize / treeRows * (i % treeRows + 0.12 + 0.76 * random(tile[0], tile[1], 101u + 3u * i));
        if (std::abs(offset) < 8.7 || gravel(x, offset, campsiteAt(x)) > 0.12f) continue;
        const float size = 2.0f + 4.4f * random(tile[0], tile[1], 102u + 3u * i);
        place(CampProp::Tree, x, offset, {size, size * 1.18f, size});
    }
    const Campsite site = campsiteAt(firstX + tileSize * 0.5);
    if (tile[1] == 0 && site.x >= firstX && site.x < firstX + tileSize) {
        const auto set = [&](CampProp kind, double x, double z, glm::vec3 size, float lift = 0.0f) {
            place(kind, site.x + x, site.offset + z * site.side, size, lift);
        };
        if (site.x == 49.0 || random(tile[0], tile[1], 53) > 0.52f) {
            set(CampProp::Shelter, 3.0, 5.0, {5.3f, 2.8f, 3.9f});
            set(CampProp::Canvas, 3.0, 5.0, {5.8f, 1.3f, 4.4f}, 2.7f);
        } else {
            set(CampProp::Canvas, 4.0, 5.0, {3.7f, 2.1f, 3.3f});
            set(CampProp::Timber, 1.0, 5.0, {1.1f, 0.85f, 1.0f});
        }
        set(CampProp::Canvas, -6.0, 2.0, {3.0f, 1.8f, 2.45f});
        set(CampProp::Canvas, -7.0, -3.0, {2.7f, 1.55f, 2.2f});
        set(CampProp::Timber, 2.0, -3.0, {2.65f, 0.26f, 1.3f}, 0.68f);
        set(CampProp::Timber, 1.25, -3.0, {0.2f, 0.7f, 1.05f});
        set(CampProp::Timber, 2.75, -3.0, {0.2f, 0.7f, 1.05f});
        place(CampProp::Timber, site.x - 14.0, 7.4 * site.side, {0.17f, 2.2f, 0.17f});
        place(CampProp::Sign, site.x - 14.0, 7.4 * site.side, {0.27f, 0.68f, 1.55f}, 1.53f);
    }
    return props;
}

void CampWorld::collide(glm::vec3& position, glm::vec3& velocity) const
{
    const auto tile = tileAt(position);
    if (obstacles_.empty() || collisionTile_ != tile) {
        collisionTile_ = tile;
        obstacles_.clear();
        for (auto x = tile[0] - 1; x <= tile[0] + 1; ++x) {
            for (auto z = tile[1] - 1; z <= tile[1] + 1; ++z) {
                const CampTile key{x, z};
                const glm::dvec2 origin = chunkOrigin(key);
                for (const auto& prop : buildProps(key, origin)) {
                    if (!prop.solid) continue;
                    const bool tree = prop.kind == CampProp::Tree;
                    const glm::vec2 halfSize = glm::vec2(prop.size.x, prop.size.z) * (tree ? 0.085f : 0.5f);
                    obstacles_.push_back({origin + glm::dvec2(prop.center.x, prop.center.z), halfSize, tree});
                }
            }
        }
    }
    glm::dvec2 center = origin_ + glm::dvec2(position.x, position.z);
    glm::vec2 motion{velocity.x, velocity.z};
    constexpr float carRadius = 0.87f;
    for (const auto& obstacle : obstacles_) {
        const glm::vec2 difference(center - obstacle.center);
        const glm::vec2 overlap = obstacle.extent + carRadius - glm::abs(difference);
        if (overlap.x <= 0.0f || overlap.y <= 0.0f) continue;
        glm::vec2 normal;
        float penetration;
        if (obstacle.round) {
            const float distance = glm::length(difference);
            penetration = obstacle.extent.x + carRadius - distance;
            if (penetration <= 0.0f) continue;
            normal = distance > 0.0001f ? difference / distance : glm::vec2(1.0f, 0.0f);
        } else if (overlap.x < overlap.y) {
            normal = {std::copysign(1.0f, difference.x), 0.0f};
            penetration = overlap.x;
        } else {
            normal = {0.0f, std::copysign(1.0f, difference.y)};
            penetration = overlap.y;
        }
        center += glm::dvec2(normal * penetration);
        motion -= normal * std::min(0.0f, glm::dot(motion, normal)) * 1.12f;
    }
    const glm::dvec2 local = center - origin_;
    position.x = static_cast<float>(local.x);
    position.z = static_cast<float>(local.y);
    velocity.x = motion.x;
    velocity.z = motion.y;
}
