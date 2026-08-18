#pragma once
#include <string>
#include <vector>
#include <functional>
#include "offsets.hpp"
#include "memory.hpp"
#include "math.hpp"

class RbxInstance {
public:
    uintptr_t ptr = 0;

    explicit RbxInstance(uintptr_t p = 0) : ptr(p) {}
    bool valid() const { return ptr != 0; }

    static constexpr size_t CHILD_STRIDE = 16;

    std::string get_class() const {
        if (!ptr) return {};
        uintptr_t desc = rpm<uintptr_t>(ptr + offsets::Instance::ClassDescriptor);
        if (!desc) return {};
        uintptr_t name_ptr = rpm<uintptr_t>(desc + offsets::ClassDescriptor::Name);
        if (!name_ptr) return {};
        char buf[64]{};
        mem::vm_read(mem::g_proc, (LPCVOID)name_ptr, buf, 63, nullptr);
        return buf;
    }

    std::string get_name() const {
        if (!ptr) return {};

        if (offsets::Instance::NameContainer) {
            uintptr_t container = rpm<uintptr_t>(ptr + offsets::Instance::NameContainer);
            if (container) {
                auto s = rpm_string(container + offsets::Instance::Name);
                if (!s.empty()) return s;

                uintptr_t indirect = rpm<uintptr_t>(container + offsets::Instance::Name);
                if (indirect) {
                    auto s2 = rpm_string(indirect);
                    if (!s2.empty()) return s2;
                }
            }
        }

        uintptr_t name_ptr = rpm<uintptr_t>(ptr + offsets::Instance::Name);
        return rpm_string(name_ptr);
    }

    std::vector<RbxInstance> get_children() const {
        if (!ptr) return {};
        uintptr_t children_start = rpm<uintptr_t>(ptr + offsets::Instance::Children);
        if (!children_start) return {};
        uintptr_t array_base = rpm<uintptr_t>(children_start);
        uintptr_t array_end  = rpm<uintptr_t>(children_start + 0x8);
        if (!array_base || array_end <= array_base) return {};

        size_t count = (array_end - array_base) / CHILD_STRIDE;
        if (count > 4096) return {};

        std::vector<RbxInstance> out;
        out.reserve(count);
        for (size_t i = 0; i < count; i++) {
            uintptr_t child = rpm<uintptr_t>(array_base + i * CHILD_STRIDE);
            if (child) out.emplace_back(child);
        }
        return out;
    }

    RbxInstance find_child_by_class(const std::string& cls) const {
        for (auto& c : get_children())
            if (c.get_class() == cls) return c;
        return RbxInstance{};
    }

    RbxInstance find_child_by_name(const std::string& name) const {
        for (auto& c : get_children())
            if (c.get_name() == name) return c;
        return RbxInstance{};
    }
};

struct RbxVisualEngine {
    uintptr_t ptr = 0;

    static RbxVisualEngine get() {
        if (offsets::VisualEngine::Pointer) {
            uintptr_t p = rpm<uintptr_t>(mem::g_base + offsets::VisualEngine::Pointer);
            if (p) return {p};
        }
        if (runtime::ve_addr) return {runtime::ve_addr};
        return {};
    }

    bool valid() const { return ptr != 0; }

    bool read_view_matrix(Matrix4& out) const {
        if (!ptr) return false;
        return mem::vm_read(mem::g_proc,
            (LPCVOID)(ptr + offsets::VisualEngine::ViewMatrix),
            out, sizeof(Matrix4), nullptr) != 0;
    }

    Vec2 dimensions() const {
        return rpm<Vec2>(ptr + offsets::VisualEngine::Dimensions);
    }

    uintptr_t render_view() const {
        return rpm<uintptr_t>(ptr + offsets::VisualEngine::RenderView);
    }
};

struct RbxDataModel {
    uintptr_t ptr = 0;

