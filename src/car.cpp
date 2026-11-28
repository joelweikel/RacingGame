#include "car.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <nlohmann/json.hpp>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

struct GltfFile {
    explicit GltfFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open " + path.string());
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        cgltf_options options{};
        cgltf_data* parsed = nullptr;
        if (cgltf_parse(&options, bytes.data(), bytes.size(), &parsed) != cgltf_result_success) {
            throw std::runtime_error("Cannot parse " + path.string());
        }
        data.reset(parsed);
        if (cgltf_load_buffers(&options, data.get(), nullptr) != cgltf_result_success ||
            cgltf_validate(data.get()) != cgltf_result_success) {
            throw std::runtime_error("Invalid mesh buffers in " + path.string());
        }
    }

    std::vector<char> bytes;
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data{nullptr, cgltf_free};
};

std::string_view blenderBaseName(std::string_view name)
{
    const auto dot = name.rfind('.');
    if (dot != std::string_view::npos && name.size() - dot >= 4 &&
        name.find_first_not_of("0123456789", dot + 1) == std::string_view::npos) {
        return name.substr(0, dot);
    }
    return name;
}

const cgltf_node* exactNode(const cgltf_data& data, std::string_view name, bool mesh)
{
    for (std::size_t i = 0; i < data.nodes_count; ++i) {
        const cgltf_node& node = data.nodes[i];
        if ((node.mesh != nullptr) == mesh && node.name && name == node.name) {
            return &node;
        }
    }
    return nullptr;
}

const cgltf_node* namedNode(const cgltf_data& data, std::string_view name, bool mesh)
{
    if (const cgltf_node* node = exactNode(data, name, mesh)) {
        return node;
    }
    const cgltf_node* suffixed = nullptr;
    for (std::size_t i = 0; i < data.nodes_count; ++i) {
        const cgltf_node& node = data.nodes[i];
        if ((node.mesh != nullptr) == mesh && node.name && name == blenderBaseName(node.name)) {
            if (suffixed) {
                throw std::runtime_error("Multiple nodes named like " + std::string(name));
            }
            suffixed = &node;
        }
    }
    return suffixed;
}

const cgltf_node& meshNode(const cgltf_data& data, std::string_view name)
{
    if (const cgltf_node* node = namedNode(data, name, true)) {
        return *node;
    }
    throw std::runtime_error("Missing mesh node: " + std::string(name));
}

const cgltf_node* wheelNode(const cgltf_data& data, const char* name)
{
    if (const auto* assembly = namedNode(data, name, false)) return assembly;
    return namedNode(data, name, true);
}

glm::mat4 nodeTransform(const cgltf_node& node)
{
    cgltf_float values[16];
    cgltf_node_transform_world(&node, values);
    return glm::make_mat4(values);
}

glm::vec3 findHub(const cgltf_data& data, std::string_view socket, std::string_view name, bool combinedGale)
{
    if (combinedGale) {
        const std::string galeName = std::string(name) + ".001";
        if (const cgltf_node* node = exactNode(data, galeName, false)) {
            return glm::vec3(nodeTransform(*node)[3]);
        }
        throw std::runtime_error("Missing Gale wheel socket: " + galeName);
    }
    for (std::size_t i = 0; i < data.nodes_count; ++i) {
        const cgltf_node& node = data.nodes[i];
        if (!node.extras.data) {
            continue;
        }
        const auto extras = nlohmann::json::parse(node.extras.data, nullptr, false);
        const auto value = extras.find("socket");
        if (value != extras.end() && value->is_string() &&
            std::string_view(value->get_ref<const std::string&>()) == socket) {
            return glm::vec3(nodeTransform(node)[3]);
        }
    }
    if (const cgltf_node* node = namedNode(data, name, false)) {
        return glm::vec3(nodeTransform(*node)[3]);
    }
    throw std::runtime_error("Missing wheel socket: " + std::string(socket));
}

