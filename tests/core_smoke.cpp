#include "AkiCore.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace {

int Fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

void PutBe16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    data[offset] = static_cast<uint8_t>(value >> 8);
    data[offset + 1] = static_cast<uint8_t>(value);
}

void PutBe32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    data[offset] = static_cast<uint8_t>(value >> 24);
    data[offset + 1] = static_cast<uint8_t>(value >> 16);
    data[offset + 2] = static_cast<uint8_t>(value >> 8);
    data[offset + 3] = static_cast<uint8_t>(value);
}

uint32_t GetBe32(const std::vector<uint8_t>& data, size_t offset) {
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) |
           data[offset + 3];
}

aki::LoadedRom MakeSyntheticLoopRom(std::string& error,
                                    uint32_t protectedObject = 0x380,
                                    size_t romSize = 0x500) {
    constexpr uint32_t control = 0x100;
    constexpr uint32_t wave = 0x300;

    aki::SoundRecord encoder;
    encoder.predictorOrder = 2;
    encoder.predictorCount = 1;
    encoder.predictorBook.assign(16, 0);
    std::vector<int16_t> pcm(32);
    for (size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = static_cast<int16_t>(static_cast<int>(i) * 700 - 9000);
    }
    std::vector<uint8_t> encoded;
    uint32_t padded = 0;
    if (!aki::EncodePcmWithOriginalBook(
            encoder, pcm, encoded, padded, error)) {
        return {};
    }

    aki::LoadedRom rom;
    rom.z64.assign(romSize, 0);
    const char magic[] = "N64 PtrTablesV2";
    std::copy(magic, magic + 15, rom.z64.begin() + control);
    PutBe32(rom.z64, control + 0x20, 1);
    PutBe32(rom.z64, control + 0x24, 0);
    PutBe32(rom.z64, control + 0x28, 0);
    PutBe32(rom.z64, control + 0x2C, 0x30);
    PutBe32(rom.z64, control + 0x30, 0x40);

    const uint32_t record = control + 0x40;
    PutBe32(rom.z64, record + 0x00, 0);
    PutBe32(rom.z64, record + 0x04,
            static_cast<uint32_t>(encoded.size()));
    PutBe32(rom.z64, record + 0x0C, 0x80);
    PutBe32(rom.z64, record + 0x10, 0xB0);

    const uint32_t loop = control + 0x80;
    PutBe32(rom.z64, loop + 0x00, 16);
    PutBe32(rom.z64, loop + 0x04, 32);
    PutBe32(rom.z64, loop + 0x08, 0xFFFFFFFFU);

    const uint32_t book = control + 0xB0;
    PutBe32(rom.z64, book + 0x00, 2);
    PutBe32(rom.z64, book + 0x04, 1);
    for (size_t i = 0; i < 16; ++i) PutBe16(rom.z64, book + 8 + i * 2, 0);
    std::copy(encoded.begin(), encoded.end(), rom.z64.begin() + wave);
    rom.z64[protectedObject] = 0x7E;

    rom.customProfile.id = aki::GameId::VirtualProWrestling2;
    rom.customProfile.gameCode = "TEST";
    rom.customProfile.displayName = "Synthetic loop bank";
    rom.customProfile.banks.push_back(
        {9, control, wave, protectedObject, "Synthetic"});
    rom.profile = &rom.customProfile;
    if (!aki::ParseAkiBanks(rom, nullptr, error)) return {};
    return rom;
}

} // namespace

