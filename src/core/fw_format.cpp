#include "fw_format.hpp"

#include <cstring>

namespace {

struct Writer {
    std::vector<uint8_t> data;

    void u8(uint8_t v) { data.push_back(v); }

    void u32(uint32_t v) {
        for (int i = 0; i < 4; i++) data.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }

    void u64(uint64_t v) {
        for (int i = 0; i < 8; i++) data.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }

    void f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }

    void f64(double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        u64(bits);
    }

    // LEB128 unsigned varint
    void varint(uint64_t v) {
        while (true) {
            uint8_t byte = v & 0x7F;
            v >>= 7;
            if (v != 0) {
                data.push_back(byte | 0x80);
            } else {
                data.push_back(byte);
                break;
            }
        }
    }

    void str8(std::string const& s) {
        uint8_t len = static_cast<uint8_t>(s.size() > 255 ? 255 : s.size());
        u8(len);
        for (uint8_t i = 0; i < len; i++) data.push_back(static_cast<uint8_t>(s[i]));
    }

    void str16(std::string const& s) {
        uint16_t len = static_cast<uint16_t>(s.size() > 65535 ? 65535 : s.size());
        data.push_back(static_cast<uint8_t>(len & 0xFF));
        data.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        for (uint16_t i = 0; i < len; i++) data.push_back(static_cast<uint8_t>(s[i]));
    }
};

struct Reader {
    std::vector<uint8_t> const& data;
    size_t pos = 0;
    bool bad = false;

    bool hasBytes(size_t n) const { return !bad && pos + n <= data.size(); }

    uint8_t u8() {
        if (!hasBytes(1)) { bad = true; return 0; }
        return data[pos++];
    }

    uint32_t u32() {
        if (!hasBytes(4)) { bad = true; return 0; }
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(data[pos + i]) << (8 * i);
        pos += 4;
        return v;
    }