Surface readSurface(const cgltf_material* material, const glm::vec3& fallbackColor, bool carMaterials)
{
    Surface surface;
    surface.color = fallbackColor;
    if (!material) {
        return surface;
    }
    if (material->has_pbr_metallic_roughness) {
        const auto& pbr = material->pbr_metallic_roughness;
        surface.color = glm::make_vec3(pbr.base_color_factor);
        surface.metallic = pbr.metallic_factor;
        surface.roughness = pbr.roughness_factor;
        if (material->alpha_mode == cgltf_alpha_mode_blend) {
            surface.opacity = pbr.base_color_factor[3];
        }
    }
    const float strength = material->has_emissive_strength ? material->emissive_strength.emissive_strength : 1.0f;
    surface.emission = glm::make_vec3(material->emissive_factor) * strength;
    if (material->has_clearcoat) {
        surface.clearcoat = material->clearcoat.clearcoat_factor;
    }
    if (!carMaterials) return surface;

    std::string name(blenderBaseName(material->name ? material->name : ""));
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
    if (name == "paint" || name == "painty") {
        surface.paint = true;
        surface.roughness = 0.21f;
        surface.metallic = 0.28f;
        surface.clearcoat = 1.0f;
    } else if (name == "black") {
        surface.color = glm::vec3(0.006f, 0.008f, 0.010f);
        surface.metallic = 0.0f;
        surface.roughness = 0.16f;
        surface.clearcoat = 0.75f;
    } else if (name == "glass") {
        surface.color = glm::vec3(0.92f, 0.96f, 1.0f);
        surface.metallic = 0.0f;
        surface.roughness = 0.09f;
        surface.opacity = 0.14f;
    } else if (name == "tailglass") {
        surface.color = glm::vec3(0.53f, 0.012f, 0.02f);
        surface.metallic = 0.0f;
        surface.roughness = 0.13f;
        surface.opacity = 0.42f;
        surface.lensTint = 0.86f;
    } else if (name == "taillight" || name == "brakelight") {
        const bool brake = name == "brakelight";
        surface.color = glm::vec3(0.058f, 0.0015f, 0.002f);
        surface.roughness = 0.30f;
        surface.metallic = 0.0f;
        surface.emission = glm::vec3(1.0f, 0.007f, 0.004f) * (brake ? 4.8f : 1.7f);
        surface.lightControl = brake ? LightControl::Brake : LightControl::Night;
    } else if (name == "drl") {
        surface.color = glm::vec3(0.0f);
        surface.emission = glm::vec3(1.0f, 0.95f, 0.88f) * std::max(3.0f, strength);
        surface.lightControl = LightControl::Headlamp;
    } else if (name == "metal") {
        surface.color = glm::vec3(0.25f, 0.28f, 0.32f);
        surface.metallic = 1.0f;
        surface.roughness = 0.45f;
    } else if (name == "tire" || name == "matteblack") {
        surface.metallic = 0.0f;
        surface.roughness = 0.82f;
    }
    return surface;
}

using DecodedImages = std::vector<std::pair<const cgltf_image*, std::shared_ptr<const MeshPart::Image>>>;

