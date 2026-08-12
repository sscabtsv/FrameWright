#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

struct FwInput {
    uint64_t frame = 0;
    uint8_t button = 0;
    bool player2 = false;
    bool down = false;
};

struct FwFrameData {
    float posX = 0.f;
    float posY = 0.f;
    float rotation = 0.f;
    bool rotate = true;
    double yVelocity = 0.0;
    double xVelocity = 0.0;
};

struct FwFrameFix {
    uint64_t frame = 0;
    FwFrameData p1;
    FwFrameData p2;
};

struct FwTpsChange {
    uint64_t frame = 0;
    float tps = 240.f;
};

struct FwReplay {
    float framerate = 240.f;
    bool hasSeed = false;
    uint64_t seed = 0;
    std::string botName;
    uint32_t botVersion = 0;
    std::string author;
    std::string description;
    float duration = 0.f;
    uint32_t levelId = 0;
    std::string levelName;
    std::vector<FwInput> inputs;
    std::vector<FwFrameFix> frameFixes;
    std::vector<FwTpsChange> tpsChanges;
};

constexpr uint8_t FW_MAGIC[4] = {0x46, 0x57, 0x4D, 0x31};

std::vector<uint8_t> fwEncode(FwReplay const& replay);

std::optional<FwReplay> fwDecode(std::vector<uint8_t> const& data, std::string* error = nullptr);
