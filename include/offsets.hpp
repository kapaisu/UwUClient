#pragma once
#include <cstdint>
#include <string>
#include <fstream>
#include <sstream>

namespace runtime {
    inline uintptr_t ve_addr = 0;
    inline uintptr_t dm_addr = 0;
}

namespace offsets {
    inline std::string roblox_version = "unknown";

    namespace FakeDataModel {
        inline uintptr_t Pointer       = 0;
        inline uintptr_t RealDataModel = 0;
    }

    namespace DataModel {

        inline uintptr_t PlaceId = 0x190;
    }

    namespace RenderView {
        inline uintptr_t LightingValid = 0x220;
        inline uintptr_t SkyboxValid   = 0x28D;
    }

    namespace VisualEngine {
        inline uintptr_t Dimensions    = 0;
        inline uintptr_t FakeDataModel = 0xA90;
        inline uintptr_t Pointer       = 0;
        inline uintptr_t RenderView    = 0xBB8;
        inline uintptr_t ViewMatrix    = 0;
    }

    namespace Instance {
        inline uintptr_t ClassDescriptor = 0x18;
        inline uintptr_t Children        = 0;

        inline uintptr_t NameContainer   = 0;
        inline uintptr_t Name            = 0;
        inline uintptr_t Parent          = 0;
    }

    namespace Player {
        inline uintptr_t Character = 0x2A0;
        inline uintptr_t Team      = 0;
        inline uintptr_t MinZoomDistance = 0x36C;
        inline uintptr_t MaxZoomDistance = 0x368;
        inline uintptr_t CameraMode      = 0x370;
        inline uintptr_t UserId          = 0x300;
        inline uintptr_t AccountAge      = 0x35C;
    }

    namespace Players {
        inline uintptr_t LocalPlayer = 0;
    }

    namespace ClassDescriptor {
        inline constexpr uintptr_t Name           = 0x08;
    }

    namespace BasePart {
        inline uintptr_t Primitive = 0;
    }

    namespace Primitive {
        inline uintptr_t CFrame = 0;
        inline uintptr_t Position = 0;
        inline uintptr_t Size     = 0;
        inline uintptr_t AssemblyLinearVelocity = 0;
        inline uintptr_t AssemblyAngularVelocity = 0;
        inline uintptr_t PrimitiveFlags = 0;
    }

    namespace PrimitiveFlags {
        inline uint8_t Anchored   = 0x2;
        inline uint8_t CanCollide = 0x8;
    }

    namespace Humanoid {
        inline uintptr_t Health         = 0x188;
        inline uintptr_t MaxHealth      = 0x1A8;
        inline uintptr_t WalkSpeed       = 0x1D0;
        inline uintptr_t WalkSpeedCheck  = 0x3BC;
        inline uintptr_t JumpPower       = 0x1A4;
        inline uintptr_t PlatformStand   = 0x1DC;
        inline uintptr_t FloorMaterial   = 0x184;
        inline uintptr_t Jump            = 0x1DA;
        inline uintptr_t HipHeight       = 0x194;
        inline uintptr_t JumpHeight      = 0x1A0;
        inline uintptr_t MaxSlopeAngle   = 0x1AC;
    }

    namespace Workspace {
        inline uintptr_t CurrentCamera = 0x488;
        inline uintptr_t World         = 0x3E0;
    }

    namespace World {
        inline uintptr_t Gravity          = 0x210;
        inline uintptr_t WorldStepsPerSec = 0x680;
    }

    namespace Lighting {
        inline uintptr_t ClockTime      = 0x1A8;
        inline uintptr_t Brightness     = 0x110;
        inline uintptr_t FogEnd         = 0x124;
        inline uintptr_t OutdoorAmbient = 0xF8;
    }

    namespace Camera {
        inline uintptr_t CFrame        = 0xD8;
        inline uintptr_t FieldOfView   = 0x140;
        inline uintptr_t CameraType    = 0x138;
        inline uintptr_t CameraSubject = 0xC8;


        inline uintptr_t Viewport      = 0x28C;
    }

    namespace ValueBase {

        inline uintptr_t Value = 0xB8;
    }

    namespace MouseService {

        inline uintptr_t InputObject   = 0xF0;
        inline uintptr_t MousePosition = 0xD4;
    }

    namespace GuiService {


        inline uintptr_t GuiInset = 0x330;
    }

    namespace ChatInputBarConfiguration {

        inline uintptr_t IsOpen = 0x156;
    }

    namespace GuiObject {
        inline uintptr_t Size = 0x530;
    }