void readSceneryMaterial(MeshPart& part, const cgltf_primitive& primitive, DecodedImages& images)
{
    const auto* material = primitive.material;
    if (!material) return;
    if (material->alpha_mode == cgltf_alpha_mode_mask) part.alphaCutoff = material->alpha_cutoff;
    if (!material->has_pbr_metallic_roughness) return;
    const auto& pbr = material->pbr_metallic_roughness;
    part.alphaFactor = pbr.base_color_factor[3];
    const auto& view = pbr.base_color_texture;
    if (!view.texture) return;

    const cgltf_image* image = view.texture->image;
    const auto* buffer = image ? image->buffer_view : nullptr;
    if (!buffer || buffer->size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Scenery base color needs an embedded PNG or JPEG");
    }
    const int texCoord = view.has_transform && view.transform.has_texcoord ? view.transform.texcoord : view.texcoord;
    const auto* accessor = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, texCoord);
    if (!accessor || accessor->count != part.vertices.size()) {
        throw std::runtime_error("Scenery base color texture has no matching UVs");
    }
    std::vector<float> uvs(part.vertices.size() * 2);
    if (cgltf_accessor_unpack_floats(accessor, uvs.data(), uvs.size()) != uvs.size()) {
        throw std::runtime_error("Could not read scenery UVs");
    }
    const auto& transform = view.transform;
    const float sine = std::sin(transform.rotation);
    const float cosine = std::cos(transform.rotation);
    for (std::size_t i = 0; i < part.vertices.size(); ++i) {
        glm::vec2 uv{uvs[i * 2], uvs[i * 2 + 1]};
        if (view.has_transform) {
            uv *= glm::make_vec2(transform.scale);
            uv = glm::vec2(cosine * uv.x - sine * uv.y, sine * uv.x + cosine * uv.y) + glm::make_vec2(transform.offset);
        }
        part.vertices[i].uv = uv;
    }

    const auto cached = std::find_if(images.begin(), images.end(), [image](const auto& item) { return item.first == image; });
    if (cached != images.end()) {
        part.baseColor = cached->second;
        return;
    }
    auto decoded = std::make_shared<MeshPart::Image>();
    const auto* storage = static_cast<const unsigned char*>(buffer->buffer->data) + buffer->offset;
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(storage, static_cast<int>(buffer->size), &decoded->width, &decoded->height, nullptr, 4),
        stbi_image_free);
    if (!pixels) throw std::runtime_error("Could not decode the scenery base color texture");
    const auto count = static_cast<std::size_t>(decoded->width) * decoded->height * 4;
    decoded->rgba.assign(pixels.get(), pixels.get() + count);
    part.baseColor = decoded;
    images.emplace_back(image, std::move(decoded));
}

std::vector<MeshPart> readMesh(const cgltf_node& node, const glm::mat4& transform, const glm::vec3& fallbackColor,
                               DecodedImages* images = nullptr)
{
    const glm::mat3 linearTransform(transform);
    const glm::mat3 normalTransform = glm::transpose(glm::inverse(linearTransform));
    const bool reverseWinding = glm::determinant(linearTransform) < 0.0f;
    std::vector<MeshPart> mesh;
    for (std::size_t i = 0; i < node.mesh->primitives_count; ++i) {
        const cgltf_primitive& primitive = node.mesh->primitives[i];
        const cgltf_accessor* positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
        const cgltf_accessor* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
        if (primitive.type != cgltf_primitive_type_triangles || !positions) {
            throw std::runtime_error("Expected a triangle mesh on " + std::string(node.name ? node.name : "unnamed node"));
        }

        std::vector<float> coordinates(positions->count * 3);
        std::vector<float> normalValues(coordinates.size());
        if (cgltf_accessor_unpack_floats(positions, coordinates.data(), coordinates.size()) != coordinates.size() ||
            (normals && cgltf_accessor_unpack_floats(normals, normalValues.data(), normalValues.size()) != normalValues.size())) {
            throw std::runtime_error("Could not read vertices on " + std::string(node.name ? node.name : "unnamed node"));
        }

        MeshPart part;
        part.surface = readSurface(primitive.material, fallbackColor, images == nullptr);
        part.vertices.reserve(positions->count);
        for (std::size_t vertex = 0; vertex < coordinates.size(); vertex += 3) {
            const glm::vec3 position = glm::make_vec3(coordinates.data() + vertex);
            const glm::vec3 normal = normals ? glm::make_vec3(normalValues.data() + vertex) : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 adjustedNormal = normalTransform * normal;
            part.vertices.push_back({
                glm::vec3(transform * glm::vec4(position, 1.0f)),
                glm::dot(adjustedNormal, adjustedNormal) > 0.0f ? glm::normalize(adjustedNormal) : glm::vec3(0.0f, 1.0f, 0.0f),
            });
        }

        if (primitive.indices) {
            part.indices.resize(primitive.indices->count);
            if (cgltf_accessor_unpack_indices(primitive.indices, part.indices.data(),
                                              sizeof(std::uint32_t), part.indices.size()) != part.indices.size()) {
                throw std::runtime_error("Could not read indices on " + std::string(node.name));
            }
        } else {
            part.indices.resize(part.vertices.size());
            std::iota(part.indices.begin(), part.indices.end(), 0u);
        }
        if (reverseWinding) {
            for (std::size_t i = 2; i < part.indices.size(); i += 3) {
                std::swap(part.indices[i - 1], part.indices[i]);
            }
        }

        if (!part.vertices.empty() && !part.indices.empty()) {
            if (images) readSceneryMaterial(part, primitive, *images);
            mesh.push_back(std::move(part));
        }
    }
    if (mesh.empty()) {
        throw std::runtime_error("Empty mesh on " + std::string(node.name ? node.name : "unnamed node"));
    }
    return mesh;
}