    uint64_t u64() {
        if (!hasBytes(8)) { bad = true; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
        pos += 8;
        return v;
    }

    float f32() {
        uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    double f64() {
        uint64_t bits = u64();
        double v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    uint64_t varint() {
        uint64_t value = 0;
        int shift = 0;
        while (true) {
            uint8_t byte = u8();
            if (bad) return 0;
            value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) return value;
            shift += 7;
            if (shift >= 64) { bad = true; return 0; }
        }
    }

    std::string str8() {
        uint8_t len = u8();
        if (bad || !hasBytes(len)) { bad = true; return {}; }
        std::string s(reinterpret_cast<char const*>(&data[pos]), len);
        pos += len;
        return s;
    }

    std::string str16() {
        if (!hasBytes(2)) { bad = true; return {}; }
        uint16_t len = static_cast<uint16_t>(data[pos]) | (static_cast<uint16_t>(data[pos + 1]) << 8);
        pos += 2;
        if (!hasBytes(len)) { bad = true; return {}; }
        std::string s(reinterpret_cast<char const*>(&data[pos]), len);
        pos += len;
        return s;
    }
};

void writeFrameData(Writer& w, FwFrameData const& fd) {
    w.f32(fd.posX);
    w.f32(fd.posY);
    w.f32(fd.rotation);
    w.f64(fd.yVelocity);
    w.f64(fd.xVelocity);
}

FwFrameData readFrameData(Reader& r) {
    FwFrameData fd;
    fd.posX = r.f32();
    fd.posY = r.f32();
    fd.rotation = r.f32();
    fd.yVelocity = r.f64();
    fd.xVelocity = r.f64();
    return fd;
}

} // namespace

std::vector<uint8_t> fwEncode(FwReplay const& replay) {
    Writer w;
    w.data.insert(w.data.end(), FW_MAGIC, FW_MAGIC + 4);

    w.f32(replay.framerate);
    w.u8(replay.hasSeed ? 1 : 0);
    w.u64(replay.seed);
    w.u32(replay.botVersion);
    w.str8(replay.botName);
    w.str16(replay.author);
    w.str16(replay.description);
    w.f32(replay.duration);
    w.u32(replay.levelId);
    w.str16(replay.levelName);

    w.varint(replay.inputs.size());
    uint64_t lastFrame = 0;
    for (auto const& in : replay.inputs) {
        uint64_t delta = in.frame >= lastFrame ? in.frame - lastFrame : 0;
        w.varint(delta);
        lastFrame = in.frame;

        uint8_t flags = static_cast<uint8_t>(in.button & 0x07);
        if (in.down) flags |= 0x08;
        if (in.player2) flags |= 0x10;
        w.u8(flags);
    }

    w.varint(replay.frameFixes.size());
    lastFrame = 0;
    for (auto const& fix : replay.frameFixes) {
        uint64_t delta = fix.frame >= lastFrame ? fix.frame - lastFrame : 0;
        w.varint(delta);
        lastFrame = fix.frame;

        uint8_t flags = 0;
        if (fix.p1.rotate) flags |= 0x01;
        if (fix.p2.rotate) flags |= 0x02;
        w.u8(flags);

        writeFrameData(w, fix.p1);
        writeFrameData(w, fix.p2);
    }

    w.varint(replay.tpsChanges.size());
    lastFrame = 0;
    for (auto const& change : replay.tpsChanges) {
        uint64_t delta = change.frame >= lastFrame ? change.frame - lastFrame : 0;
        w.varint(delta);
        lastFrame = change.frame;

        w.f32(change.tps);
    }

    return w.data;
}

std::optional<FwReplay> fwDecode(std::vector<uint8_t> const& data, std::string* error) {
    auto fail = [&](char const* msg) -> std::optional<FwReplay> {
        if (error) *error = msg;
        return std::nullopt;
    };

    if (data.size() < 4 || std::memcmp(data.data(), FW_MAGIC, 4) != 0)
        return fail("Not a valid .fw file (header mismatch)");

    Reader r{data, 4};
    FwReplay replay;

    replay.framerate = r.f32();
    replay.hasSeed = r.u8() != 0;
    replay.seed = r.u64();
    replay.botVersion = r.u32();
    replay.botName = r.str8();
    replay.author = r.str16();
    replay.description = r.str16();
    replay.duration = r.f32();
    replay.levelId = r.u32();
    replay.levelName = r.str16();

    if (r.bad) return fail(".fw file is truncated (header)");

    uint64_t inputCount = r.varint();
    if (r.bad) return fail(".fw file is truncated (input count)");
    replay.inputs.reserve(inputCount > 1'000'000 ? 0 : inputCount);

    uint64_t frame = 0;
    for (uint64_t i = 0; i < inputCount; i++) {
        uint64_t delta = r.varint();
        uint8_t flags = r.u8();
        if (r.bad) return fail(".fw file is truncated (inputs)");

        frame += delta;

        FwInput in;
        in.frame = frame;
        in.button = flags & 0x07;
        in.down = (flags & 0x08) != 0;
        in.player2 = (flags & 0x10) != 0;
        replay.inputs.push_back(in);
    }

    uint64_t frameFixCount = r.varint();
    if (r.bad) return fail(".fw file is truncated (frame fix count)");
    replay.frameFixes.reserve(frameFixCount > 1'000'000 ? 0 : frameFixCount);

    frame = 0;
    for (uint64_t i = 0; i < frameFixCount; i++) {
        uint64_t delta = r.varint();
        uint8_t flags = r.u8();
        if (r.bad) return fail(".fw file is truncated (frame fixes)");

        frame += delta;

        FwFrameFix fix;
        fix.frame = frame;
        fix.p1 = readFrameData(r);
        fix.p1.rotate = (flags & 0x01) != 0;
        fix.p2 = readFrameData(r);
        fix.p2.rotate = (flags & 0x02) != 0;

        if (r.bad) return fail(".fw file is truncated (frame fix data)");

        replay.frameFixes.push_back(fix);
    }

    uint64_t tpsChangeCount = r.varint();
    if (r.bad) return fail(".fw file is truncated (tps change count)");
    replay.tpsChanges.reserve(tpsChangeCount > 1'000'000 ? 0 : tpsChangeCount);

    frame = 0;
    for (uint64_t i = 0; i < tpsChangeCount; i++) {
        uint64_t delta = r.varint();
        float tps = r.f32();
        if (r.bad) return fail(".fw file is truncated (tps changes)");

        frame += delta;

        FwTpsChange change;
        change.frame = frame;
        change.tps = tps;
        replay.tpsChanges.push_back(change);
    }

    return replay;
}