    inline uintptr_t parse_json_value(const std::string& content, const std::string& block, const std::string& key) {
        size_t block_pos = content.find("\"" + block + "\": {");
        if (block_pos == std::string::npos) return 0;

        size_t pos = content.find("\"" + key + "\"", block_pos);
        if (pos == std::string::npos) return 0;

        size_t end_block = content.find("}", block_pos);
        if (end_block != std::string::npos && pos > end_block) return 0;

        pos = content.find(":", pos);
        if (pos == std::string::npos) return 0;
        pos++;
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t' || content[pos] == '\r' || content[pos] == '\n' || content[pos] == '\"')) {
            pos++;
        }
        size_t end_pos = pos;
        while (end_pos < content.size() && content[end_pos] != ',' && content[end_pos] != '}' && content[end_pos] != '\"' && content[end_pos] != '\r' && content[end_pos] != '\n') {
            end_pos++;
        }
        std::string val_str = content.substr(pos, end_pos - pos);
        if (val_str == "null" || val_str.empty()) return 0;
        try {
            return std::stoull(val_str, nullptr, 0);
        } catch (...) {
            return 0;
        }
    }

    inline std::string parse_json_string(const std::string& content, const std::string& key) {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = content.find(":", pos);
        if (pos == std::string::npos) return "";
        pos = content.find("\"", pos);
        if (pos == std::string::npos) return "";
        pos++;
        size_t end_pos = content.find("\"", pos);
        if (end_pos == std::string::npos) return "";
        return content.substr(pos, end_pos - pos);
    }

    inline bool load_txt_stream(std::istream& f) {
        std::string line;
        auto ltrim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
                s.erase(0, 1);
        };
        auto rtrim = [](std::string& s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
                s.pop_back();
        };
        auto put = [](uintptr_t& dst, uintptr_t v) { if (v) dst = v; };

        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            rtrim(key); ltrim(val); rtrim(val);
            if (key.empty() || val.empty()) continue;

            uintptr_t v = 0;
            try { v = std::stoull(val, nullptr, 0); } catch (...) { continue; }

            if (key == "ClientVersion") { roblox_version = val; continue; }

            if (key == "VisualEngine::Pointer")            put(VisualEngine::Pointer, v);
            else if (key == "VisualEngine::Dimensions")    put(VisualEngine::Dimensions, v);
            else if (key == "VisualEngine::ViewMatrix")    put(VisualEngine::ViewMatrix, v);
            else if (key == "VisualEngine::FakeDataModel") put(VisualEngine::FakeDataModel, v);
            else if (key == "VisualEngine::RenderView")    put(VisualEngine::RenderView, v);
            else if (key == "FakeDataModel::Pointer")      put(FakeDataModel::Pointer, v);
            else if (key == "FakeDataModel::RealDataModel")put(FakeDataModel::RealDataModel, v);

            else if (key == "Instance::Name")            put(Instance::Name, v);
            else if (key == "Instance::NameContainer")   put(Instance::NameContainer, v);
            else if (key == "Instance::Parent")          put(Instance::Parent, v);
            else if (key == "Instance::ChildrenStart" ||
                     key == "Instance::Children")        put(Instance::Children, v);
            else if (key == "Instance::ClassDescriptor") put(Instance::ClassDescriptor, v);

            else if (key == "Player::ModelInstance" ||
                     key == "Player::Character")       put(Player::Character, v);
            else if (key == "Player::LocalPlayer" ||
                     key == "Players::LocalPlayer")    put(Players::LocalPlayer, v);
            else if (key == "Player::Team")            put(Player::Team, v);
            else if (key == "Player::UserId")          put(Player::UserId, v);
            else if (key == "Player::AccountAge")      put(Player::AccountAge, v);
            else if (key == "Player::MinZoomDistance") put(Player::MinZoomDistance, v);
            else if (key == "Player::MaxZoomDistance") put(Player::MaxZoomDistance, v);
            else if (key == "Player::CameraMode")      put(Player::CameraMode, v);

            else if (key == "BasePart::Primitive")               put(BasePart::Primitive, v);
            else if (key == "Primitive::Position")               put(Primitive::Position, v);
            else if (key == "Primitive::CFrame" ||
                     key == "Primitive::Rotation")               put(Primitive::CFrame, v);
            else if (key == "Primitive::Size")                   put(Primitive::Size, v);
            else if (key == "Primitive::AssemblyLinearVelocity") put(Primitive::AssemblyLinearVelocity, v);
            else if (key == "Primitive::AssemblyAngularVelocity")put(Primitive::AssemblyAngularVelocity, v);
            else if (key == "Primitive::PrimitiveFlags" ||
                     key == "Primitive::Flags")                  put(Primitive::PrimitiveFlags, v);

            else if (key == "Humanoid::Health")         put(Humanoid::Health, v);
            else if (key == "Humanoid::MaxHealth")      put(Humanoid::MaxHealth, v);
            else if (key == "Humanoid::WalkSpeed" ||
                     key == "Humanoid::Walkspeed")      put(Humanoid::WalkSpeed, v);
            else if (key == "Humanoid::WalkSpeedCheck" ||
                     key == "Humanoid::WalkspeedCheck") put(Humanoid::WalkSpeedCheck, v);
            else if (key == "Humanoid::JumpPower")      put(Humanoid::JumpPower, v);
            else if (key == "Humanoid::JumpHeight")     put(Humanoid::JumpHeight, v);
            else if (key == "Humanoid::HipHeight")      put(Humanoid::HipHeight, v);
            else if (key == "Humanoid::MaxSlopeAngle")  put(Humanoid::MaxSlopeAngle, v);
            else if (key == "Humanoid::PlatformStand")  put(Humanoid::PlatformStand, v);
            else if (key == "Humanoid::FloorMaterial")  put(Humanoid::FloorMaterial, v);
            else if (key == "Humanoid::Jump")           put(Humanoid::Jump, v);

            else if (key == "DataModel::PlaceId")       put(DataModel::PlaceId, v);

            else if (key == "Workspace::CurrentCamera") put(Workspace::CurrentCamera, v);
            else if (key == "Workspace::World")         put(Workspace::World, v);
            else if (key == "World::Gravity")           put(World::Gravity, v);

            else if (key == "Lighting::ClockTime")      put(Lighting::ClockTime, v);
            else if (key == "Lighting::Brightness")     put(Lighting::Brightness, v);
            else if (key == "Lighting::FogEnd")         put(Lighting::FogEnd, v);
            else if (key == "Lighting::OutdoorAmbient") put(Lighting::OutdoorAmbient, v);

            else if (key == "Camera::CFrame" ||
                     key == "Camera::Rotation")        put(Camera::CFrame, v);
            else if (key == "Camera::FieldOfView")     put(Camera::FieldOfView, v);
            else if (key == "Camera::CameraType")      put(Camera::CameraType, v);
            else if (key == "Camera::CameraSubject")   put(Camera::CameraSubject, v);
            else if (key == "Camera::Viewport" ||
                     key == "Camera::ViewportSize")     put(Camera::Viewport, v);

            else if (key == "MouseService::InputObject")   put(MouseService::InputObject, v);
            else if (key == "MouseService::MousePosition") put(MouseService::MousePosition, v);

            else if (key == "GuiService::GuiInset" ||
                     key == "GuiService::TopbarInset")     put(GuiService::GuiInset, v);

            else if (key == "World::worldStepsPerSec" ||
                     key == "World::WorldStepsPerSec")      put(World::WorldStepsPerSec, v);

            else if (key == "ValueBase::Value" ||
                     key == "NumberValue::Value" ||
                     key == "IntValue::Value" ||
                     key == "BoolValue::Value" ||
                     key == "Misc::Value")        put(ValueBase::Value, v);

            else if (key == "RenderView::LightingValid") put(RenderView::LightingValid, v);
            else if (key == "RenderView::SkyValid" ||
                     key == "RenderView::SkyboxValid")  put(RenderView::SkyboxValid, v);
        }

        return VisualEngine::Pointer != 0 && Instance::Children != 0;
    }

    inline bool load_txt_content(const std::string& content) {
        std::istringstream ss(content);
        return load_txt_stream(ss);
    }

    inline bool load_txt(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        return load_txt_stream(f);
    }

    inline bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        std::string content = ss.str();

        roblox_version = parse_json_string(content, "roblox_version");

        FakeDataModel::Pointer = parse_json_value(content, "FakeDataModel", "Pointer");
        FakeDataModel::RealDataModel = parse_json_value(content, "FakeDataModel", "RealDataModel");

        VisualEngine::Pointer = parse_json_value(content, "VisualEngine", "Pointer");
        VisualEngine::Dimensions = parse_json_value(content, "VisualEngine", "Dimensions");
        VisualEngine::ViewMatrix = parse_json_value(content, "VisualEngine", "ViewMatrix");
        VisualEngine::RenderView = parse_json_value(content, "VisualEngine", "RenderView");
        if (auto v = parse_json_value(content, "VisualEngine", "FakeDataModel")) VisualEngine::FakeDataModel = v;

        Instance::ClassDescriptor = parse_json_value(content, "Instance", "ClassDescriptor");
        Instance::Children = parse_json_value(content, "Instance", "ChildrenStart");
        if (!Instance::Children) Instance::Children = parse_json_value(content, "Instance", "Children");
        Instance::Name = parse_json_value(content, "Instance", "Name");
        Instance::Parent = parse_json_value(content, "Instance", "Parent");

        Player::Character = parse_json_value(content, "Player", "Character");
        if (auto v = parse_json_value(content, "Player", "Team")) Player::Team = v;
        Players::LocalPlayer = parse_json_value(content, "Players", "LocalPlayer");
        BasePart::Primitive = parse_json_value(content, "BasePart", "Primitive");
        Primitive::CFrame = parse_json_value(content, "Primitive", "CFrame");
        Primitive::Position = parse_json_value(content, "Primitive", "Position");
        Primitive::AssemblyLinearVelocity = parse_json_value(content, "Primitive", "AssemblyLinearVelocity");
        Primitive::AssemblyAngularVelocity = parse_json_value(content, "Primitive", "AssemblyAngularVelocity");
        Primitive::PrimitiveFlags = parse_json_value(content, "Primitive", "PrimitiveFlags");
        if (auto v = parse_json_value(content, "PrimitiveFlags", "CanCollide")) PrimitiveFlags::CanCollide = (uint8_t)v;
        if (auto v = parse_json_value(content, "PrimitiveFlags", "Anchored")) PrimitiveFlags::Anchored = (uint8_t)v;

        if (auto v = parse_json_value(content, "Humanoid", "Health"))         Humanoid::Health = v;
        if (auto v = parse_json_value(content, "Humanoid", "MaxHealth"))      Humanoid::MaxHealth = v;
        if (auto v = parse_json_value(content, "Humanoid", "WalkSpeed"))      Humanoid::WalkSpeed = v;
        if (auto v = parse_json_value(content, "Humanoid", "WalkSpeedCheck")) Humanoid::WalkSpeedCheck = v;
        if (auto v = parse_json_value(content, "Humanoid", "JumpPower"))      Humanoid::JumpPower = v;

        Workspace::CurrentCamera = parse_json_value(content, "Workspace", "CurrentCamera");
        if (auto v = parse_json_value(content, "Workspace", "World"))         Workspace::World = v;
        if (auto v = parse_json_value(content, "World", "Gravity"))           World::Gravity = v;
        if (auto v = parse_json_value(content, "Lighting", "ClockTime"))      Lighting::ClockTime = v;
        if (auto v = parse_json_value(content, "Lighting", "Brightness"))     Lighting::Brightness = v;
        if (auto v = parse_json_value(content, "Lighting", "FogEnd"))         Lighting::FogEnd = v;
        if (auto v = parse_json_value(content, "Lighting", "OutdoorAmbient")) Lighting::OutdoorAmbient = v;
        if (auto v = parse_json_value(content, "Camera", "CFrame"))           Camera::CFrame = v;
        if (auto v = parse_json_value(content, "Camera", "FieldOfView"))      Camera::FieldOfView = v;
        if (auto v = parse_json_value(content, "Camera", "Viewport"))         Camera::Viewport = v;
        else if (auto v = parse_json_value(content, "Camera", "ViewportSize")) Camera::Viewport = v;
        if (auto v = parse_json_value(content, "Misc",   "Value"))            ValueBase::Value = v;
        if (auto v = parse_json_value(content, "ValueBase", "Value"))         ValueBase::Value = v;
        if (auto v = parse_json_value(content, "DataModel", "PlaceId"))       DataModel::PlaceId = v;
        if (auto v = parse_json_value(content, "MouseService", "InputObject"))   MouseService::InputObject = v;
        if (auto v = parse_json_value(content, "MouseService", "MousePosition")) MouseService::MousePosition = v;
        if (auto v = parse_json_value(content, "GuiService",  "GuiInset"))       GuiService::GuiInset = v;
        else if (auto v = parse_json_value(content, "GuiService", "TopbarInset")) GuiService::GuiInset = v;
        if (auto v = parse_json_value(content, "World", "worldStepsPerSec"))     World::WorldStepsPerSec = v;

        return FakeDataModel::Pointer != 0 && VisualEngine::Pointer != 0 && Instance::Children != 0;
    }
}