void appendParts(const cgltf_node& node, const glm::mat4& space, std::vector<MeshPart>& parts,
                 CarPart visibility = CarPart::Always)
{
    if (node.mesh) {
        auto detail = readMesh(node, space * nodeTransform(node), {0.50f, 0.50f, 0.50f});
        for (auto& part : detail) {
            part.carPart = visibility;
            parts.push_back(std::move(part));
        }
    }
    for (std::size_t i = 0; i < node.children_count; ++i) {
        appendParts(*node.children[i], space, parts, visibility);
    }
}

glm::vec3 mirrorHub(glm::vec3 hub)
{
    // Vehicle assets use +X forward, +Y up, and Z across the axles.
    hub.z = -hub.z;
    return hub;
}

} // namespace

bool CarSetup::shows(CarPart part) const
{
    switch (part) {
    case CarPart::HoodIntake: return hoodIntake;
    case CarPart::Wing: return wing;
    case CarPart::Splitter: return splitter;
    case CarPart::Gasoline: return !diesel;
    case CarPart::Diesel: return diesel;
    case CarPart::Always: return true;
    }
    return true;
}

const CarSpec& carSpec(CarModel model)
{
    static constexpr CarSpec slimRider{
        "SlimRider", "cars.glb", 2.51f, 0.085f, 1540.0f, {2450.0f, 2180.0f, 650.0f},
        0.48f, 0.0f, 1.0f, 6340.0f, 2250.0f,
        {{{850.0f, 340.0f}, {1750.0f, 510.0f}, {3000.0f, 650.0f},
          {4750.0f, 765.0f}, {6100.0f, 705.0f}, {6700.0f, 610.0f}}},
    };
    static constexpr CarSpec gale{
        "Gale", "gale.glb", 2.65f, 0.075f, 1420.0f, {2100.0f, 1850.0f, 590.0f},
        0.43f, 0.43f, 0.70f, 8400.0f, 3300.0f,
        {{{850.0f, 200.0f}, {2500.0f, 300.0f}, {4300.0f, 380.0f},
          {6500.0f, 453.0f}, {8000.0f, 478.0f}, {8700.0f, 456.0f}}},
    };
    return model == CarModel::Gale ? gale : slimRider;
}

const CarSpec& powertrainSpec(CarModel model, bool diesel)
{
    const auto dieselVersion = [](CarSpec spec) {
        spec.upshiftRpm = 4200.0f;
        spec.downshiftRpm = 2050.0f;
        spec.torqueCurve = {{{850, 420}, {1400, 640}, {2000, 1260}, {2850, 1420}, {3900, 1120}, {4600, 780}}};
        return spec;
    };
    static const CarSpec slimDiesel = dieselVersion(carSpec(CarModel::SlimRider));
    static const CarSpec galeDiesel = dieselVersion(carSpec(CarModel::Gale));
    return diesel ? (model == CarModel::Gale ? galeDiesel : slimDiesel) : carSpec(model);
}