    static RbxDataModel get() {

        if (offsets::VisualEngine::Pointer && offsets::VisualEngine::FakeDataModel &&
            offsets::FakeDataModel::RealDataModel) {
            uintptr_t ve = rpm<uintptr_t>(mem::g_base + offsets::VisualEngine::Pointer);
            if (ve) {
                uintptr_t fake = rpm<uintptr_t>(ve + offsets::VisualEngine::FakeDataModel);
                if (fake) {
                    uintptr_t real = rpm<uintptr_t>(fake + offsets::FakeDataModel::RealDataModel);
                    if (real) return {real};
                }
            }
        }

        if (offsets::FakeDataModel::Pointer && offsets::FakeDataModel::RealDataModel) {
            uintptr_t fake2 = rpm<uintptr_t>(mem::g_base + offsets::FakeDataModel::Pointer);
            if (fake2) {
                uintptr_t r = rpm<uintptr_t>(fake2 + offsets::FakeDataModel::RealDataModel);
                if (r) return {r};
            }
        }

        if (runtime::ve_addr && offsets::VisualEngine::FakeDataModel &&
            offsets::FakeDataModel::RealDataModel) {
            uintptr_t fake = rpm<uintptr_t>(runtime::ve_addr + offsets::VisualEngine::FakeDataModel);
            if (fake) {
                uintptr_t r = rpm<uintptr_t>(fake + offsets::FakeDataModel::RealDataModel);
                if (r) return {r};
            }
        }

        if (runtime::dm_addr) return {runtime::dm_addr};
        return {};
    }

    bool valid() const { return ptr != 0; }
    RbxInstance as_instance() const { return RbxInstance{ptr}; }

    RbxInstance get_service(const std::string& cls) const {
        return as_instance().find_child_by_class(cls);
    }
};

enum Joint : int {
    J_Head = 0, J_UpperTorso, J_LowerTorso, J_HRP,
    J_LUArm, J_LLArm, J_LHand,
    J_RUArm, J_RLArm, J_RHand,
    J_LULeg, J_LLLeg, J_LFoot,
    J_RULeg, J_RLLeg, J_RFoot,
    J_COUNT
};

struct PlayerInfo {
    std::string name;
    std::string weapon;
    Vec3        position;
    Vec3        velocity;
    Vec3        head_pos;
    float       health     = 0.f;
    float       max_health = 100.f;
    bool        valid      = false;
    uintptr_t   player_ptr = 0;
    uintptr_t   cached_char_ptr = 0;
    uintptr_t   hrp_ptr    = 0;
    uintptr_t   head_ptr   = 0;
    uintptr_t   humanoid_ptr = 0;
    uintptr_t   team_ptr   = 0;
    long long   user_id    = 0;
    int         account_age = 0;
    int         role       = 0;

    uintptr_t   joint_prim[J_COUNT] = {};
    Vec3        joint_pos[J_COUNT]  = {};
    uint32_t    joint_valid_mask    = 0;

    Vec3        char_look = {0.f, 0.f, 1.f};

    float       prev_health     = 0.f;
    double      last_hit_time   = 0.0;
    double      last_alert_time = 0.0;
    bool        seen_dead       = false;
};

inline std::vector<PlayerInfo> collect_player_instances(const RbxDataModel& dm,
                                                        const std::string&  local_name = {}) {
    std::vector<PlayerInfo> out;

    RbxInstance players_svc = dm.get_service("Players");
    if (!players_svc.valid()) return out;

    uintptr_t local_ptr = offsets::Players::LocalPlayer
        ? rpm<uintptr_t>(players_svc.ptr + offsets::Players::LocalPlayer) : 0;

    for (auto& player : players_svc.get_children()) {
        if (player.get_class() != "Player") continue;
        if (local_ptr && player.ptr == local_ptr) continue;

        std::string name = player.get_name();
        if (!local_name.empty() && name == local_name) continue;

        PlayerInfo info;
        info.name = name;
        info.player_ptr = player.ptr;
        out.push_back(std::move(info));
    }
    return out;
}