int main(int argc, char** argv) {
    if (aki::Hex4(0x5f) != "005F") return Fail("Hex4 formatting");
    if (aki::Hex8(0x12b34e0) != "012B34E0") return Fail("Hex8 formatting");
    if (aki::RateConfidenceText(aki::RateConfidence::RomDerived) != "ROM-derived") {
        return Fail("confidence text");
    }

    // Import resampling must preserve duration closely and move the two loop
    // points onto the target-rate sample timeline.
    aki::WavPcm16 sourceWav;
    sourceWav.sampleRate = 44100;
    sourceWav.sourceChannels = 1;
    sourceWav.monoSamples.resize(4410);
    for (size_t i = 0; i < sourceWav.monoSamples.size(); ++i) {
        sourceWav.monoSamples[i] = static_cast<int16_t>(
            std::llround(12000.0 * std::sin(2.0 * 3.14159265358979323846 *
                                           440.0 * static_cast<double>(i) / 44100.0)));
    }
    sourceWav.loopMetadataPresent = true;
    sourceWav.hasLoop = true;
    sourceWav.loopStart = 882;
    sourceWav.loopEnd = 3528;
    aki::WavPcm16 resampledWav;
    std::string resampleError;
    if (!aki::ResampleWavPcm16(sourceWav, 22050, resampledWav, resampleError)) {
        return Fail("automatic WAV resampling failed: " + resampleError);
    }
    if (resampledWav.sampleRate != 22050 ||
        resampledWav.monoSamples.size() != 2205 ||
        resampledWav.loopStart != 441 ||
        resampledWav.loopEnd != 1764 ||
        !resampledWav.hasLoop) {
        return Fail("automatic WAV resampling did not scale samples/loop points");
    }

    // Import gain must alter PCM amplitude without moving the two loop points.
    aki::GainResult gainResult;
    std::string gainError;
    const uint32_t gainLoopStart = resampledWav.loopStart;
    const uint32_t gainLoopEnd = resampledWav.loopEnd;
    if (!aki::ApplyWavGain(resampledWav, 6.0, true, gainResult, gainError)) {
        return Fail("automatic WAV gain failed: " + gainError);
    }
    if (resampledWav.loopStart != gainLoopStart ||
        resampledWav.loopEnd != gainLoopEnd ||
        gainResult.appliedDb <= 0.0 ||
        gainResult.peakAfter <= gainResult.peakBefore ||
        gainResult.clippedSamples != 0) {
        return Fail("WAV gain changed loop points or failed clipping-safe amplification");
    }

    // v0.7.2 real-world loop-marker regression fixture. The WAV stores
    // a forward loop as two RIFF smpl points. The smpl end is inclusive on
    // disk and must become an exclusive end internally.
    const auto realLoopFixture =
        std::filesystem::path(AKI_TEST_FIXTURE_DIR) / "austin.wav";
    if (!std::filesystem::is_regular_file(realLoopFixture)) {
        return Fail(
            "required regression fixture is missing: " + realLoopFixture.string() +
            "\nThis is a test configuration error, not a WAV importer failure.");
    }
    aki::WavPcm16 realLoopWav;
    std::string realLoopError;
    if (!aki::ReadPcm16Wav(realLoopFixture, realLoopWav, realLoopError)) {
        return Fail("real loop fixture import failed: " + realLoopError);
    }
    if (realLoopWav.sampleRate != 7000 ||
        realLoopWav.sourceChannels != 1 ||
        realLoopWav.monoSamples.size() != 188179 ||
        !realLoopWav.loopMetadataPresent || !realLoopWav.hasLoop ||
        realLoopWav.loopStart != 12544 ||
        realLoopWav.loopEnd != 133889 ||
        realLoopWav.loopCount != 0xFFFFFFFFU) {
        return Fail("real loop fixture metadata was not read exactly");
    }

    aki::SoundRecord previewSound;
    previewSound.loopControlOffset = 1;
    previewSound.loopStart = realLoopWav.loopStart;
    previewSound.loopEnd = realLoopWav.loopEnd;
    const auto previewPlan = aki::ResolveLoopPreviewPlan(
        previewSound, realLoopWav.monoSamples.size());
    if (!previewPlan.hasLoop ||
        previewPlan.introEnd != 12544 ||
        previewPlan.loopStart != 12544 ||
        previewPlan.loopEnd != 133889 ||
        previewPlan.loopEnd - previewPlan.loopStart != 121345) {
        return Fail("loop preview plan did not use the two fixture markers");
    }

    aki::WavPcm16 doubledLoopWav;
    if (!aki::ResampleWavPcm16(
            realLoopWav, 14000, doubledLoopWav, realLoopError)) {
        return Fail("real loop fixture resampling failed: " + realLoopError);
    }
    if (doubledLoopWav.monoSamples.size() != 376358 ||
        doubledLoopWav.loopStart != 25088 ||
        doubledLoopWav.loopEnd != 267778 ||
        !doubledLoopWav.hasLoop) {
        return Fail("real loop fixture markers did not scale with resampling");
    }

    aki::LoadedRom realLoopRom = MakeSyntheticLoopRom(
        realLoopError, 0x30000, 0x30010);
    if (!realLoopError.empty() || realLoopRom.sounds.size() != 1) {
        return Fail("large synthetic loop bank parse failed: " + realLoopError);
    }
    aki::ReplacementResult realLoopResult;
    if (!aki::ReplaceSoundPcm(realLoopRom,
                              realLoopRom.sounds[0],
                              realLoopWav,
                              realLoopResult,
                              realLoopError)) {
        return Fail("real loop fixture injection failed: " + realLoopError);
    }
    const auto& injectedLoopSound = realLoopRom.sounds[0];
    if (!realLoopResult.loopEnabled ||
        !realLoopResult.loopImportedFromWav ||
        !realLoopResult.loopStateRebuilt ||
        injectedLoopSound.loopStart != 12544 ||
        injectedLoopSound.loopEnd != 133889 ||
        injectedLoopSound.loopControlOffset == 0 ||
        GetBe32(realLoopRom.z64, injectedLoopSound.loopControlOffset) != 12544 ||
        GetBe32(realLoopRom.z64, injectedLoopSound.loopControlOffset + 4) != 133889) {
        return Fail("real loop fixture injection did not rebuild exact loop metadata");
    }

    const auto realLoopExport =
        std::filesystem::temp_directory_path() / "aki_real_loop_roundtrip.wav";
    if (!aki::WriteMonoPcm16Wav(realLoopExport,
                                realLoopWav.monoSamples,
                                realLoopWav.sampleRate,
                                realLoopWav.loopStart,
                                realLoopWav.loopEnd,
                                realLoopWav.loopCount,
                                realLoopError)) {
        return Fail("real loop fixture export failed: " + realLoopError);
    }
    aki::WavPcm16 roundTrippedLoopWav;
    if (!aki::ReadPcm16Wav(realLoopExport,
                           roundTrippedLoopWav,
                           realLoopError)) {
        return Fail("real loop fixture re-import failed: " + realLoopError);
    }
    std::error_code realLoopRemoveError;
    std::filesystem::remove(realLoopExport, realLoopRemoveError);
    if (!roundTrippedLoopWav.hasLoop ||
        roundTrippedLoopWav.loopStart != 12544 ||
        roundTrippedLoopWav.loopEnd != 133889 ||
        roundTrippedLoopWav.loopCount != 0xFFFFFFFFU) {
        return Fail("real loop fixture export changed the two loop markers");
    }

    // v0.5.2 regression guard: LoadedRom owns customProfile while profile points
    // at it. Copying or moving a LoadedRom must rebind that pointer to the
    // destination object. The v0.5/v0.5.1 Win32 loader moved a freshly loaded
    // ROM into global app state, leaving profile aimed at the moved-from
    // temporary. Its bank vector was empty, so parsing returned zero sounds.
    aki::LoadedRom bindingSource;
    bindingSource.customProfile = aki::VirtualProWrestling2Profile();
    bindingSource.profile = &bindingSource.customProfile;

    aki::LoadedRom bindingCopy = bindingSource;
    if (bindingCopy.profile != &bindingCopy.customProfile ||
        bindingCopy.profile->banks.size() !=
            aki::VirtualProWrestling2Profile().banks.size()) {
        return Fail("LoadedRom copy did not rebind custom profile");
    }

    aki::LoadedRom bindingMove;
    bindingMove = std::move(bindingSource);
    if (bindingMove.profile != &bindingMove.customProfile ||
        bindingMove.profile->banks.size() !=
            aki::VirtualProWrestling2Profile().banks.size()) {
        return Fail("LoadedRom move did not rebind custom profile");
    }

    if (aki::RevengeReduxProfile().banks.size() != 2 ||
        aki::RevengeReduxProfile().banks[0].controlOffset != 0x02D62CEC ||
        aki::RevengeReduxProfile().banks[0].waveOffset != 0x02D66BBC ||
        aki::RevengeReduxProfile().banks[1].controlOffset != 0x03D9715C ||
        aki::RevengeReduxProfile().banks[1].waveOffset != 0x03D9D6EC) {
        return Fail("Revenge Redux built-in bank map changed unexpectedly");
    }

    aki::LoadedRom emptyProfileRom;
    emptyProfileRom.customProfile.id = aki::GameId::VirtualProWrestling2;
    emptyProfileRom.profile = &emptyProfileRom.customProfile;
    std::string emptyProfileError;
    if (aki::ParseAkiBanks(emptyProfileRom, nullptr, emptyProfileError) ||
        emptyProfileError.find("no sound banks") == std::string::npos) {
        return Fail("empty profile did not fail loudly");
    }

    // v0.5.4 loop regression: a markerless replacement must not inherit the
    // old song's numeric loop positions. It disables looping while still using
    // only contiguous blank TBL padding for a modest oversized replacement.
    std::string loopError;
    aki::LoadedRom loopRom = MakeSyntheticLoopRom(loopError);
    if (!loopError.empty() || loopRom.sounds.size() != 1) {
        return Fail("synthetic loop bank parse failed: " + loopError);
    }
    aki::WavPcm16 longer;
    longer.sampleRate = 22050;
    longer.sourceChannels = 1;
    longer.monoSamples.resize(48);
    for (size_t i = 0; i < longer.monoSamples.size(); ++i) {
        longer.monoSamples[i] =
            static_cast<int16_t>(static_cast<int>(i) * 500 - 10000);
    }
    aki::ReplacementResult loopResult;
    if (!aki::ReplaceSoundPcm(
            loopRom, loopRom.sounds[0], longer,
            loopResult, loopError)) {
        return Fail("markerless replacement failed: " + loopError);
    }
    const auto& clearedLoop = loopRom.sounds[0];
    if (loopResult.loopEnabled || loopResult.loopStateRebuilt ||
        loopResult.loopImportedFromWav ||
        clearedLoop.loopControlOffset != 0 ||
        clearedLoop.loopStart != 0 || clearedLoop.loopEnd != 0 ||
        GetBe32(loopRom.z64, clearedLoop.controlRecordOffset + 0x0C) != 0 ||
        loopResult.allowedTblCapacityBytes <=
            loopResult.normalTblCapacityBytes) {
        return Fail("markerless WAV inherited or retained stale loop metadata");
    }
    if (loopRom.z64[0x380] != 0x7E) {
        return Fail("safe TBL padding overwrote the protected object");
    }

    // two-point loop round-trip plus allocation of a new
    // ALADPCMloop block for a previously non-looped target.
    const auto loopWavPath =
        std::filesystem::temp_directory_path() /
        "aki_sound_studio_loop_smoke.wav";
    if (!aki::WriteMonoPcm16Wav(
            loopWavPath, longer.monoSamples, 22050,
            8, 40, 0xFFFFFFFFU, loopError)) {
        return Fail("looped WAV export failed: " + loopError);
    }
    aki::WavPcm16 importedLoopWav;
    if (!aki::ReadPcm16Wav(loopWavPath, importedLoopWav, loopError)) {
        return Fail("looped WAV import failed: " + loopError);
    }
    std::error_code removeError;
    std::filesystem::remove(loopWavPath, removeError);
    if (!importedLoopWav.loopMetadataPresent || !importedLoopWav.hasLoop ||
        importedLoopWav.loopStart != 8 || importedLoopWav.loopEnd != 40 ||
        importedLoopWav.loopCount != 0xFFFFFFFFU) {
        return Fail("two-point WAV loop did not round-trip");
    }

    aki::LoadedRom newLoopRom = MakeSyntheticLoopRom(loopError);
    if (!loopError.empty()) return Fail(loopError);
    PutBe32(newLoopRom.z64,
            newLoopRom.sounds[0].controlRecordOffset + 0x0C, 0);
    if (!aki::ParseAkiBanks(newLoopRom, nullptr, loopError)) {
        return Fail("non-looped synthetic reparse failed: " + loopError);
    }
    aki::ReplacementResult importedLoopResult;
    if (!aki::ReplaceSoundPcm(
            newLoopRom, newLoopRom.sounds[0], importedLoopWav,
            importedLoopResult, loopError)) {
        return Fail("two-point WAV loop injection failed: " + loopError);
    }
    const auto& allocatedLoop = newLoopRom.sounds[0];
    if (!importedLoopResult.loopImportedFromWav ||
        allocatedLoop.loopControlOffset == 0 ||
        allocatedLoop.loopStart != 8 || allocatedLoop.loopEnd != 40 ||
        GetBe32(newLoopRom.z64,
                allocatedLoop.controlRecordOffset + 0x0C) == 0) {
        return Fail("new loop metadata block was not allocated/written");
    }

    if (argc == 1) {
        std::cout << "Core utility smoke tests passed.\n";
        std::cout << "Pass a NWXE, NA2J, Revenge Redux NW2E, or No Mercy NW4E ROM path to run the full parser/decoder smoke test.\n";
        return 0;
    }

    const std::filesystem::path romPath = argv[1];
    aki::LoadedRom loadedRom;
    std::string error;
    if (!aki::LoadRom(romPath, loadedRom, error)) return Fail(error);

    // Mirror App.cpp: load into a temporary, then move into persistent state.
    aki::LoadedRom rom = std::move(loadedRom);
    if (!rom.profile || rom.profile != &rom.customProfile ||
        rom.profile->banks.empty()) {
        return Fail("LoadedRom app-state move lost the selected game profile");
    }

    aki::LabelDatabase labels;
    std::filesystem::path dataDirectory = argc >= 3 ? argv[2] : std::filesystem::path("data");
    std::filesystem::path labelFile;
    switch (rom.profile->id) {
        case aki::GameId::WrestleMania2000:
            labelFile = dataDirectory / "wm2k_sounds.csv";
            break;
        case aki::GameId::VirtualProWrestling2:
            labelFile = dataDirectory / "vpw2_sounds.csv";
            break;
        case aki::GameId::RevengeRedux:
            labelFile = dataDirectory / "revenge_redux_sounds.csv";
            break;
        case aki::GameId::NoMercy:
            labelFile = dataDirectory / "no_mercy_sounds.csv";
            break;
        default:
            return Fail("no label database for detected profile");
    }
    if (!labels.loadCsv(labelFile, &error)) return Fail(error);
    if (!aki::ParseAkiBanks(rom, &labels, error)) return Fail(error);
    if (rom.sounds.empty()) return Fail("no sounds parsed");

    const aki::SoundRecord* target = nullptr;
    if (rom.profile->id == aki::GameId::WrestleMania2000) {
        for (const auto& sound : rom.sounds) {
            if (sound.bankId == 1 && sound.soundId == 0x005F) {
                target = &sound;
                break;
            }
        }
        if (!target) return Fail("WM2000 Bank 01 / 005F not found");
        if (!target->label.rate.primaryHz || *target->label.rate.primaryHz != 33038) {
            return Fail("WM2000 Bank 01 / 005F rate trace did not resolve to 33038 Hz");
        }
    } else if (rom.profile->id == aki::GameId::VirtualProWrestling2) {
        if (rom.sounds.size() != 516) {
            return Fail("VPW2 did not parse all 516 waveform records");
        }

        const aki::SoundRecord* thunder = nullptr;
        const aki::SoundRecord* theme = nullptr;
        const aki::SoundRecord* announcer = nullptr;
        const aki::SoundRecord* wrestlerVoice = nullptr;
        for (const auto& sound : rom.sounds) {
            if (sound.bankId == 1 && sound.soundId == 0x0037) thunder = &sound;
            if (sound.bankId == 2 && sound.soundId == 0x0000) theme = &sound;
            if (sound.bankId == 3 && sound.soundId == 0x0000) announcer = &sound;
            if (sound.bankId == 6 && sound.soundId == 0x0004) wrestlerVoice = &sound;
        }
        if (!thunder || !theme || !announcer || !wrestlerVoice) {
            return Fail("VPW2 validation targets were not found");
        }
        if (!thunder->label.rate.primaryHz ||
            *thunder->label.rate.primaryHz != 11429 ||
            thunder->coarseTuneSemitones != 1 ||
            thunder->pitchKeys.empty() ||
            thunder->pitchKeys.front() != 0x1F) {
            return Fail("VPW2 Bank 01 / 0037 ROM rate trace failed");
        }
        if (!theme->label.rate.primaryHz ||
            *theme->label.rate.primaryHz != 22050 ||
            std::find(theme->label.rate.alternateHz.begin(),
                      theme->label.rate.alternateHz.end(),
                      21576) == theme->label.rate.alternateHz.end()) {
            return Fail("VPW2 Bank 02 reference/ROM rate evidence failed");
        }
        if (!announcer->label.rate.primaryHz ||
            *announcer->label.rate.primaryHz != 6796 ||
            announcer->label.rate.confidence != aki::RateConfidence::RomDerived) {
            return Fail("VPW2 announcer-bank ROM rate trace failed");
        }
        if (!wrestlerVoice->label.rate.primaryHz ||
            *wrestlerVoice->label.rate.primaryHz != 11025 ||
            std::find(wrestlerVoice->label.rate.alternateHz.begin(),
                      wrestlerVoice->label.rate.alternateHz.end(),
                      11429) == wrestlerVoice->label.rate.alternateHz.end()) {
            return Fail("VPW2 wrestler-voice reference/ROM rate evidence failed");
        }
        target = thunder;
    } else if (rom.profile->id == aki::GameId::NoMercy) {
        if (rom.sounds.size() != 293) return Fail("No Mercy did not parse all 293 waveform records");
        size_t traced = 0;
        for (const auto& sound : rom.sounds) {
            if (sound.label.rate.confidence == aki::RateConfidence::RomDerived) ++traced;
        }
        if (traced == 0) return Fail("No Mercy ROM pitch trace produced no rates");
        for (const auto& sound : rom.sounds) {
            if (sound.bankId == 1 && sound.soundId == 0x005F) { target = &sound; break; }
        }
        if (!target) target = &rom.sounds.front();
    } else if (rom.profile->id == aki::GameId::RevengeRedux) {
        if (rom.sounds.size() != 245) {
            return Fail("Revenge Redux did not parse all 245 bank records");
        }
        size_t bank0Count = 0;
        size_t bank1Count = 0;
        const aki::SoundRecord* copiedBank0 = nullptr;
        const aki::SoundRecord* shiftedBank0 = nullptr;
        const aki::SoundRecord* copiedBank1 = nullptr;
        const aki::SoundRecord* fineTunedBank1 = nullptr;
        const aki::SoundRecord* coarseTunedBank1 = nullptr;
        const aki::SoundRecord* rate33038Bank1 = nullptr;
        const aki::SoundRecord* unmatchedBank1 = nullptr;
        const aki::SoundRecord* unreferencedBank1 = nullptr;
        size_t romDerivedBank1Count = 0;
        for (const auto& sound : rom.sounds) {
            if (sound.bankId == 0) ++bank0Count;
            if (sound.bankId == 1) {
                ++bank1Count;
                if (sound.label.rate.confidence == aki::RateConfidence::RomDerived &&
                    !sound.pitchKeys.empty()) {
                    ++romDerivedBank1Count;
                }
            }
            if (sound.bankId == 0 && sound.soundId == 0x0000) copiedBank0 = &sound;
            if (sound.bankId == 0 && sound.soundId == 0x0033) shiftedBank0 = &sound;
            if (sound.bankId == 1 && sound.soundId == 0x0000) copiedBank1 = &sound;
            if (sound.bankId == 1 && sound.soundId == 0x0001) fineTunedBank1 = &sound;
            if (sound.bankId == 1 && sound.soundId == 0x0031) coarseTunedBank1 = &sound;
            if (sound.bankId == 1 && sound.soundId == 0x0044) rate33038Bank1 = &sound;
            if (sound.bankId == 1 && sound.soundId == 0x005A) unreferencedBank1 = &sound;
            if (sound.bankId == 1 && sound.soundId == 0x005F) {
                unmatchedBank1 = &sound;
                target = &sound;
            }
        }
        if (bank0Count != 96 || bank1Count != 149) {
            return Fail("Revenge Redux bank counts are not 96 / 149");
        }
        if (!copiedBank0 || copiedBank0->label.name != "Bass Drum" ||
            !shiftedBank0 || shiftedBank0->label.name != "cheering" ||
            !copiedBank1 || copiedBank1->label.name != "Ring Bell") {
            return Fail("Revenge Redux did not load the verified WM2000 audio-match labels");
        }
        if (!unmatchedBank1 || !unmatchedBank1->label.name.empty()) {
            return Fail("Revenge Redux unmatched Bank 01 / 005F retained an unverified WM2000 label");
        }
        if (romDerivedBank1Count != 136) {
            return Fail("Revenge Redux did not trace exactly 136 Bank 01 waveforms from ROM scripts");
        }
        if (!fineTunedBank1 || fineTunedBank1->fineTuneCents != 10 ||
            !fineTunedBank1->label.rate.primaryHz ||
            *fineTunedBank1->label.rate.primaryHz != 11089 ||
            fineTunedBank1->pitchKeys != std::vector<uint8_t>{0x1F}) {
            return Fail("Revenge Redux fine-tuned Bank 01 / 0001 rate trace failed");
        }
        if (!coarseTunedBank1 || coarseTunedBank1->coarseTuneSemitones != 2 ||
            !coarseTunedBank1->label.rate.primaryHz ||
            *coarseTunedBank1->label.rate.primaryHz != 20812 ||
            coarseTunedBank1->pitchKeys != std::vector<uint8_t>{0x28}) {
            return Fail("Revenge Redux coarse-tuned Bank 01 / 0031 rate trace failed");
        }
        if (!rate33038Bank1 || !rate33038Bank1->label.rate.primaryHz ||
            *rate33038Bank1->label.rate.primaryHz != 33038 ||
            rate33038Bank1->pitchKeys != std::vector<uint8_t>{0x32}) {
            return Fail("Revenge Redux Bank 01 / 0044 did not trace to 33038 Hz");
        }
        if (!unmatchedBank1 || !unmatchedBank1->label.rate.primaryHz ||
            *unmatchedBank1->label.rate.primaryHz != 31183 ||
            unmatchedBank1->pitchKeys != std::vector<uint8_t>{0x31} ||
            unmatchedBank1->label.rate.confidence != aki::RateConfidence::RomDerived) {
            return Fail("Revenge Redux Bank 01 / 005F ROM rate trace failed");
        }
        if (!unreferencedBank1 || !unreferencedBank1->pitchKeys.empty() ||
            unreferencedBank1->label.rate.confidence == aki::RateConfidence::RomDerived) {
            return Fail("Revenge Redux unreferenced Bank 01 / 005A was assigned a guessed ROM rate");
        }
        if (!target || (target->encodedBytes % 9U) != 0) {
            return Fail("Revenge Redux Bank 01 / 005F validation target is unavailable");
        }
    } else {
        return Fail("unsupported detected profile in full smoke test");
    }

    const auto decoded = aki::DecodeSelectedSound(rom, *target, error);
    if (!error.empty()) return Fail(error);
    if (decoded.empty()) return Fail("decoder returned no samples");
    if (decoded.size() != target->decodedSampleCount()) return Fail("decoded size mismatch");

    // Real-world relocation regression: Badd Blood compacts all WM2000 banks
    // and leaves only six bytes after the entrance TBL.  Older structural
    // validation rejected its non-frame trailer bytes, and ordinary Replace
    // stopped at a size warning instead of using the relocation engine.
    if (romPath.filename().string().find("Badd Blood") != std::string::npos) {
        aki::BankAllocation entranceAllocation;
        if (!aki::GetBankAllocation(rom, 2, entranceAllocation, error)) {
            return Fail("Badd Blood entrance allocation failed: " + error);
        }
        const uint32_t entranceFree =
            entranceAllocation.safeWaveEndOffset - entranceAllocation.normalWaveEndOffset;
        if (entranceFree != 6U) {
            return Fail("Badd Blood entrance bank did not report the expected six free bytes");
        }

        aki::LoadedRom relocated = rom;
        aki::SoundRecord* entrance = nullptr;
        for (auto& candidate : relocated.sounds) {
            if (candidate.bankId == 2 && candidate.soundId == 0) {
                entrance = &candidate;
                break;
            }
        }
        if (!entrance) return Fail("Badd Blood entrance sound 0000 was not found");

        aki::SoundRecord complete = *entrance;
        complete.encodedBytes -= complete.encodedBytes % 9U;
        std::string entranceDecodeError;
        auto entrancePcm = aki::DecodeSelectedSound(relocated, complete, entranceDecodeError);
        if (!entranceDecodeError.empty() || entrancePcm.empty()) {
            return Fail("Badd Blood entrance decode failed: " + entranceDecodeError);
        }
        entrancePcm.insert(entrancePcm.end(), 1600, 0);
        aki::WavPcm16 bigger;
        bigger.sampleRate = entrance->label.rate.primaryHz.value_or(22050U);
        bigger.sourceChannels = 1;
        bigger.monoSamples = std::move(entrancePcm);

        const uint32_t oldCtl = relocated.profile->banks[2].controlOffset;
        const uint32_t oldTbl = relocated.profile->banks[2].waveOffset;
        constexpr uint32_t protectedOffset = 0x0196AFCCU;
        if (static_cast<uint64_t>(protectedOffset) + 0x100U > relocated.z64.size()) {
            return Fail("Badd Blood protected boundary is outside the ROM");
        }
        const std::vector<uint8_t> protectedBytes(
            relocated.z64.begin() + protectedOffset,
            relocated.z64.begin() + protectedOffset + 0x100U);

        aki::ReplacementResult relocationResult;
        if (!aki::ReplaceSoundPcm(relocated, *entrance, bigger, relocationResult, error)) {
            return Fail("Badd Blood automatic entrance relocation failed: " + error);
        }
        if (!relocationResult.bankRelocated ||
            relocated.profile->banks[2].controlOffset == oldCtl ||
            relocated.profile->banks[2].waveOffset == oldTbl) {
            return Fail("Badd Blood oversized replacement did not relocate Bank 02");
        }
        if (!std::equal(protectedBytes.begin(), protectedBytes.end(),
                        relocated.z64.begin() + protectedOffset)) {
            return Fail("Badd Blood relocation modified data after the original full entrance TBL");
        }

        std::vector<aki::BankTraceResult> relocatedTraces;
        if (!aki::TraceSoundBankAsmPointers(relocated, relocatedTraces, error)) {
            return Fail("Badd Blood relocated pointer trace failed: " + error);
        }
        const auto trace = std::find_if(
            relocatedTraces.begin(), relocatedTraces.end(),
            [](const aki::BankTraceResult& item) { return item.bankId == 2; });
        if (trace == relocatedTraces.end() || trace->controlReferences.empty() ||
            trace->waveReferences.empty()) {
            return Fail("Badd Blood relocated entrance ASM pointers were not patched/detected");
        }
    }

    std::vector<uint8_t> reencoded;
    uint32_t paddedSamples = 0;
    if (!aki::EncodePcmWithOriginalBook(*target, decoded, reencoded,
                                        paddedSamples, error)) {
        return Fail("VADPCM re-encode failed: " + error);
    }
    if (paddedSamples != decoded.size()) {
        return Fail("VADPCM re-encode changed an aligned decoded length");
    }
    if (reencoded.size() > target->slotCapacityBytes()) {
        return Fail("round-trip VADPCM did not fit the original slot");
    }

    aki::LoadedRom patched = rom;
    aki::SoundRecord* patchedTarget = nullptr;
    for (auto& sound : patched.sounds) {
        if (sound.bankId == target->bankId &&
            sound.soundId == target->soundId) {
            patchedTarget = &sound;
            break;
        }
    }
    if (!patchedTarget) return Fail("patched target lookup failed");

    aki::WavPcm16 replacementWav;
    replacementWav.sampleRate =
        target->label.rate.primaryHz.value_or(22050);
    replacementWav.sourceChannels = 1;
    replacementWav.monoSamples = decoded;
    aki::ReplacementResult replacementResult;
    if (!aki::ReplaceSoundPcm(patched, *patchedTarget, replacementWav,
                              replacementResult, error)) {
        return Fail("in-place replacement failed: " + error);
    }
    const auto roundTrip =
        aki::DecodeSelectedSound(patched, *patchedTarget, error);
    if (!error.empty() || roundTrip.size() != decoded.size()) {
        return Fail("round-trip decode failed");
    }

    if (rom.profile->id == aki::GameId::RevengeRedux) {
        for (const uint16_t tailId : {static_cast<uint16_t>(0x0093),
                                      static_cast<uint16_t>(0x0094)}) {
            const aki::SoundRecord* before = nullptr;
            const aki::SoundRecord* after = nullptr;
            for (const auto& sound : rom.sounds) {
                if (sound.bankId == 1 && sound.soundId == tailId) before = &sound;
            }
            for (const auto& sound : patched.sounds) {
                if (sound.bankId == 1 && sound.soundId == tailId) after = &sound;
            }
            if (!before || !after || before->encodedBytes != after->encodedBytes ||
                !std::equal(rom.z64.begin() + before->waveDataOffset,
                            rom.z64.begin() + before->waveDataOffset + before->encodedBytes,
                            patched.z64.begin() + after->waveDataOffset)) {
                return Fail("Revenge Redux nonstandard tail record changed during repack");
            }
        }
    }

    long double signal = 0.0L;
    long double noise = 0.0L;
    for (size_t i = 0; i < decoded.size(); ++i) {
        const long double original = decoded[i];
        const long double difference =
            static_cast<long double>(decoded[i]) - roundTrip[i];
        signal += original * original;
        noise += difference * difference;
    }
    const double snr = noise == 0.0L
        ? 999.0
        : 10.0 * std::log10(static_cast<double>(signal / noise));
    if (snr < 12.0) {
        return Fail("VADPCM round-trip quality below 12 dB SNR");
    }

    uint32_t crc1 = 0;
    uint32_t crc2 = 0;
    if (!aki::RepairN64Crc6102(patched.z64, crc1, crc2, error)) {
        return Fail("N64 CRC repair failed: " + error);
    }
    if (crc1 == 0 || crc2 == 0) return Fail("N64 CRC repair returned zero");

    aki::LoadedRom oversized = rom;
    aki::SoundRecord* oversizedTarget = nullptr;
    for (auto& sound : oversized.sounds) {
        if (sound.bankId == target->bankId &&
            sound.soundId == target->soundId) {
            oversizedTarget = &sound;
            break;
        }
    }
    if (!oversizedTarget) return Fail("oversized target lookup failed");
    aki::WavPcm16 oversizedWav;
    oversizedWav.sampleRate = target->label.rate.primaryHz.value_or(22050);
    oversizedWav.sourceChannels = 1;
    oversizedWav.monoSamples = decoded;
    oversizedWav.monoSamples.insert(oversizedWav.monoSamples.end(), 160, 0);
    aki::ReplacementResult oversizedResult;
    const bool oversizedOk = aki::ReplaceSoundPcm(
        oversized, *oversizedTarget, oversizedWav, oversizedResult, error);
    if (oversizedOk) {
        if (!oversizedResult.bankRepacked ||
            oversizedResult.encodedBytes <= target->slotCapacityBytes()) {
            return Fail("oversized replacement did not exercise bank-local TBL repack");
        }
    } else if (error.find("TBL is too large") == std::string::npos) {
        return Fail("oversized replacement failed with an unexpected error: " + error);
    }

    // Regression guard for v0.4.1: a bank repack must not zero or alter the
    // following sequence/control data that lives between one bank's exact TBL
    // end and the next CTL. The normal round-trip replacement above should
    // only touch the selected bank's actual waveform extent and CTL records.
    if (rom.profile->id == aki::GameId::WrestleMania2000) {
        const size_t protectedOffset = 0x0143FD70; // WM2000 Bank 02 sequence object
        if (!std::equal(rom.z64.begin() + protectedOffset,
                        rom.z64.begin() + protectedOffset + 0x100,
                        patched.z64.begin() + protectedOffset)) {
            return Fail("WM2000 protected entrance sequence data was modified by bank repack");
        }
    } else if (rom.profile->id == aki::GameId::VirtualProWrestling2) {
        const size_t protectedOffset = 0x0156E7D0; // VPW2 Bank 02 sequence object
        if (!std::equal(rom.z64.begin() + protectedOffset,
                        rom.z64.begin() + protectedOffset + 0x100,
                        patched.z64.begin() + protectedOffset)) {
            return Fail("VPW2 protected entrance sequence data was modified by bank repack");
        }
    }

    if (argc >= 4) {
        const uint32_t rate = target->label.rate.primaryHz.value_or(22050);
        if (!aki::WriteMonoPcm16Wav(argv[3], decoded, rate, error)) return Fail(error);
        std::cout << "Exported decoder verification WAV: " << argv[3] << '\n';
    }

    std::cout << "PASS: " << rom.profile->displayName << '\n';
    std::cout << "Sounds parsed: " << rom.sounds.size() << '\n';
    std::cout << "Decoded target: bank " << aki::Hex4(target->bankId)
              << " / " << aki::Hex4(target->soundId)
              << " -> " << decoded.size() << " PCM samples\n";
    std::cout << "Round-trip encoder SNR: " << snr << " dB\n";
    
    // v0.7.3 Revenge Redux entrance-theme metadata uses a separate namespace
    // from PtrTablesV2 waveform IDs. Do not overwrite waveform labels with these names.
    {
        const auto& reduxProfile = aki::RevengeReduxProfile();
        if (reduxProfile.entranceThemes.size() != 69)
            return Fail("Redux entrance-theme map should contain 69 entries");
        const auto* firstTheme = aki::FindEntranceThemeBySelector(reduxProfile, 0x3F00);
        if (!(firstTheme && firstTheme->themeId == 0x18 && firstTheme->name == "nWo"))
            return Fail("Redux entrance-theme first entry mismatch");
        const auto* lastTheme = aki::FindEntranceThemeBySelector(reduxProfile, 0x3F44);
        if (!(lastTheme && lastTheme->themeId == 0x5C && lastTheme->name == "Bam Bam"))
            return Fail("Redux entrance-theme last entry mismatch");
        const auto* crow = aki::FindEntranceThemeByThemeId(reduxProfile, 0x1C);
        if (!(crow && crow->selectorId == 0x3F04 && crow->name == "Sting (Crow)"))
            return Fail("Redux entrance-theme reverse lookup mismatch");
    }

    std::cout << "Repaired CRC: " << aki::Hex8(crc1)
              << " / " << aki::Hex8(crc2) << "\n";
    return 0;
}