namespace {

CarAsset readCar(const cgltf_data& data, CarModel model)
{
    const bool isGale = model == CarModel::Gale;
    const cgltf_node* galeBody = isGale ? exactNode(data, "Body.001", true) : nullptr;
    const bool combinedGale = galeBody && exactNode(data, "Body", true);
    const cgltf_node& body = galeBody ? *galeBody : meshNode(data, "Body");
    const cgltf_node* wheel = wheelNode(data, isGale ? "GaleWheel" : "SlimRiderWheel");
    const bool faceMounted = wheel || wheelNode(data, "GaleWheel") || exactNode(data, "Exhaust", false);
    if (!wheel && isGale) wheel = exactNode(data, "SimpleWheel.001", true);
    if (!wheel) wheel = &meshNode(data, "SimpleWheel");
    const glm::vec3 authoredFront = findHub(data, "FL", "FrontLeftWheel", combinedGale);
    const glm::vec3 authoredRear = findHub(data, "RL", "RearLeftWheel", combinedGale);
    const float authoredWheelbase = authoredFront.x - authoredRear.x;
    if (authoredWheelbase <= 0.0f) {
        throw std::runtime_error("Expected front hubs ahead of rear hubs");
    }
    const CarSpec& spec = carSpec(model);
    const float metersPerUnit = spec.wheelbase / authoredWheelbase;
    const float centerX = spec.frontWeightFraction > 0.0f
        ? authoredRear.x + authoredWheelbase * spec.frontWeightFraction : 0.0f;
    const glm::vec3 origin(centerX, 0.0f, 0.0f);
    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(metersPerUnit));
    const glm::mat4 bodySpace = glm::translate(scale, -origin);
    glm::vec3 front = (authoredFront - origin) * metersPerUnit;
    glm::vec3 rear = (authoredRear - origin) * metersPerUnit;
    front.z = std::copysign(std::max(0.0f, std::abs(front.z) - spec.wheelInset), front.z);
    rear.z = std::copysign(std::max(0.0f, std::abs(rear.z) - spec.wheelInset), rear.z);

