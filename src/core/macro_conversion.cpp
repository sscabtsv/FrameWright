#include "macro_conversion.hpp"
#include "fw_format.hpp"
#include "tcm_import.hpp"

using namespace geode::prelude;

namespace macro_conversion {
namespace {
std::string extensionForFormat(SaveFormat format) {
    switch (format) {
    case SaveFormat::GDR2:
        return ".gdr2";
    case SaveFormat::GDR1:
        return ".gdr";
    case SaveFormat::JSON:
        return ".gdr.json";
    case SaveFormat::FW:
        return ".fw";
    }

    return ".gdr2";
}

FwReplay botReplayToFw(BotReplay const& replay) {
    FwReplay fw;
    fw.framerate = replay.framerate;
    fw.hasSeed = replay.seed != 0;
    fw.seed = static_cast<uint64_t>(replay.seed);
    fw.botName = replay.botInfo.name;
    fw.botVersion = static_cast<uint32_t>(replay.botInfo.version);
    fw.author = replay.author;
    fw.description = replay.description;
    fw.duration = replay.duration;
    fw.levelId = replay.levelInfo.id;
    fw.levelName = replay.levelInfo.name;

    fw.inputs.reserve(replay.inputs.size());
    for (auto const& in : replay.inputs)
        fw.inputs.push_back({in.frame, in.button, in.player2, in.down});

    fw.frameFixes.reserve(replay.frameFixes.size());
    for (auto const& fix : replay.frameFixes) {
        FwFrameFix fwFix;
        fwFix.frame = static_cast<uint64_t>(fix.frame);
        fwFix.p1 = {fix.p1.pos.x, fix.p1.pos.y, fix.p1.rotation, fix.p1.rotate,
                    fix.p1.yVelocity, fix.p1.xVelocity};
        fwFix.p2 = {fix.p2.pos.x, fix.p2.pos.y, fix.p2.rotation, fix.p2.rotate,
                    fix.p2.yVelocity, fix.p2.xVelocity};
        fw.frameFixes.push_back(fwFix);
    }

    fw.tpsChanges.reserve(replay.tpsChanges.size());
    for (auto const& change : replay.tpsChanges)
        fw.tpsChanges.push_back({static_cast<uint64_t>(change.frame), change.tps});

    return fw;
}

BotReplay fwToBotReplay(FwReplay const& fw) {
    BotReplay replay;
    replay.framerate = fw.framerate;
    replay.seed = fw.hasSeed ? static_cast<uintptr_t>(fw.seed) : 0;
    replay.botInfo.name = fw.botName.empty() ? "xdBot" : fw.botName;
    replay.botInfo.version = static_cast<int>(fw.botVersion);
    replay.author = fw.author;
    replay.description = fw.description;
    replay.duration = fw.duration;
    replay.levelInfo.id = fw.levelId;
    replay.levelInfo.name = fw.levelName;
    replay.xdBotMacro = replay.botInfo.name == "xdBot";

    replay.inputs.reserve(fw.inputs.size());
    for (auto const& in : fw.inputs)
        replay.inputs.emplace_back(in.frame, in.button, in.player2, in.down);

    replay.frameFixes.reserve(fw.frameFixes.size());
    for (auto const& fwFix : fw.frameFixes) {
        gdr_legacy::FrameFix fix;
        fix.frame = static_cast<int>(fwFix.frame);
        fix.p1.pos = {fwFix.p1.posX, fwFix.p1.posY};
        fix.p1.rotation = fwFix.p1.rotation;
        fix.p1.rotate = fwFix.p1.rotate;
        fix.p1.yVelocity = fwFix.p1.yVelocity;
        fix.p1.xVelocity = fwFix.p1.xVelocity;
        fix.p2.pos = {fwFix.p2.posX, fwFix.p2.posY};
        fix.p2.rotation = fwFix.p2.rotation;
        fix.p2.rotate = fwFix.p2.rotate;
        fix.p2.yVelocity = fwFix.p2.yVelocity;
        fix.p2.xVelocity = fwFix.p2.xVelocity;
        replay.frameFixes.push_back(fix);
    }

    replay.tpsChanges.reserve(fw.tpsChanges.size());
    for (auto const& change : fw.tpsChanges)
        replay.tpsChanges.push_back({static_cast<int>(change.frame), change.tps});

    return replay;
}

std::filesystem::path nextAvailablePath(std::filesystem::path path) {
    if (!std::filesystem::exists(path))
        return path;

    auto stem = geode::utils::string::pathToString(path.stem());
    auto extension = geode::utils::string::pathToString(path.extension());

    if (extension == ".json" && path.stem().extension() == ".gdr") {
        stem = geode::utils::string::pathToString(path.stem().stem());
        extension = ".gdr.json";
    }

    int index = 1;
    while (true) {
        auto candidate = path.parent_path() / fmt::format("{} ({}){}", stem, index++, extension);
        if (!std::filesystem::exists(candidate))
            return candidate;
    }
}

gdr::Result<std::vector<uint8_t>> exportLegacyXDAsGdr2(BotReplay& replay) {
    return replay.exportGDR2();
}
} // namespace

LoadResult load(std::filesystem::path const& path) {
    LoadResult result;
    result.legacyXD = path.extension() == ".xd";

    if (result.legacyXD) {
        result.replay = gdr_legacy::importXD(path);
        return result;
    }

    auto readResult = geode::utils::file::readBinary(path);
    if (readResult.isErr()) {
        log::error("Failed to read macro file {}: {}", path, readResult.unwrapErr());
        return result;
    }

    auto data = readResult.unwrap();

    if (path.extension() == ".tcm") {
        auto tcmResult = tcm_import::importTCM(data);
        if (tcmResult.isErr()) {
            log::error("Failed to import TCM macro {}: {}", path, tcmResult.unwrapErr());
            return result;
        }
        result.replay = tcmResult.unwrap();
        return result;
    }

    if (path.extension() == ".fw") {
        std::string error;
        auto fwResult = fwDecode(data, &error);
        if (!fwResult) {
            log::error("Failed to load .fw macro {}: {}", path, error);
            return result;
        }
        result.replay = fwToBotReplay(*fwResult);
        return result;
    }

    result.replay = importData(data);
    return result;
}

BotReplay importData(std::vector<uint8_t>& data) {
    return gdr_legacy::importReplay(data);
}

gdr::Result<std::vector<uint8_t>> exportData(BotReplay& replay, SaveFormat format) {
    switch (format) {
    case SaveFormat::GDR2:
        return replay.exportGDR2();
    case SaveFormat::GDR1:
        return gdr::Ok<std::vector<uint8_t>>(replay.exportGDR1());
    case SaveFormat::JSON:
        return gdr::Ok<std::vector<uint8_t>>(replay.exportJSON());
    case SaveFormat::FW:
        return gdr::Ok<std::vector<uint8_t>>(fwEncode(botReplayToFw(replay)));
    }

    return gdr::Err<std::vector<uint8_t>>("Unknown save format");
}

int save(BotReplay& replay,
         std::string const& author,
         std::string const& description,
         std::filesystem::path const& path,
         SaveFormat format) {
    if (replay.inputs.empty())
        return 31;

    auto finalPath = nextAvailablePath(path.string() + extensionForFormat(format));
    log::debug("Saving macro to path: {}", finalPath);

    replay.author = author;
    replay.description = description;
    replay.duration = static_cast<float>(replay.inputs.back().frame) / replay.framerate;

    auto dataResult = exportData(replay, format);
    if (dataResult.isErr()) {
        log::error("Macro export failed: {}", dataResult.unwrapErr());
        return 23;
    }

    auto data = std::move(dataResult).unwrap();
    if (data.empty())
        return 23;

    auto writeResult = geode::utils::file::writeBinary(finalPath, data);
    if (writeResult.isErr()) {
        log::error("Failed to write file: {}", writeResult.unwrapErr());
        return 20;
    }

    return 0;
}

Result<ImportResult> importFile(std::filesystem::path const& sourcePath,
                                std::filesystem::path const& targetDirectory) {
    std::filesystem::create_directories(targetDirectory);

    auto targetPath = nextAvailablePath(targetDirectory / sourcePath.filename());
    bool legacyXD = sourcePath.extension() == ".xd";
    bool tcm = sourcePath.extension() == ".tcm";

    if (!legacyXD && !tcm) {
        std::error_code ec;
        std::filesystem::copy_file(
            sourcePath, targetPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            return Err(fmt::format("Failed to copy macro file: {}", ec.message()));

        return Ok(ImportResult{targetPath, false});
    }

    BotReplay replay;
    if (legacyXD) {
        auto loadResult = load(sourcePath);
        if (loadResult.replay.description == "fail")
            return Err("Failed to load legacy .xd macro");
        replay = loadResult.replay;
    } else {
        auto readResult = geode::utils::file::readBinary(sourcePath);
        if (readResult.isErr())
            return Err(fmt::format("Failed to read macro file: {}", readResult.unwrapErr()));

        auto tcmResult = tcm_import::importTCM(readResult.unwrap());
        if (tcmResult.isErr())
            return Err(fmt::format("Failed to import TCM macro: {}", tcmResult.unwrapErr()));
        replay = tcmResult.unwrap();
    }

    targetPath.replace_extension(".gdr2");
    targetPath = nextAvailablePath(targetPath);

    auto exportResult = exportLegacyXDAsGdr2(replay);
    if (exportResult.isErr())
        return Err(exportResult.unwrapErr());

    auto writeResult = geode::utils::file::writeBinary(targetPath, exportResult.unwrap());
    if (writeResult.isErr())
        return Err(fmt::format("Failed to write macro file: {}", writeResult.unwrapErr()));

    return Ok(ImportResult{targetPath, legacyXD});
}
} // namespace macro_conversion
