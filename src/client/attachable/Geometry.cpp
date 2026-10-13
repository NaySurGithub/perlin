#include "client/attachable/Geometry.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace perlin::attachable {
    namespace {
        glm::vec3 Vec3(const Json* v, glm::vec3 fallback = glm::vec3(0.0f)) {
            if (!v || !v->IsArray() || v->array.size() < 3)
                return fallback;
            return {static_cast<float>(v->array[0].NumberOr(0)), static_cast<float>(v->array[1].NumberOr(0)),
                    static_cast<float>(v->array[2].NumberOr(0))};
        }

        glm::vec2 Vec2(const Json* v) {
            if (!v || !v->IsArray() || v->array.size() < 2)
                return glm::vec2(0.0f);
            return {static_cast<float>(v->array[0].NumberOr(0)), static_cast<float>(v->array[1].NumberOr(0))};
        }

        std::string Lower(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        void ParseBones(const Json& bones, Geometry& g) {
            for (const Json& b : bones.array) {
                if (!b.IsObject())
                    continue;
                Bone bone;
                bone.name = Lower(b.Find("name") ? b.Find("name")->StringOr("") : "");
                bone.pivot = Vec3(b.Find("pivot"));
                bone.rotation = Vec3(b.Find("rotation"));
                if (const Json* binding = b.Find("binding"))
                    bone.binding = binding->StringOr("");
                if (const Json* parent = b.Find("parent"))
                    bone.parentName = Lower(parent->StringOr(""));
                if (const Json* n = b.Find("neverRender"))
                    bone.neverRender = n->BoolOr(false);

                const Json* bind = b.Find("bind_pose_rotation");
                if (const Json* inf = b.Find("inflate"))
                    bone.inflate = static_cast<float>(inf->NumberOr(0));
                if (const Json* cubes = b.Find("cubes"); cubes && cubes->IsArray()) {
                    for (const Json& c : cubes->array) {
                        Cube cube;
                        cube.origin = Vec3(c.Find("origin"));
                        cube.size = Vec3(c.Find("size"));
                        cube.pivot = Vec3(c.Find("pivot"));
                        cube.rotation = Vec3(c.Find("rotation"));
                        cube.hasRotation = c.Find("rotation") != nullptr;

                        if (cube.hasRotation && !c.Find("pivot"))
                            cube.pivot = cube.origin + cube.size * 0.5f;
                        if (bind && !cube.hasRotation) {
                            cube.pivot = bone.pivot;
                            cube.rotation = Vec3(bind);
                            cube.hasRotation = true;
                        }
                        cube.inflate = c.Find("inflate") ? static_cast<float>(c.Find("inflate")->NumberOr(0)) : bone.inflate;
                        cube.mirror = c.Find("mirror") ? c.Find("mirror")->BoolOr(false) : (b.Find("mirror") && b.Find("mirror")->BoolOr(false));
                        if (const Json* uv = c.Find("uv")) {
                            if (uv->IsArray()) {
                                cube.uv = Vec2(uv);
                            } else if (uv->IsObject()) {
                                cube.boxUv = false;
                                static const char* kFaces[6] = {"east", "west", "north", "south", "up", "down"};
                                for (int i = 0; i < 6; i++)
                                    if (const Json* f = uv->Find(kFaces[i])) {
                                        cube.faces[i].present = true;
                                        cube.faces[i].uv = Vec2(f->Find("uv"));
                                        cube.faces[i].size = Vec2(f->Find("uv_size"));
                                    }
                            }
                        }
                        bone.cubes.push_back(cube);
                    }
                }
                if (const Json* meshes = b.Find("texture_meshes"); meshes && meshes->IsArray()) {
                    for (const Json& m : meshes->array) {
                        TextureMesh mesh;
                        mesh.texture = m.Find("texture") ? m.Find("texture")->StringOr("default") : "default";
                        mesh.position = Vec3(m.Find("position"));
                        mesh.rotation = Vec3(m.Find("rotation"));
                        mesh.scale = Vec3(m.Find("scale"), glm::vec3(1.0f));
                        mesh.localPivot = Vec3(m.Find("local_pivot"));
                        bone.meshes.push_back(std::move(mesh));
                    }
                }
                g.bones.push_back(std::move(bone));
            }
        }
    }

    glm::mat4 EulerZYX(const glm::vec3& d) {

        glm::mat4 m(1.0f);
        m = glm::rotate(m, glm::radians(d.z), glm::vec3(0, 0, 1));
        m = glm::rotate(m, glm::radians(-d.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(-d.x), glm::vec3(1, 0, 0));
        return m;
    }

    void GeometryLibrary::Load(const std::string& text) {
        Json doc;
        std::string error;
        if (!Json::Parse(text, doc, error) || !doc.IsObject())
            return;
        if (const Json* list = doc.Find("minecraft:geometry"); list && list->IsArray()) {
            for (const Json& entry : list->array) {
                Geometry g;
                const Json* desc = entry.Find("description");
                if (!desc || !desc->Find("identifier"))
                    continue;
                g.id = desc->Find("identifier")->StringOr("");
                if (const size_t colon = g.id.find(':'); colon != std::string::npos) {
                    g.parentId = g.id.substr(colon + 1);
                    g.id.resize(colon);
                }
                g.textureWidth = desc->Find("texture_width") ? static_cast<float>(desc->Find("texture_width")->NumberOr(64)) : 64.0f;
                g.textureHeight = desc->Find("texture_height") ? static_cast<float>(desc->Find("texture_height")->NumberOr(64)) : 64.0f;
                if (const Json* bones = entry.Find("bones"); bones && bones->IsArray())
                    ParseBones(*bones, g);
                geometries[g.id] = std::move(g);
            }
        }

        for (const auto& [key, value] : doc.object) {
            if (key.rfind("geometry.", 0) != 0 || !value.IsObject())
                continue;
            Geometry g;
            g.id = key.substr(0, key.find(':'));
            if (const size_t colon = key.find(':'); colon != std::string::npos)
                g.parentId = key.substr(colon + 1);
            g.textureWidth = value.Find("texturewidth") ? static_cast<float>(value.Find("texturewidth")->NumberOr(64)) : 64.0f;
            g.textureHeight = value.Find("textureheight") ? static_cast<float>(value.Find("textureheight")->NumberOr(64)) : 64.0f;
            if (const Json* bones = value.Find("bones"); bones && bones->IsArray())
                ParseBones(*bones, g);
            geometries[g.id] = std::move(g);
        }
        Link();
    }

    namespace {

        void LinkBones(Geometry& g) {
            std::unordered_map<std::string, size_t> byName;
            for (size_t i = 0; i < g.bones.size(); i++)
                byName[g.bones[i].name] = i;
            std::vector<Bone> ordered;
            std::vector<int> state(g.bones.size(), 0);
            std::unordered_map<size_t, int> newIndex;
            const std::function<void(size_t)> visit = [&](size_t i) {
                if (state[i] == 2)
                    return;
                if (state[i] == 1)
                    return;
                state[i] = 1;
                const auto p = byName.find(g.bones[i].parentName);
                if (!g.bones[i].parentName.empty() && p != byName.end() && p->second != i)
                    visit(p->second);
                newIndex[i] = static_cast<int>(ordered.size());
                ordered.push_back(g.bones[i]);
                state[i] = 2;
            };
            for (size_t i = 0; i < g.bones.size(); i++)
                visit(i);
            std::unordered_map<std::string, int> finalIndex;
            for (size_t i = 0; i < ordered.size(); i++)
                finalIndex[ordered[i].name] = static_cast<int>(i);
            for (size_t i = 0; i < ordered.size(); i++) {
                const auto p = finalIndex.find(ordered[i].parentName);
                ordered[i].parent = (ordered[i].parentName.empty() || p == finalIndex.end() || p->second >= static_cast<int>(i)) ? -1 : p->second;
            }
            g.bones = std::move(ordered);
            g.linked = true;
        }
    }

    void GeometryLibrary::Link() {
        for (int pass = 0; pass < 8; pass++) {
            bool changed = false;
            for (auto& [id, g] : geometries) {
                if (g.linked)
                    continue;
                if (!g.parentId.empty()) {
                    const auto p = geometries.find(g.parentId);
                    if (p == geometries.end() || !p->second.linked)
                        continue;
                    std::vector<Bone> merged = p->second.bones;
                    for (const Bone& b : g.bones) {
                        bool replaced = false;
                        for (Bone& m : merged)
                            if (m.name == b.name) {

                                const bool overlay = !b.cubes.empty() && !m.cubes.empty() &&
                                                     std::all_of(b.cubes.begin(), b.cubes.end(), [](const Cube& c) { return c.inflate > 0.0f; });
                                if (overlay)
                                    m.cubes.insert(m.cubes.end(), b.cubes.begin(), b.cubes.end());
                                else
                                    m = b;
                                replaced = true;
                            }
                        if (!replaced)
                            merged.push_back(b);
                    }
                    g.bones = std::move(merged);
                    if (g.textureWidth <= 0)
                        g.textureWidth = p->second.textureWidth;
                }
                LinkBones(g);
                changed = true;
            }
            if (!changed)
                break;
        }
    }

    const Geometry* GeometryLibrary::Find(const std::string& id) const {
        const auto it = geometries.find(id);
        return it == geometries.end() || !it->second.linked ? nullptr : &it->second;
    }

    namespace {
        void AddCube(const Cube& c, const Geometry& g, const glm::mat4& m, std::vector<Quad>& out) {
            const float inf = c.inflate;
            const glm::vec3 lo = c.origin - glm::vec3(inf), hi = c.origin + c.size + glm::vec3(inf);
            const float x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hi.x, y1 = hi.y, z1 = hi.z;
            glm::mat4 cubeM = m;
            if (c.hasRotation)
                cubeM = m * glm::translate(glm::mat4(1.0f), c.pivot) * EulerZYX(c.rotation) * glm::translate(glm::mat4(1.0f), -c.pivot);

            struct FaceDef {
                glm::vec3 tl, tr, br, bl;
                glm::vec3 n;
            };

            const FaceDef faces[6] = {
                {{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}, {1, 0, 0}},
                {{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}, {-1, 0, 0}},
                {{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}, {0, 0, -1}},
                {{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}, {0, 0, 1}},
                {{x1, y1, z1}, {x0, y1, z1}, {x0, y1, z0}, {x1, y1, z0}, {0, 1, 0}},
                {{x1, y0, z0}, {x0, y0, z0}, {x0, y0, z1}, {x1, y0, z1}, {0, -1, 0}},
            };
            const float w = c.size.x, h = c.size.y, d = c.size.z;
            glm::vec2 uvOrigin[6], uvSize[6];
            if (c.boxUv) {
                const float u = c.uv.x, v = c.uv.y;
                uvOrigin[0] = {u, v + d};             uvSize[0] = {d, h};
                uvOrigin[1] = {u + d + w, v + d};     uvSize[1] = {d, h};
                uvOrigin[2] = {u + d, v + d};         uvSize[2] = {w, h};
                uvOrigin[3] = {u + d + w + d, v + d}; uvSize[3] = {w, h};
                uvOrigin[4] = {u + d, v};             uvSize[4] = {w, d};
                uvOrigin[5] = {u + d + w, v};         uvSize[5] = {w, d};
            } else {
                for (int i = 0; i < 6; i++) {
                    uvOrigin[i] = c.faces[i].uv;
                    uvSize[i] = c.faces[i].size;
                }
            }
            for (int i = 0; i < 6; i++) {
                if (!c.boxUv && !c.faces[i].present)
                    continue;
                int fi = i;
                bool flip = c.mirror;
                if (c.mirror && i < 2)
                    fi = 1 - i;
                const FaceDef& f = faces[i];
                const glm::vec2 o = uvOrigin[fi], s = uvSize[fi];
                glm::vec2 a = o, b = o + glm::vec2(s.x, 0), cc = o + s, dd = o + glm::vec2(0, s.y);
                if (flip) {
                    std::swap(a, b);
                    std::swap(dd, cc);
                }
                Quad q;
                const glm::vec3* corners[4] = {&f.tl, &f.tr, &f.br, &f.bl};
                const glm::vec2* uvs[4] = {&a, &b, &cc, &dd};
                for (int k = 0; k < 4; k++) {
                    q.p[k] = glm::vec3(cubeM * glm::vec4(*corners[k], 1.0f));
                    q.uv[k] = {uvs[k]->x / g.textureWidth, uvs[k]->y / g.textureHeight};
                }
                q.n = glm::normalize(glm::mat3(cubeM) * f.n);
                out.push_back(q);
            }
        }

        void AddTextureMesh(const TextureMesh& tm, const Texture& tex, const glm::mat4& boneM, std::vector<Quad>& out) {
            if (tex.width <= 0 || tex.height <= 0)
                return;
            const int w = tex.width, h = tex.height;
            const float sx = 16.0f / static_cast<float>(w), sz = 16.0f / static_cast<float>(h);
            glm::mat4 m = boneM * glm::translate(glm::mat4(1.0f), tm.position) * EulerZYX(tm.rotation) *
                          glm::scale(glm::mat4(1.0f), tm.scale) * glm::translate(glm::mat4(1.0f), -tm.localPivot);
            const auto P = [&](float px, float pz, float y) { return glm::vec3(m * glm::vec4(px * sx, y, pz * sz, 1.0f)); };
            const auto alpha = [&](int x, int y) {
                return x < 0 || y < 0 || x >= w || y >= h ? 0 : tex.pixels[(static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4 + 3];
            };
            const glm::vec3 up = glm::normalize(glm::mat3(m) * glm::vec3(0, 1, 0));
            Quad top;
            top.p[0] = P(0, 0, 1); top.p[1] = P(w, 0, 1); top.p[2] = P(w, h, 1); top.p[3] = P(0, h, 1);
            top.uv[0] = {0, 0}; top.uv[1] = {1, 0}; top.uv[2] = {1, 1}; top.uv[3] = {0, 1};
            top.n = up;
            out.push_back(top);
            Quad bottom;
            bottom.p[0] = P(w, 0, 0); bottom.p[1] = P(0, 0, 0); bottom.p[2] = P(0, h, 0); bottom.p[3] = P(w, h, 0);
            bottom.uv[0] = {1, 0}; bottom.uv[1] = {0, 0}; bottom.uv[2] = {0, 1}; bottom.uv[3] = {1, 1};
            bottom.n = -up;
            out.push_back(bottom);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    if (alpha(x, y) == 0)
                        continue;
                    const glm::vec2 c((static_cast<float>(x) + 0.5f) / static_cast<float>(w), (static_cast<float>(y) + 0.5f) / static_cast<float>(h));
                    const float fx = static_cast<float>(x), fz = static_cast<float>(y);
                    const auto side = [&](glm::vec3 a, glm::vec3 b, glm::vec3 cc, glm::vec3 d, glm::vec3 n) {
                        Quad q;
                        q.p[0] = a; q.p[1] = b; q.p[2] = cc; q.p[3] = d;
                        for (auto& t : q.uv) t = c;
                        q.n = glm::normalize(glm::mat3(m) * n);
                        out.push_back(q);
                    };
                    if (alpha(x - 1, y) == 0) side(P(fx, fz, 0), P(fx, fz, 1), P(fx, fz + 1, 1), P(fx, fz + 1, 0), {-1, 0, 0});
                    if (alpha(x + 1, y) == 0) side(P(fx + 1, fz, 1), P(fx + 1, fz, 0), P(fx + 1, fz + 1, 0), P(fx + 1, fz + 1, 1), {1, 0, 0});
                    if (alpha(x, y - 1) == 0) side(P(fx, fz, 1), P(fx + 1, fz, 1), P(fx + 1, fz, 0), P(fx, fz, 0), {0, 0, -1});
                    if (alpha(x, y + 1) == 0) side(P(fx, fz + 1, 0), P(fx + 1, fz + 1, 0), P(fx + 1, fz + 1, 1), P(fx, fz + 1, 1), {0, 0, 1});
                }
        }
    }

    void BuildMesh(const Geometry& g, const Pose& pose, const TextureLookup& textures, BuiltMesh& out,
                   const BoneFilter& visible) {
        std::vector<glm::mat4> world(g.bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < g.bones.size(); i++) {
            const Bone& b = g.bones[i];
            BoneXform x;
            if (const auto it = pose.find(b.name); it != pose.end())
                x = it->second;
            glm::mat4 local = glm::translate(glm::mat4(1.0f), x.position) * glm::translate(glm::mat4(1.0f), b.pivot) *
                              EulerZYX(b.rotation + x.rotation) * glm::scale(glm::mat4(1.0f), x.scale) *
                              glm::translate(glm::mat4(1.0f), -b.pivot);
            world[i] = (b.parent >= 0 ? world[static_cast<size_t>(b.parent)] : glm::mat4(1.0f)) * local;
            if (b.neverRender || (visible && !visible(b.name)))
                continue;
            for (const Cube& c : b.cubes)
                AddCube(c, g, world[i], out.body);
            for (const TextureMesh& tm : b.meshes) {
                const Texture* tex = textures ? textures(tm.texture) : nullptr;
                if (!tex)
                    continue;
                BuiltMesh::Extra extra;
                extra.texture = tm.texture;
                AddTextureMesh(tm, *tex, world[i], extra.quads);
                out.extras.push_back(std::move(extra));
            }
        }
    }

    bool BoneWorld(const Geometry& g, const Pose& pose, const std::string& name, glm::mat4& out, glm::vec3& pivot) {
        std::vector<glm::mat4> world(g.bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < g.bones.size(); i++) {
            const Bone& b = g.bones[i];
            BoneXform x;
            if (const auto it = pose.find(b.name); it != pose.end())
                x = it->second;
            const glm::mat4 local = glm::translate(glm::mat4(1.0f), x.position) * glm::translate(glm::mat4(1.0f), b.pivot) *
                                    EulerZYX(b.rotation + x.rotation) * glm::scale(glm::mat4(1.0f), x.scale) *
                                    glm::translate(glm::mat4(1.0f), -b.pivot);
            world[i] = (b.parent >= 0 ? world[static_cast<size_t>(b.parent)] : glm::mat4(1.0f)) * local;
            if (b.name == name) {
                out = world[i];
                pivot = b.pivot;
                return true;
            }
        }
        return false;
    }
}