    CarAsset car;
    car.model = model;
    car.body = readMesh(body, bodySpace * nodeTransform(body), {0.62f, 0.14f, 0.09f});
    if (faceMounted) {
        // Blender +Y exports as -Z. Rotate the face toward the left-side socket.
        const glm::mat4 facing = glm::scale(scale, glm::vec3(-1.0f, 1.0f, -1.0f));
        appendParts(*wheel, facing, car.wheel);
        const auto* disc = namedNode(data, "MedDisc", true);
        const auto* caliper = namedNode(data, "MedCaliper", true);
        if (disc) {
            appendParts(*disc, facing, car.brakeDisc);
            if (!caliper) {
                // This combined export retained the caliper's original Blender object name.
                const auto* cube = exactNode(data, "Cube", true);
                if (cube && !cube->parent && cube->mesh->name &&
                    std::string_view(cube->mesh->name) == "Cube.021") caliper = cube;
            }
        }
        if (caliper) {
            appendParts(*caliper, facing, car.brakeCaliper);
            for (auto& part : car.brakeCaliper) part.surface.paint = false;
        }
        float inner = std::numeric_limits<float>::infinity();
        float outer = -inner;
        for (const auto& part : car.wheel) {
            for (const auto& vertex : part.vertices) {
                inner = std::min(inner, vertex.position.z);
                outer = std::max(outer, vertex.position.z);
            }
        }
        if (car.wheel.empty()) throw std::runtime_error("Wheel assembly has no meshes");
        const float center = (inner + outer) * 0.5f;
        car.wheelTransform[3].z = -center;
        front.z = authoredFront.z * metersPerUnit + center - 0.014f;
        rear.z = authoredRear.z * metersPerUnit + center - 0.014f;
    } else {
        car.wheel = readMesh(*wheel, glm::mat4(1.0f), {0.12f, 0.13f, 0.15f});
        car.wheelTransform = scale * nodeTransform(*wheel);
        car.wheelTransform[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    car.hubs = {front, mirrorHub(front), rear, mirrorHub(rear)};

    float bodyMinZ = std::numeric_limits<float>::infinity();
    float bodyMaxZ = -bodyMinZ;
    for (const MeshPart& part : car.body) {
        for (const Vertex& vertex : part.vertices) {
            bodyMinZ = std::min(bodyMinZ, vertex.position.z);
            bodyMaxZ = std::max(bodyMaxZ, vertex.position.z);
        }
    }
    constexpr float centerlineTolerance = 0.001f;
    const bool halfBody = (std::abs(bodyMinZ) < centerlineTolerance && bodyMaxZ > centerlineTolerance) ||
                          (std::abs(bodyMaxZ) < centerlineTolerance && bodyMinZ < -centerlineTolerance);
    for (MeshPart& part : car.body) {
        part.mirrorAcrossBody = halfBody;
    }
    const auto lamps = [&](LightControl control, float axleOffset) {
        const bool frontLamp = control == LightControl::Headlamp;
        const auto& hub = car.hubs[frontLamp ? 0 : 2];
        const glm::vec3 fallback(hub.x + axleOffset, hub.y + 0.28f, std::abs(hub.z) * 0.82f);
        std::array<glm::vec3, 2> centers{fallback, mirrorHub(fallback)};
        for (std::size_t side = 0; side < centers.size(); ++side) {
            glm::vec3 min(std::numeric_limits<float>::infinity()), max(-min);
            for (const auto& part : car.body) {
                if (part.surface.lightControl != control) continue;
                for (const auto& vertex : part.vertices) {
                    const auto p = vertex.position;
                    if ((p.z >= 0.0f) != (side == 0)) continue;
                    min = glm::min(min, p);
                    max = glm::max(max, p);
                }
            }
            if (min.x <= max.x) centers[side] = (min + max) * 0.5f;
        }
        if (halfBody) {
            if (std::abs(bodyMinZ) < centerlineTolerance) centers[1] = mirrorHub(centers[0]);
            else centers[0] = mirrorHub(centers[1]);
        }
        // Move the cone origins clear of their housings.
        for (auto& center : centers) center.x += frontLamp ? 0.10f : -0.08f;
        return centers;
    };
    car.headlights = lamps(LightControl::Headlamp, 0.59f);
    car.taillights = lamps(LightControl::Night, -0.61f);
    const auto append = [&](const cgltf_node& node, CarPart visibility) {
        if (&node != wheel) appendParts(node, bodySpace, car.body, visibility);
    };
    const auto addNamed = [&](const char* name, CarPart visibility) {
        if (const auto* node = namedNode(data, name, true)) append(*node, visibility);
    };
    const bool separateGale = isGale && !combinedGale;
    for (std::size_t i = 0; i < body.children_count; ++i) {
        const auto& child = *body.children[i];
        const auto name = blenderBaseName(child.name ? child.name : "");
        if (name == "HoodIntake" || name == "Wing" || name == "Splitter" || name == "Diesel") continue;
        append(child, name == "ExhaustSide" ? CarPart::Diesel :
                     name == "engine" ? CarPart::Gasoline : CarPart::Always);
    }
    if (separateGale) {
        for (const char* detail : {"enginebay", "mirror", "exhaust"}) {
            const auto* node = namedNode(data, detail, true);
            if (node && !node->parent) append(*node, CarPart::Always);
        }
        if (const auto* engine = namedNode(data, "engine", true); engine && !engine->parent) {
            append(*engine, CarPart::Gasoline);
        }
    }
    const auto addOption = [&](const char* name, CarPart visibility) {
        const std::string nodeName = combinedGale ? std::string(name) + ".001" : name;
        if (const auto* node = exactNode(data, nodeName, true)) append(*node, visibility);
        else if (separateGale) addNamed(name, visibility);
    };
    addOption("HoodIntake", CarPart::HoodIntake);
    addOption("Wing", CarPart::Wing);
    addOption("Splitter", CarPart::Splitter);
    if (isGale) {
        addNamed("Diesel", CarPart::Diesel);
    } else {
        if (faceMounted) addNamed("Cylinder", CarPart::Gasoline);
        if (const auto* side = namedNode(data, "ExhaustSide", true); side && !side->parent) {
            append(*side, CarPart::Diesel);
        }
    }

    const auto outlet = [&](const cgltf_node& node, glm::vec3 direction) {
        const glm::mat4 transform = nodeTransform(node);
        return CarAsset::Exhaust{glm::vec3(bodySpace * transform[3]), glm::normalize(direction)};
    };
    for (std::size_t i = 0; i < data.nodes_count; ++i) {
        const auto& node = data.nodes[i];
        if (node.mesh || node.children_count || !node.name || blenderBaseName(node.name) != "Exhaust") continue;
        const glm::mat4 transform = nodeTransform(node);
        // In the combined file, the single center tip belongs to SlimRider.
        const bool centerTip = std::abs(transform[3].z) < 0.1f;
        if (!faceMounted || !exactNode(data, "Body.001", true) || centerTip != isGale) {
            car.gasolineExhaust.push_back(outlet(node, glm::vec3(transform[1])));
        }
    }
    if (!isGale) {
        if (const auto* side = namedNode(data, "DieselExhaust", false)) {
            // The side exit faces outboard. The exported arrow shares the rear tips' rotation.
            car.dieselExhaust.push_back(outlet(*side, glm::vec3(nodeTransform(*side)[2])));
        }
    }
    if (car.dieselExhaust.empty()) car.dieselExhaust = car.gasolineExhaust;

    for (const MeshPart& part : car.wheel) {
        for (const Vertex& vertex : part.vertices) {
            const glm::vec4 point = car.wheelTransform * glm::vec4(vertex.position, 1.0f);
            car.wheelRadius = std::max(car.wheelRadius, glm::length(glm::vec2(point)));
            car.wheelHalfWidth = std::max(car.wheelHalfWidth, std::abs(point.z));
        }
    }
    if (car.wheelRadius <= 0.0f) {
        throw std::runtime_error("Expected a positive wheel radius");
    }
    car.groundHeight = std::min(front.y, rear.y) - car.wheelRadius;
    return car;
}

} // namespace

CarAsset loadCar(const std::filesystem::path& path, CarModel model)
{
    return readCar(*GltfFile(path).data, model);
}

CarAsset loadGameCar(const std::filesystem::path& assets, CarModel model)
{
    const auto path = assets / carSpec(model).assetFile;
    const auto galePath = assets / carSpec(CarModel::Gale).assetFile;
    if (model == CarModel::SlimRider && std::filesystem::is_regular_file(galePath)) {
        const GltfFile combined(galePath);
        if (exactNode(*combined.data, "Body", true) && exactNode(*combined.data, "Body.001", true) &&
            (wheelNode(*combined.data, "SlimRiderWheel") || wheelNode(*combined.data, "GaleWheel"))) {
            return readCar(*combined.data, model);
        }
    }
    return loadCar(path, model);
}

std::vector<MeshPart> loadScenery(const std::filesystem::path& path)
{
    const GltfFile file(path);
    const auto& data = *file.data;
    const cgltf_scene* scene = data.scene ? data.scene : data.scenes_count > 0 ? &data.scenes[0] : nullptr;
    DecodedImages images;
    std::vector<MeshPart> parts;
    const auto visit = [&](const auto& self, const cgltf_node& node) -> void {
        if (node.mesh) {
            auto meshes = readMesh(node, nodeTransform(node), {0.14f, 0.38f, 0.17f}, &images);
            parts.insert(parts.end(), std::make_move_iterator(meshes.begin()), std::make_move_iterator(meshes.end()));
        }
        for (std::size_t i = 0; i < node.children_count; ++i) self(self, *node.children[i]);
    };
    if (scene) {
        for (std::size_t i = 0; i < scene->nodes_count; ++i) visit(visit, *scene->nodes[i]);
    } else {
        for (std::size_t i = 0; i < data.nodes_count; ++i) {
            if (!data.nodes[i].parent) visit(visit, data.nodes[i]);
        }
    }
    if (parts.empty()) throw std::runtime_error("No scenery mesh in " + path.string());
    return parts;
}
