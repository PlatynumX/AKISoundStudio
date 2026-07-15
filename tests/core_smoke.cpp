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

aki::LoadedRom MakeSyntheticLoopRom(std::string& error) {
    constexpr uint32_t control = 0x100;
    constexpr uint32_t wave = 0x300;
    constexpr uint32_t protectedObject = 0x380;

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
    rom.z64.assign(0x500, 0);
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

    // Wavosaur-compatible two-point loop round-trip plus allocation of a new
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
        std::cout << "Pass a NWXE or NA2J ROM path to run the full parser/decoder smoke test.\n";
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
    const std::filesystem::path labelFile = rom.profile->id == aki::GameId::WrestleMania2000
        ? dataDirectory / "wm2k_sounds.csv"
        : dataDirectory / "vpw2_sounds.csv";
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
    } else {
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
    }

    const auto decoded = aki::DecodeSelectedSound(rom, *target, error);
    if (!error.empty()) return Fail(error);
    if (decoded.empty()) return Fail("decoder returned no samples");
    if (decoded.size() != target->decodedSampleCount()) return Fail("decoded size mismatch");

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
    std::cout << "Repaired CRC: " << aki::Hex8(crc1)
              << " / " << aki::Hex8(crc2) << "\n";
    return 0;
}
