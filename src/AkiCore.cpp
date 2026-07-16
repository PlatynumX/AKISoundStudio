#include "AkiCore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace aki {
namespace {

constexpr std::array<uint8_t, 4> kZ64Magic{0x80, 0x37, 0x12, 0x40};
constexpr std::array<uint8_t, 4> kV64Magic{0x37, 0x80, 0x40, 0x12};
constexpr std::array<uint8_t, 4> kN64Magic{0x40, 0x12, 0x37, 0x80};
constexpr char kPtrTablesMagic[] = "N64 PtrTablesV2";

uint16_t ReadBe16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) throw std::out_of_range("ReadBe16");
    return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
}

int16_t ReadBeS16(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<int16_t>(ReadBe16(data, offset));
}

uint32_t ReadBe32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) throw std::out_of_range("ReadBe32");
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) |
           static_cast<uint32_t>(data[offset + 3]);
}

uint16_t ReadLe16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) throw std::out_of_range("ReadLe16");
    return static_cast<uint16_t>(data[offset] |
                                 (static_cast<uint16_t>(data[offset + 1]) << 8));
}

uint32_t ReadLe32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) throw std::out_of_range("ReadLe32");
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

void WriteBe32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    if (offset + 4 > data.size()) throw std::out_of_range("WriteBe32");
    data[offset] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 3] = static_cast<uint8_t>(value & 0xFF);
}

void WriteBe16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    if (offset + 2 > data.size()) throw std::out_of_range("WriteBe16");
    data[offset] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 1] = static_cast<uint8_t>(value & 0xFF);
}

uint32_t RotateLeft32(uint32_t value, uint32_t amount) {
    amount &= 31U;
    if (amount == 0) return value;
    return (value << amount) | (value >> (32U - amount));
}

void WriteLe16(std::ostream& out, uint16_t value) {
    const char bytes[2]{
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
    };
    out.write(bytes, 2);
}

void WriteLe32(std::ostream& out, uint32_t value) {
    const char bytes[4]{
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >> 24) & 0xFF),
    };
    out.write(bytes, 4);
}

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> ParseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                current.push_back(ch);
            }
        } else if (ch == '"') {
            quoted = true;
        } else if (ch == ',') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

std::string CsvEscape(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string escaped = "\"";
    for (char ch : value) {
        if (ch == '"') escaped += "\"\"";
        else escaped += ch;
    }
    escaped += '"';
    return escaped;
}

uint32_t ParseHex(const std::string& value) {
    return static_cast<uint32_t>(std::stoul(value, nullptr, 16));
}

std::string CleanAscii(const uint8_t* bytes, size_t length) {
    std::string value;
    value.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        const uint8_t ch = bytes[i];
        value.push_back((ch >= 0x20 && ch <= 0x7E) ? static_cast<char>(ch) : ' ');
    }
    return Trim(value);
}

class Sha1 {
public:
    Sha1() { reset(); }

    void update(const uint8_t* data, size_t length) {
        for (size_t i = 0; i < length; ++i) {
            buffer_[bufferSize_++] = data[i];
            totalBits_ += 8;
            if (bufferSize_ == 64) {
                processBlock(buffer_.data());
                bufferSize_ = 0;
            }
        }
    }

    std::string finish() {
        buffer_[bufferSize_++] = 0x80;
        if (bufferSize_ > 56) {
            while (bufferSize_ < 64) buffer_[bufferSize_++] = 0;
            processBlock(buffer_.data());
            bufferSize_ = 0;
        }
        while (bufferSize_ < 56) buffer_[bufferSize_++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8) {
            buffer_[bufferSize_++] = static_cast<uint8_t>((totalBits_ >> shift) & 0xFF);
        }
        processBlock(buffer_.data());

        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (uint32_t value : state_) out << std::setw(8) << value;
        return out.str();
    }

private:
    std::array<uint32_t, 5> state_{};
    std::array<uint8_t, 64> buffer_{};
    size_t bufferSize_ = 0;
    uint64_t totalBits_ = 0;

    static uint32_t Rol(uint32_t value, unsigned bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    void reset() {
        state_ = {0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};
        bufferSize_ = 0;
        totalBits_ = 0;
    }

    void processBlock(const uint8_t* block) {
        uint32_t words[80]{};
        for (int i = 0; i < 16; ++i) {
            words[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                       (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                       static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            words[i] = Rol(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
        }

        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999U;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1U;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCU;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6U;
            }
            const uint32_t temp = Rol(a, 5) + f + e + k + words[i];
            e = d;
            d = c;
            c = Rol(b, 30);
            b = a;
            a = temp;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }
};

std::string Sha1Hex(const std::vector<uint8_t>& data) {
    Sha1 sha;
    sha.update(data.data(), data.size());
    return sha.finish();
}

bool HasMagic(const std::vector<uint8_t>& data, uint32_t offset, const char* magic, size_t length) {
    return offset + length <= data.size() && std::memcmp(data.data() + offset, magic, length) == 0;
}

int16_t Clamp16(int32_t value) {
    if (value > std::numeric_limits<int16_t>::max()) return std::numeric_limits<int16_t>::max();
    if (value < std::numeric_limits<int16_t>::min()) return std::numeric_limits<int16_t>::min();
    return static_cast<int16_t>(value);
}

void AddAlternateUnique(RateInfo& info, uint32_t rate) {
    if (info.primaryHz && *info.primaryHz == rate) return;
    if (std::find(info.alternateHz.begin(), info.alternateHz.end(), rate) == info.alternateHz.end()) {
        info.alternateHz.push_back(rate);
    }
}


std::map<uint16_t, std::vector<uint8_t>> TraceAkiBankPitchKeys(
    const LoadedRom& rom,
    const BankDefinition& bank,
    uint32_t bankSoundCount) {
    std::map<uint16_t, std::vector<uint8_t>> result;
    const uint32_t objectOffset = bank.sequenceObjectOffset;
    if (objectOffset == 0 || rom.z64.size() < objectOffset + 0x20) return result;

    try {
        const uint32_t wrapperCount = ReadBe32(rom.z64, objectOffset);
        const uint32_t selectorCount = ReadBe32(rom.z64, objectOffset + 8);
        const uint32_t selectorMapRelative = ReadBe32(rom.z64, objectOffset + 0x14);
        if (wrapperCount == 0 || wrapperCount > 0x1000) return result;
        if (selectorCount == 0 || selectorCount > bankSoundCount) return result;
        if (selectorMapRelative < 0x20 ||
            static_cast<uint64_t>(objectOffset) + selectorMapRelative +
                    static_cast<uint64_t>(selectorCount) * 2 >
                rom.z64.size()) {
            return result;
        }

        std::vector<uint16_t> selectorToWave(selectorCount);
        for (uint32_t i = 0; i < selectorCount; ++i) {
            selectorToWave[i] =
                ReadBe16(rom.z64, objectOffset + selectorMapRelative + i * 2);
            if (selectorToWave[i] >= bankSoundCount) return {};
        }

        std::vector<uint32_t> starts(wrapperCount);
        for (uint32_t i = 0; i < wrapperCount; ++i) {
            const size_t entry = objectOffset + 0x18 + i * 8;
            if (entry + 8 > rom.z64.size()) return {};
            starts[i] = ReadBe32(rom.z64, entry);
        }

        const std::map<uint8_t, int> fixedLengths{
            {0x82,1},{0x83,0},{0x84,7},{0x85,1},{0x86,2},{0x87,1},
            {0x88,3},{0x89,3},{0x8A,0},{0x8C,0},{0x8D,1},{0x8E,0},
            {0x8F,1},{0x91,0},{0x92,0},{0x93,0},{0x94,0},{0x95,1},
            {0x96,0},{0x97,3},{0x98,0},{0x99,0},{0x9A,0},{0x9B,1},
            {0x9C,1},{0x9D,2},{0x9F,0},{0xA0,1},{0xA1,6},{0xA2,1},
            {0xA3,2},{0xA4,2},{0xA5,2},{0xA6,1},{0xA8,1},{0xA9,1},
            {0xAA,1},{0xAC,0},
        };
        const std::set<uint8_t> variableOps{0x81,0x8B,0x90,0x9E,0xA7};

        for (uint32_t wrapper = 0; wrapper < wrapperCount; ++wrapper) {
            const uint32_t startRelative = starts[wrapper];
            const uint32_t endRelative =
                (wrapper + 1 < wrapperCount) ? starts[wrapper + 1] : selectorMapRelative;
            if (startRelative >= endRelative ||
                static_cast<uint64_t>(objectOffset) + endRelative > rom.z64.size()) {
                continue;
            }

            size_t pos = objectOffset + startRelative;
            const size_t end = objectOffset + endRelative;
            std::optional<uint32_t> selector;

            while (pos < end) {
                const uint8_t value = rom.z64[pos++];
                if (value >= 0x80) {
                    const uint8_t opcode = value;
                    if (opcode == 0x80) break;

                    size_t parameterLength = 0;
                    if (variableOps.count(opcode)) {
                        if (pos >= end) break;
                        parameterLength = (rom.z64[pos] & 0x80) ? 2 : 1;
                    } else if (opcode == 0xAB) {
                        if (pos >= end) break;
                        parameterLength = 1;
                        if (pos + 1 < end) {
                            parameterLength += (rom.z64[pos + 1] & 0x80) ? 2 : 1;
                        }
                    } else {
                        const auto it = fixedLengths.find(opcode);
                        if (it == fixedLengths.end()) break;
                        parameterLength = static_cast<size_t>(it->second);
                    }

                    if (pos + parameterLength > end) break;
                    if (opcode == 0x81 && parameterLength > 0) {
                        if ((rom.z64[pos] & 0x80) && parameterLength >= 2) {
                            selector = ((rom.z64[pos] & 0x7F) << 8) |
                                       rom.z64[pos + 1];
                        } else {
                            selector = rom.z64[pos];
                        }
                    }
                    pos += parameterLength;
                    continue;
                }

                const uint8_t pitchKey = value;
                if (pos >= end) break;
                const size_t durationLength = (rom.z64[pos] & 0x80) ? 2 : 1;
                if (pos + durationLength > end) break;
                pos += durationLength;

                if (!selector || *selector >= selectorToWave.size()) continue;
                const uint16_t waveId = selectorToWave[*selector];
                auto& keys = result[waveId];
                if (std::find(keys.begin(), keys.end(), pitchKey) == keys.end()) {
                    keys.push_back(pitchKey);
                }
            }
        }
    } catch (...) {
        result.clear();
    }
    return result;
}


std::map<uint16_t, std::vector<uint8_t>> TraceAkiDirectPointerPitchKeys(
    const LoadedRom& rom,
    uint32_t pointerTableOffset,
    uint32_t scriptCount,
    uint32_t bankSoundCount) {
    std::map<uint16_t, std::vector<uint8_t>> result;
    if (pointerTableOffset == 0 || scriptCount == 0 || bankSoundCount == 0) {
        return result;
    }
    if (static_cast<uint64_t>(pointerTableOffset) +
            static_cast<uint64_t>(scriptCount) * 4ULL > rom.z64.size()) {
        return result;
    }

    // Revenge's source segment maps ROM 0x1000 to RAM 0x80000400.
    constexpr uint32_t kSourceRamMinusRom = 0x7FFFF400U;
    try {
        std::vector<uint32_t> starts;
        starts.reserve(scriptCount);
        for (uint32_t i = 0; i < scriptCount; ++i) {
            const uint32_t runtimePointer =
                ReadBe32(rom.z64, pointerTableOffset + i * 4U);
            if (runtimePointer < kSourceRamMinusRom) return {};
            const uint32_t romOffset = runtimePointer - kSourceRamMinusRom;
            if (romOffset >= pointerTableOffset || romOffset >= rom.z64.size()) {
                return {};
            }
            starts.push_back(romOffset);
        }

        const std::map<uint8_t, int> fixedLengths{
            {0x82,1},{0x83,0},{0x84,7},{0x85,1},{0x86,2},{0x87,1},
            {0x88,3},{0x89,3},{0x8A,0},{0x8C,0},{0x8D,1},{0x8E,0},
            {0x8F,1},{0x91,0},{0x92,0},{0x93,0},{0x94,0},{0x95,1},
            {0x96,0},{0x97,3},{0x98,0},{0x99,0},{0x9A,0},{0x9B,1},
            {0x9C,1},{0x9D,2},{0x9F,0},{0xA0,1},{0xA1,6},{0xA2,1},
            {0xA3,2},{0xA4,2},{0xA5,2},{0xA6,1},{0xA8,1},{0xA9,1},
            {0xAA,1},{0xAC,0},
        };
        const std::set<uint8_t> variableOps{0x81,0x8B,0x90,0x9E,0xA7};

        for (uint32_t script = 0; script < scriptCount; ++script) {
            const uint32_t start = starts[script];
            uint32_t end = pointerTableOffset;
            for (const uint32_t candidate : starts) {
                if (candidate > start && candidate < end) end = candidate;
            }
            if (start >= end || end > rom.z64.size()) continue;

            size_t pos = start;
            std::optional<uint32_t> waveId;
            while (pos < end) {
                const uint8_t value = rom.z64[pos++];
                if (value >= 0x80) {
                    const uint8_t opcode = value;
                    if (opcode == 0x80) break;

                    size_t parameterLength = 0;
                    if (variableOps.count(opcode)) {
                        if (pos >= end) break;
                        parameterLength = (rom.z64[pos] & 0x80) ? 2 : 1;
                    } else if (opcode == 0xAB) {
                        if (pos >= end) break;
                        parameterLength = 1;
                        if (pos + 1 < end) {
                            parameterLength +=
                                (rom.z64[pos + 1] & 0x80) ? 2 : 1;
                        }
                    } else {
                        const auto it = fixedLengths.find(opcode);
                        if (it == fixedLengths.end()) break;
                        parameterLength = static_cast<size_t>(it->second);
                    }

                    if (pos + parameterLength > end) break;
                    if (opcode == 0x81 && parameterLength > 0) {
                        uint32_t selected = 0;
                        if ((rom.z64[pos] & 0x80) && parameterLength >= 2) {
                            selected = ((rom.z64[pos] & 0x7F) << 8) |
                                       rom.z64[pos + 1];
                        } else {
                            selected = rom.z64[pos];
                        }
                        waveId = selected < bankSoundCount
                            ? std::optional<uint32_t>(selected)
                            : std::nullopt;
                    }
                    pos += parameterLength;
                    continue;
                }

                const uint8_t pitchKey = value;
                if (pos >= end) break;
                const size_t durationLength = (rom.z64[pos] & 0x80) ? 2 : 1;
                if (pos + durationLength > end) break;
                pos += durationLength;

                if (!waveId) continue;
                auto& keys = result[static_cast<uint16_t>(*waveId)];
                if (std::find(keys.begin(), keys.end(), pitchKey) == keys.end()) {
                    keys.push_back(pitchKey);
                }
            }
        }
    } catch (...) {
        result.clear();
    }
    return result;
}

std::map<uint16_t, std::vector<uint8_t>> TraceRevengeReduxBank1PitchKeys(
    const LoadedRom& rom) {
    uint32_t bankSoundCount = 0;
    for (const auto& sound : rom.sounds) {
        if (sound.bankId == 1) {
            bankSoundCount =
                std::max<uint32_t>(bankSoundCount, sound.soundId + 1U);
        }
    }
    // ROM 0x00030ACC is a 210-entry table of source-resident AKI SFX script
    // pointers. Unlike WM2000's selector object, Redux opcode 0x81 contains
    // the Bank 01 waveform ID directly.
    return TraceAkiDirectPointerPitchKeys(
        rom, 0x00030ACCU, 210U, bankSoundCount);
}

uint32_t AkiPlaybackRateFromPitch(uint32_t mixerRateHz,
                                  uint8_t pitchKey,
                                  int16_t coarseTuneSemitones,
                                  int16_t fineTuneCents = 0) {
    if (mixerRateHz == 0) return 0;
    const double semitoneOffset =
        static_cast<int>(pitchKey) - 48 +
        static_cast<int>(coarseTuneSemitones) +
        static_cast<double>(fineTuneCents) / 100.0;
    const double rate =
        static_cast<double>(mixerRateHz) * std::pow(2.0, semitoneOffset / 12.0);
    return static_cast<uint32_t>(std::llround(rate));
}

std::map<uint16_t, std::vector<uint8_t>> TraceWm2kBank1PitchKeys(const LoadedRom& rom) {
    if (!rom.profile) return {};
    const auto bankIt = std::find_if(
        rom.profile->banks.begin(),
        rom.profile->banks.end(),
        [](const BankDefinition& bank) { return bank.bankId == 1; });
    if (bankIt == rom.profile->banks.end()) return {};

    uint32_t bankSoundCount = 0;
    for (const auto& sound : rom.sounds) {
        if (sound.bankId == 1) {
            bankSoundCount = std::max<uint32_t>(bankSoundCount, sound.soundId + 1);
        }
    }
    return TraceAkiBankPitchKeys(rom, *bankIt, bankSoundCount);
}

uint32_t Wm2kRateFromPitch(uint8_t pitchKey,
                              int16_t coarseTuneSemitones = 0,
                              int16_t fineTuneCents = 0) {
    // WM2000/Revenge use the same AKI SFX pitch convention. Key 0x1F
    // represents 11025 Hz; every twelve key/tuning semitones doubles it.
    const double semitoneOffset =
        static_cast<int>(pitchKey) - 0x1F +
        static_cast<int>(coarseTuneSemitones) +
        static_cast<double>(fineTuneCents) / 100.0;
    const double rate = 11025.0 * std::pow(2.0, semitoneOffset / 12.0);
    return static_cast<uint32_t>(std::llround(rate));
}

bool IsBlankRomByte(uint8_t value) {
    return value == 0x00 || value == 0xFF;
}

uint32_t Align16(uint32_t value) {
    return (value + 0x0FU) & ~0x0FU;
}

size_t Align16Size(size_t value) {
    return (value + 0x0FU) & ~static_cast<size_t>(0x0F);
}

const BankDefinition* FindBankDefinition(const LoadedRom& rom, uint16_t bankId) {
    if (!rom.profile) return nullptr;
    for (const auto& bank : rom.profile->banks) {
        if (bank.bankId == bankId) return &bank;
    }
    return nullptr;
}


uint32_t ExpectedSoundCount(GameId game, uint16_t bankId) {
    if (game == GameId::WrestleMania2000) {
        switch (bankId) {
            case 0: return 46;
            case 1: return 147;
            case 2: return 45;
            default: return 0;
        }
    }
    if (game == GameId::VirtualProWrestling2) {
        switch (bankId) {
            case 0: return 46;
            case 1: return 119;
            case 2: return 10;
            case 3: return 92;
            case 4: return 87;
            case 5: return 36;
            case 6: return 63;
            case 7: return 63;
            default: return 0;
        }
    }
    if (game == GameId::RevengeRedux) {
        switch (bankId) {
            case 0: return 96;
            case 1: return 149;
            default: return 0;
        }
    }
    if (game == GameId::NoMercy) {
        switch (bankId) {
            case 0: return 85;
            case 1: return 165;
            case 2: return 43;
            default: return 0;
        }
    }
    return 0;
}

bool LooksLikeAkiBankAt(const std::vector<uint8_t>& rom,
                       uint32_t controlOffset,
                       uint32_t expectedCount,
                       uint32_t waveOffset) {
    if (controlOffset + 0x30 > rom.size() || waveOffset >= rom.size()) return false;
    if (!HasMagic(rom, controlOffset, kPtrTablesMagic, 15)) return false;
    const uint32_t count = ReadBe32(rom, controlOffset + 0x20);
    if (expectedCount != 0 && count != expectedCount) return false;
    if (count == 0 || count > 0x10000) return false;
    const uint32_t recordTableRelative = ReadBe32(rom, controlOffset + 0x2C);
    const uint64_t recordTableEnd = static_cast<uint64_t>(controlOffset) +
        recordTableRelative + static_cast<uint64_t>(count) * 4ULL;
    if (recordTableEnd > rom.size()) return false;

    uint32_t checked = 0;
    const uint32_t sampleLimit = std::min<uint32_t>(count, 8);
    for (uint32_t id = 0; id < count && checked < sampleLimit; ++id) {
        const uint32_t recordRelative = ReadBe32(rom, controlOffset + recordTableRelative + id * 4U);
        const uint64_t recordOffset = static_cast<uint64_t>(controlOffset) + recordRelative;
        if (recordOffset + 0x18ULL > rom.size()) return false;
        const uint32_t relativeWave = ReadBe32(rom, static_cast<size_t>(recordOffset));
        const uint32_t encodedBytes = ReadBe32(rom, static_cast<size_t>(recordOffset) + 4U);
        const uint32_t bookRelative = ReadBe32(rom, static_cast<size_t>(recordOffset) + 0x10U);
        if (encodedBytes < 9U || encodedBytes > 0x02000000) return false;
        // Modified AKI banks made by older tools may retain one to eight
        // trailer bytes after the last complete 9-byte VADPCM frame.  The
        // decoder and the game consume complete frames and ignore that tail,
        // so it must not make an otherwise valid relocated bank invisible.
        if (bookRelative == 0) return false;
        const uint64_t waveEnd = static_cast<uint64_t>(waveOffset) + relativeWave + encodedBytes;
        if (waveEnd > rom.size()) return false;
        ++checked;
    }
    return checked != 0;
}

std::vector<uint32_t> FindPtrTableCandidates(const std::vector<uint8_t>& rom) {
    std::vector<uint32_t> offsets;
    const size_t magicLength = std::strlen(kPtrTablesMagic);
    for (size_t pos = 0; pos + magicLength <= rom.size(); ++pos) {
        if (std::memcmp(rom.data() + pos, kPtrTablesMagic, magicLength) == 0) {
            offsets.push_back(static_cast<uint32_t>(pos));
            pos += magicLength - 1;
        }
    }
    return offsets;
}


std::vector<uint32_t> FindWaveTableCandidates(const std::vector<uint8_t>& rom) {
    constexpr char kWaveMagic[] = "N64 WaveTables ";
    std::vector<uint32_t> result;
    const size_t magicLength = sizeof(kWaveMagic) - 1;
    for (size_t pos = 0; pos + magicLength <= rom.size(); ++pos) {
        if (std::memcmp(rom.data() + pos, kWaveMagic, magicLength) == 0) {
            result.push_back(static_cast<uint32_t>(pos));
        }
    }
    return result;
}

RateConfidence ParseConfidenceText(std::string value) {
    value = Trim(value);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value == "reference" || value == "confirmed" || value == "confirmed reference") {
        return RateConfidence::ConfirmedReference;
    }
    if (value == "estimate" || value == "reference estimate") return RateConfidence::ReferenceEstimate;
    if (value == "rom-derived" || value == "rom derived") return RateConfidence::RomDerived;
    if (value == "manual" || value == "manual override") return RateConfidence::ManualOverride;
    return RateConfidence::Unknown;
}

int FindHeaderIndex(const std::vector<std::string>& header, const std::string& name) {
    for (size_t i = 0; i < header.size(); ++i) {
        std::string value = Trim(header[i]);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (value == name) return static_cast<int>(i);
    }
    return -1;
}

std::string FieldOrEmpty(const std::vector<std::string>& row, int index) {
    if (index < 0 || static_cast<size_t>(index) >= row.size()) return {};
    return row[static_cast<size_t>(index)];
}

uint32_t ParseHexOffsetFlexible(const std::string& value) {
    std::string text = Trim(value);
    if (text.empty()) return 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text = text.substr(2);
    return static_cast<uint32_t>(std::stoul(text, nullptr, 16));
}

bool ComputeBankAllocation(const LoadedRom& rom,
                           const BankDefinition& bank,
                           BankAllocation& allocation,
                           std::string& error) {
    allocation = {};
    allocation.bankId = bank.bankId;
    allocation.controlStartOffset = bank.controlOffset;
    allocation.normalControlEndOffset = bank.waveOffset;
    allocation.waveStartOffset = bank.waveOffset;

    if (bank.controlOffset >= rom.z64.size() || bank.waveOffset >= rom.z64.size() ||
        bank.controlOffset >= bank.waveOffset) {
        error = "Bank " + Hex4(bank.bankId) + " has invalid configured CTL/TBL offsets.";
        return false;
    }

    // v0.4.1 correction:
    // The old v0.4 code treated everything between this bank's TBL start and
    // the next CTL as writable TBL capacity. That is wrong for AKI games: the
    // next bank's sequence object, and sometimes unrelated packed data, can sit
    // immediately after the last waveform and before the next CTL. Repacking a
    // bank must therefore use the bank's exact original waveform-data extent,
    // derived from its own records, unless the user enables Expert override.
    if (!HasMagic(rom.z64, bank.controlOffset, kPtrTablesMagic, 15)) {
        error = "Bank " + Hex4(bank.bankId) + " does not contain N64 PtrTablesV2 at ROM 0x" +
                Hex8(bank.controlOffset) + ".";
        return false;
    }

    const uint32_t count = ReadBe32(rom.z64, bank.controlOffset + 0x20);
    const uint32_t recordTableRelative = ReadBe32(rom.z64, bank.controlOffset + 0x2C);
    if (count == 0 || count > 0x10000) {
        error = "Invalid sound count while sizing bank " + Hex4(bank.bankId) + ".";
        return false;
    }

    const uint64_t recordTableEnd =
        static_cast<uint64_t>(bank.controlOffset) +
        recordTableRelative +
        static_cast<uint64_t>(count) * 4ULL;
    if (recordTableEnd > rom.z64.size()) {
        error = "Bank " + Hex4(bank.bankId) + " record table is outside the ROM while sizing TBL.";
        return false;
    }

    uint64_t maxWaveEnd = bank.waveOffset;
    for (uint32_t id = 0; id < count; ++id) {
        const uint32_t recordRelative =
            ReadBe32(rom.z64, bank.controlOffset + recordTableRelative + id * 4U);
        const uint64_t recordOffset = static_cast<uint64_t>(bank.controlOffset) + recordRelative;
        if (recordOffset + 8ULL > rom.z64.size()) {
            error = "Bank " + Hex4(bank.bankId) + " sound " + Hex4(id) +
                    " has an invalid control record while sizing TBL.";
            return false;
        }
        const uint32_t relativeWave = ReadBe32(rom.z64, static_cast<size_t>(recordOffset));
        const uint32_t encodedBytes = ReadBe32(rom.z64, static_cast<size_t>(recordOffset) + 4U);
        const uint64_t absoluteEnd =
            static_cast<uint64_t>(bank.waveOffset) + relativeWave + encodedBytes;
        if (absoluteEnd > rom.z64.size()) {
            error = "Bank " + Hex4(bank.bankId) + " sound " + Hex4(id) +
                    " extends outside the ROM while sizing TBL.";
            return false;
        }
        maxWaveEnd = std::max(maxWaveEnd, absoluteEnd);
    }

    if (maxWaveEnd <= bank.waveOffset || maxWaveEnd > rom.z64.size()) {
        error = "Bank " + Hex4(bank.bankId) + " has an invalid original TBL extent.";
        return false;
    }

    allocation.normalWaveEndOffset = static_cast<uint32_t>(maxWaveEnd);

    // N64 Sound Tool can accept modestly larger replacements because AKI banks
    // commonly leave blank alignment/padding bytes immediately after the last
    // waveform.  v0.4.1 correctly stopped treating the entire gap to the next
    // CTL as writable, but v0.5.2 became too strict by rejecting even verified
    // contiguous 00/FF padding.  Extend only through blank bytes and never past
    // a configured CTL, TBL, or sequence-object boundary.
    uint32_t protectedBoundary = static_cast<uint32_t>(rom.z64.size());
    if (rom.profile) {
        const auto considerBoundary = [&](uint32_t candidate) {
            if (candidate > allocation.normalWaveEndOffset &&
                candidate < protectedBoundary) {
                protectedBoundary = candidate;
            }
        };
        for (const auto& profileBank : rom.profile->banks) {
            considerBoundary(profileBank.controlOffset);
            considerBoundary(profileBank.waveOffset);
            if (profileBank.sequenceObjectOffset != 0) {
                considerBoundary(profileBank.sequenceObjectOffset);
            }
        }
    }

    uint32_t safeEnd = allocation.normalWaveEndOffset;
    while (safeEnd < protectedBoundary &&
           safeEnd < rom.z64.size() &&
           IsBlankRomByte(rom.z64[safeEnd])) {
        ++safeEnd;
    }
    allocation.safeWaveEndOffset = safeEnd;
    return true;
}

bool ValidateBlankExtension(const LoadedRom& rom,
                            uint32_t normalEnd,
                            uint32_t overrideEnd,
                            bool force,
                            const char* regionName,
                            std::string& error) {
    if (overrideEnd <= normalEnd || force) return true;
    if (overrideEnd > rom.z64.size()) {
        error = std::string(regionName) + " override end is outside the ROM.";
        return false;
    }
    for (uint32_t offset = normalEnd; offset < overrideEnd; ++offset) {
        if (!IsBlankRomByte(rom.z64[offset])) {
            error = std::string(regionName) + " override area is not blank at ROM 0x" +
                    Hex8(offset) + ". Enable Force only if you have manually verified this range is safe.";
            return false;
        }
    }
    return true;
}

bool ResolveBankWriteLimits(const LoadedRom& rom,
                            const BankAllocation& allocation,
                            const BankWriteOptions* options,
                            uint32_t& allowedControlEnd,
                            uint32_t& allowedWaveEnd,
                            bool& overrideUsed,
                            std::string& error) {
    allowedControlEnd = allocation.normalControlEndOffset;
    allowedWaveEnd = std::max(allocation.normalWaveEndOffset,
                              allocation.safeWaveEndOffset);
    overrideUsed = false;

    if (options && options->enableSizeOverride) {
        overrideUsed = true;
        if (options->controlEndOffset != 0) allowedControlEnd = options->controlEndOffset;
        if (options->waveEndOffset != 0) allowedWaveEnd = options->waveEndOffset;
    }

    if (allowedControlEnd <= allocation.controlStartOffset) {
        error = "CTL override end must be greater than the CTL start offset.";
        return false;
    }
    if (allowedWaveEnd <= allocation.waveStartOffset) {
        error = "TBL override end must be greater than the TBL start offset.";
        return false;
    }
    if (allowedControlEnd > rom.z64.size() || allowedWaveEnd > rom.z64.size()) {
        error = "CTL/TBL override end must be inside the current ROM file.";
        return false;
    }

    const bool force = options && options->enableSizeOverride && options->forceNonBlankOverride;
    if (!ValidateBlankExtension(rom, allocation.normalControlEndOffset, allowedControlEnd, force, "CTL", error)) {
        return false;
    }
    if (!ValidateBlankExtension(rom, allocation.normalWaveEndOffset, allowedWaveEnd, force, "TBL", error)) {
        return false;
    }
    return true;
}

} // namespace

LoadedRom::LoadedRom(const LoadedRom& other)
    : sourcePath(other.sourcePath),
      originalByteOrder(other.originalByteOrder),
      z64(other.z64),
      title(other.title),
      gameCode(other.gameCode),
      sha1(other.sha1),
      customProfile(other.customProfile),
      profile(other.profile == &other.customProfile ? &customProfile : other.profile),
      sounds(other.sounds) {}

LoadedRom& LoadedRom::operator=(const LoadedRom& other) {
    if (this == &other) return *this;
    const bool usesCustomProfile = other.profile == &other.customProfile;
    const GameProfile* externalProfile = other.profile;
    sourcePath = other.sourcePath;
    originalByteOrder = other.originalByteOrder;
    z64 = other.z64;
    title = other.title;
    gameCode = other.gameCode;
    sha1 = other.sha1;
    customProfile = other.customProfile;
    sounds = other.sounds;
    profile = usesCustomProfile ? &customProfile : externalProfile;
    return *this;
}

LoadedRom::LoadedRom(LoadedRom&& other) noexcept {
    *this = std::move(other);
}

LoadedRom& LoadedRom::operator=(LoadedRom&& other) noexcept {
    if (this == &other) return *this;
    const bool usesCustomProfile = other.profile == &other.customProfile;
    const GameProfile* externalProfile = other.profile;
    sourcePath = std::move(other.sourcePath);
    originalByteOrder = other.originalByteOrder;
    z64 = std::move(other.z64);
    title = std::move(other.title);
    gameCode = std::move(other.gameCode);
    sha1 = std::move(other.sha1);
    customProfile = std::move(other.customProfile);
    sounds = std::move(other.sounds);
    profile = usesCustomProfile ? &customProfile : externalProfile;
    other.profile = nullptr;
    return *this;
}

uint32_t SoundRecord::decodedSampleCount() const {
    return (encodedBytes / 9U) * 16U;
}

uint32_t SoundRecord::slotCapacityBytes() const {
    return originalEncodedBytes != 0 ? originalEncodedBytes : encodedBytes;
}

uint32_t BankAllocation::normalControlCapacityBytes() const {
    return normalControlEndOffset > controlStartOffset
        ? normalControlEndOffset - controlStartOffset
        : 0;
}

uint32_t BankAllocation::normalWaveCapacityBytes() const {
    return normalWaveEndOffset > waveStartOffset
        ? normalWaveEndOffset - waveStartOffset
        : 0;
}

bool LabelDatabase::loadCsv(const std::filesystem::path& path, std::string* error) {
    labels_.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error) *error = "Could not open label database: " + path.string();
        return false;
    }

    std::string line;
    bool header = true;
    size_t lineNumber = 0;
    try {
        while (std::getline(input, line)) {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (header) {
                header = false;
                continue;
            }
            if (Trim(line).empty()) continue;
            const auto fields = ParseCsvLine(line);
            if (fields.size() < 6) continue;

            const uint16_t bank = static_cast<uint16_t>(ParseHex(fields[0]));
            const uint16_t id = static_cast<uint16_t>(ParseHex(fields[1]));
            SoundLabel label;
            label.name = fields[2];
            if (!Trim(fields[3]).empty()) label.rate.primaryHz = static_cast<uint32_t>(std::stoul(fields[3]));
            const std::string confidence = Trim(fields[4]);
            if (confidence == "Reference") label.rate.confidence = RateConfidence::ConfirmedReference;
            else if (confidence == "Estimate") label.rate.confidence = RateConfidence::ReferenceEstimate;
            else label.rate.confidence = RateConfidence::Unknown;
            label.rate.method = confidence.empty() ? std::string{} : "AJ World reference list";
            label.rate.note = fields[5];
            labels_[(static_cast<uint32_t>(bank) << 16) | id] = std::move(label);
        }
    } catch (const std::exception& ex) {
        if (error) {
            *error = "Label database parse error at line " + std::to_string(lineNumber) + ": " + ex.what();
        }
        labels_.clear();
        return false;
    }
    return true;
}

std::optional<SoundLabel> LabelDatabase::find(uint16_t bankId, uint16_t soundId) const {
    const auto it = labels_.find((static_cast<uint32_t>(bankId) << 16) | soundId);
    if (it == labels_.end()) return std::nullopt;
    return it->second;
}

const GameProfile& WrestleMania2000Profile() {
    static const GameProfile profile{
        GameId::WrestleMania2000,
        "NWXE",
        "WWF WrestleMania 2000 (USA)",
        "",
        28800,
        {
            {0, 0x0119EA90, 0x011A0C20, 0x00000000, "Instruments and miscellaneous sounds"},
            {1, 0x012B34E0, 0x012B9680, 0x012B1EB0, "Game sounds and commentary"},
            {2, 0x014407C0, 0x01442E20, 0x0143FD70, "Entrance themes"},
        },
    };
    return profile;
}

const GameProfile& VirtualProWrestling2Profile() {
    static const GameProfile profile{
        GameId::VirtualProWrestling2,
        "NA2J",
        "Virtual Pro-Wrestling 2: Oudou Keishou (Japan)",
        "82dd25a044689eab57ab362fe10c0da6388c217a",
        28800,
        {
            {0, 0x0133AC80, 0x0133CE10, 0x00000000, "Musical sounds and effects"},
            {1, 0x0144FD30, 0x01454C20, 0x0144E0A0, "Game sounds"},
            {2, 0x0156EA30, 0x0156F2E0, 0x0156E7D0, "Streamed entrance themes"},
            {3, 0x01C98A10, 0x01C9C700, 0x01C97400, "Announcer voices 1"},
            {4, 0x01D0C170, 0x01D0FB10, 0x01D0AC40, "Announcer voices 2"},
            {5, 0x01E487B0, 0x01E49FB0, 0x01E47EF0, "Announcer voices 3"},
            {6, 0x01E90700, 0x01E930D0, 0x01E90030, "Wrestler voices A"},
            {7, 0x01EDCAE0, 0x01EDF4B0, 0x01EDC410, "Wrestler voices B"},
        },
    };
    return profile;
}

const GameProfile& RevengeReduxProfile() {
    static const GameProfile profile{
        GameId::RevengeRedux,
        "NW2E",
        "WCW/nWo Revenge Redux (USA)",
        "0695b127b654a1d6b79ffe7e62fb8f2981c26d5c",
        28800,
        {
            {0, 0x02D62CEC, 0x02D66BBC, 0x00000000,
             "Redux instruments, music, and miscellaneous sounds"},
            {1, 0x03D9715C, 0x03D9D6EC, 0x00000000,
             "Redux game sounds, voices, and additional data"},
        },
    };
    return profile;
}

const GameProfile& NoMercyProfile() {
    static const GameProfile profile{
        GameId::NoMercy,
        "NW4E",
        "WWF No Mercy (USA) (Rev 1)",
        "",
        28800,
        {
            {0, 0x016F32A0, 0x016F6F10, 0x00000000,
             "Instruments, music, and miscellaneous sounds"},
            {1, 0x01858030, 0x0185EDB0, 0x01855F90,
             "Game sounds and voices"},
            {2, 0x01967410, 0x01969880, 0x01965C50,
             "Entrance themes"},
        },
    };
    return profile;
}

bool ProfileStockBanksPresent(const std::vector<uint8_t>& rom, const GameProfile& profile) {
    for (const auto& bank : profile.banks) {
        const uint32_t expectedCount = ExpectedSoundCount(profile.id, bank.bankId);
        if (!LooksLikeAkiBankAt(rom, bank.controlOffset, expectedCount, bank.waveOffset)) {
            return false;
        }
    }
    return true;
}

bool CandidateCountsContainProfile(const std::vector<uint8_t>& rom, const GameProfile& profile) {
    std::vector<uint32_t> foundCounts;
    for (const uint32_t candidate : FindPtrTableCandidates(rom)) {
        if (candidate + 0x24 > rom.size()) continue;
        try {
            const uint32_t count = ReadBe32(rom, candidate + 0x20);
            if (count != 0 && count < 0x10000) foundCounts.push_back(count);
        } catch (...) {
        }
    }

    std::vector<uint32_t> expected;
    for (const auto& bank : profile.banks) {
        expected.push_back(ExpectedSoundCount(profile.id, bank.bankId));
    }

    std::sort(foundCounts.begin(), foundCounts.end());
    std::sort(expected.begin(), expected.end());

    size_t searchStart = 0;
    for (const uint32_t count : expected) {
        auto it = std::find(foundCounts.begin() + static_cast<std::ptrdiff_t>(searchStart),
                            foundCounts.end(),
                            count);
        if (it == foundCounts.end()) return false;
        searchStart = static_cast<size_t>(std::distance(foundCounts.begin(), it)) + 1U;
    }
    return true;
}

const GameProfile* DetectProfileFromRom(const std::string& gameCode,
                                        const std::vector<uint8_t>& rom) {
    // Header match remains the fastest path for clean stock ROMs.
    if (gameCode == WrestleMania2000Profile().gameCode) return &WrestleMania2000Profile();
    if (gameCode == VirtualProWrestling2Profile().gameCode) return &VirtualProWrestling2Profile();
    if (gameCode == RevengeReduxProfile().gameCode) return &RevengeReduxProfile();
    if (gameCode == NoMercyProfile().gameCode) return &NoMercyProfile();

    // Hack/prototype path: many ROM hacks change title/header code while
    // leaving the AKI sound banks intact.  v0.5 incorrectly rejected those
    // ROMs before the bank scanner ever had a chance to run.
    if (ProfileStockBanksPresent(rom, WrestleMania2000Profile())) return &WrestleMania2000Profile();
    if (ProfileStockBanksPresent(rom, VirtualProWrestling2Profile())) return &VirtualProWrestling2Profile();
    if (ProfileStockBanksPresent(rom, RevengeReduxProfile())) return &RevengeReduxProfile();
    if (ProfileStockBanksPresent(rom, NoMercyProfile())) return &NoMercyProfile();

    // Last-resort family guess from the bank-count signature.  This is enough
    // to choose the profile, after which ParseAkiBanks/AutoDetectSoundBankLocations
    // performs the stricter per-bank parse.
    const bool looksWm2k = CandidateCountsContainProfile(rom, WrestleMania2000Profile());
    const bool looksVpw2 = CandidateCountsContainProfile(rom, VirtualProWrestling2Profile());
    const bool looksRedux = CandidateCountsContainProfile(rom, RevengeReduxProfile());
    const bool looksNoMercy = CandidateCountsContainProfile(rom, NoMercyProfile());
    if (looksWm2k && !looksVpw2 && !looksRedux && !looksNoMercy) return &WrestleMania2000Profile();
    if (looksVpw2 && !looksWm2k && !looksRedux && !looksNoMercy) return &VirtualProWrestling2Profile();
    if (looksRedux && !looksWm2k && !looksVpw2 && !looksNoMercy) return &RevengeReduxProfile();
    if (looksNoMercy && !looksWm2k && !looksVpw2 && !looksRedux) return &NoMercyProfile();

    return nullptr;
}

const GameProfile* DetectProfile(const std::string& gameCode) {
    if (gameCode == WrestleMania2000Profile().gameCode) return &WrestleMania2000Profile();
    if (gameCode == VirtualProWrestling2Profile().gameCode) return &VirtualProWrestling2Profile();
    if (gameCode == RevengeReduxProfile().gameCode) return &RevengeReduxProfile();
    if (gameCode == NoMercyProfile().gameCode) return &NoMercyProfile();
    return nullptr;
}

bool LoadRom(const std::filesystem::path& path, LoadedRom& out, std::string& error) {
    out = {};
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "Could not open ROM: " + path.string();
        return false;
    }
    const auto size = input.tellg();
    if (size < 0x40) {
        error = "The selected file is too small to be an N64 ROM.";
        return false;
    }
    input.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "Could not read the complete ROM file.";
        return false;
    }

    RomByteOrder order = RomByteOrder::Unknown;
    if (std::equal(kZ64Magic.begin(), kZ64Magic.end(), bytes.begin())) {
        order = RomByteOrder::Z64BigEndian;
    } else if (std::equal(kV64Magic.begin(), kV64Magic.end(), bytes.begin())) {
        order = RomByteOrder::V64ByteSwapped;
        for (size_t i = 0; i + 1 < bytes.size(); i += 2) std::swap(bytes[i], bytes[i + 1]);
    } else if (std::equal(kN64Magic.begin(), kN64Magic.end(), bytes.begin())) {
        order = RomByteOrder::N64LittleEndian;
        for (size_t i = 0; i + 3 < bytes.size(); i += 4) {
            std::swap(bytes[i], bytes[i + 3]);
            std::swap(bytes[i + 1], bytes[i + 2]);
        }
    } else {
        error = "Unsupported or unrecognized N64 ROM byte order.";
        return false;
    }

    out.sourcePath = path;
    out.originalByteOrder = order;
    out.z64 = std::move(bytes);
    out.title = CleanAscii(out.z64.data() + 0x20, 20);
    out.gameCode = CleanAscii(out.z64.data() + 0x3B, 4);
    out.sha1 = Sha1Hex(out.z64);
    const GameProfile* detectedProfile = DetectProfileFromRom(out.gameCode, out.z64);
    if (!detectedProfile) {
        error = "Unsupported ROM header/game code '" + out.gameCode +
                "'. No supported AKI WM2000, VPW2, or Revenge Redux sound-bank signature was detected.";
        return false;
    }
    out.customProfile = *detectedProfile;
    out.profile = &out.customProfile;
    if (out.gameCode != detectedProfile->gameCode) {
        out.customProfile.displayName += " compatible hack";
        out.customProfile.gameCode = detectedProfile->gameCode;
    }
    return true;
}

bool ParseAkiBanks(LoadedRom& rom, const LabelDatabase* labels, std::string& error) {
    if (!rom.profile) {
        error = "No AKI game profile is selected.";
        return false;
    }
    if (rom.profile->banks.empty()) {
        error = "The selected AKI game profile contains no sound banks.";
        return false;
    }
    rom.sounds.clear();

    try {
        bool needsAutoDetect = false;
        for (const auto& bank : rom.profile->banks) {
            if (!HasMagic(rom.z64, bank.controlOffset, kPtrTablesMagic, 15)) {
                needsAutoDetect = true;
                break;
            }
        }
        if (needsAutoDetect) {
            std::string detectError;
            if (!AutoDetectSoundBankLocations(rom, detectError)) {
                error = "Configured sound locations failed and auto-detection could not recover them: " + detectError;
                rom.sounds.clear();
                return false;
            }
        }

        for (const auto& bank : rom.profile->banks) {
            if (!HasMagic(rom.z64, bank.controlOffset, kPtrTablesMagic, 15)) {
                error = "Bank " + Hex4(bank.bankId) + " does not contain N64 PtrTablesV2 at ROM 0x" + Hex8(bank.controlOffset) + ".";
                rom.sounds.clear();
                return false;
            }
            const uint32_t count = ReadBe32(rom.z64, bank.controlOffset + 0x20);
            const uint32_t tuningTableRelative = ReadBe32(rom.z64, bank.controlOffset + 0x24);
            const uint32_t fineTuningTableRelative = ReadBe32(rom.z64, bank.controlOffset + 0x28);
            const uint32_t recordTableRelative = ReadBe32(rom.z64, bank.controlOffset + 0x2C);
            if (count == 0 || count > 0x10000) {
                error = "Invalid sound count in bank " + Hex4(bank.bankId) + ".";
                rom.sounds.clear();
                return false;
            }
            const uint64_t pointerTableEnd =
                static_cast<uint64_t>(bank.controlOffset) +
                recordTableRelative +
                static_cast<uint64_t>(count) * 4;
            if (pointerTableEnd > rom.z64.size()) {
                error = "Bank " + Hex4(bank.bankId) + " record table is outside the ROM.";
                rom.sounds.clear();
                return false;
            }

            const bool hasTuningTable =
                tuningTableRelative != 0 &&
                static_cast<uint64_t>(bank.controlOffset) +
                        tuningTableRelative + count <=
                    rom.z64.size();
            const bool hasFineTuningTable =
                fineTuningTableRelative != 0 &&
                static_cast<uint64_t>(bank.controlOffset) +
                        fineTuningTableRelative +
                        static_cast<uint64_t>(count) * 4ULL <=
                    rom.z64.size();

            for (uint32_t id = 0; id < count; ++id) {
                const uint32_t recordRelative = ReadBe32(rom.z64, bank.controlOffset + recordTableRelative + id * 4);
                const uint32_t recordOffset = bank.controlOffset + recordRelative;
                // AKI N64 PtrTablesV2 waveform records are 0x18-byte pointer records.
                // Predictor books and optional loop state are stored elsewhere in the CTL and
                // referenced by bank-relative offsets at +0x10 and +0x0C respectively.
                if (recordOffset + 0x18 > rom.z64.size()) {
                    error = "Bank " + Hex4(bank.bankId) + " sound " + Hex4(id) + " has an invalid control record.";
                    rom.sounds.clear();
                    return false;
                }

                SoundRecord sound;
                sound.bankId = bank.bankId;
                sound.soundId = static_cast<uint16_t>(id);
                sound.controlRecordOffset = recordOffset;
                const uint32_t relativeWave = ReadBe32(rom.z64, recordOffset);
                sound.waveDataOffset = bank.waveOffset + relativeWave;
                sound.encodedBytes = ReadBe32(rom.z64, recordOffset + 4);
                sound.originalEncodedBytes = sound.encodedBytes;

                const uint32_t loopRelative = ReadBe32(rom.z64, recordOffset + 0x0C);
                if (loopRelative != 0) {
                    const uint64_t loopOffset = static_cast<uint64_t>(bank.controlOffset) + loopRelative;
                    // Standard Nintendo ALADPCMloop: start, end, count, then
                    // sixteen signed 16-bit decoder-state samples (0x2C bytes).
                    if (loopOffset + 0x2C > rom.z64.size()) {
                        error = "Bank " + Hex4(bank.bankId) + " sound " + Hex4(id) + " has an invalid loop pointer.";
                        rom.sounds.clear();
                        return false;
                    }
                    sound.loopControlOffset = static_cast<uint32_t>(loopOffset);
                    sound.loopStart = ReadBe32(rom.z64, static_cast<size_t>(loopOffset));
                    sound.loopEnd = ReadBe32(rom.z64, static_cast<size_t>(loopOffset) + 4);
                    sound.loopCount = ReadBe32(rom.z64, static_cast<size_t>(loopOffset) + 8);
                    for (size_t stateIndex = 0; stateIndex < sound.loopState.size(); ++stateIndex) {
                        sound.loopState[stateIndex] = ReadBeS16(
                            rom.z64,
                            static_cast<size_t>(loopOffset) + 0x0C + stateIndex * 2U);
                    }
                }

                const uint32_t bookRelative = ReadBe32(rom.z64, recordOffset + 0x10);
                const uint64_t bookOffset = static_cast<uint64_t>(bank.controlOffset) + bookRelative;
                if (bookRelative == 0 || bookOffset + 8 > rom.z64.size()) {
                    error = "Bank " + Hex4(bank.bankId) + " sound " + Hex4(id) + " has an invalid predictor-book pointer.";
                    rom.sounds.clear();
                    return false;
                }
                sound.predictorOrder = ReadBe32(rom.z64, static_cast<size_t>(bookOffset));
                sound.predictorCount = ReadBe32(rom.z64, static_cast<size_t>(bookOffset) + 4);
                if (hasTuningTable) {
                    const uint8_t rawTune =
                        rom.z64[bank.controlOffset + tuningTableRelative + id];
                    sound.coarseTuneSemitones =
                        rawTune < 0x80
                            ? static_cast<int16_t>(rawTune)
                            : static_cast<int16_t>(static_cast<int>(rawTune) - 256);
                }
                if (hasFineTuningTable) {
                    const uint8_t rawFine = rom.z64[
                        bank.controlOffset + fineTuningTableRelative + id * 4U];
                    sound.fineTuneCents =
                        rawFine < 0x80
                            ? static_cast<int16_t>(rawFine)
                            : static_cast<int16_t>(static_cast<int>(rawFine) - 256);
                }

                const uint64_t bookValues = static_cast<uint64_t>(sound.predictorOrder) * sound.predictorCount * 8;
                const uint64_t bookEnd = bookOffset + 8 + bookValues * 2;
                const uint64_t waveEnd = static_cast<uint64_t>(sound.waveDataOffset) + sound.encodedBytes;
                if (sound.predictorOrder == 0 || sound.predictorCount == 0 || bookValues > 0x10000 ||
                    bookEnd > rom.z64.size() || waveEnd > rom.z64.size()) {
                    error = "Bank " + Hex4(bank.bankId) + " sound " + Hex4(id) + " contains invalid VADPCM metadata.";
                    rom.sounds.clear();
                    return false;
                }
                sound.predictorBook.reserve(static_cast<size_t>(bookValues));
                for (uint64_t coefficient = 0; coefficient < bookValues; ++coefficient) {
                    sound.predictorBook.push_back(ReadBeS16(rom.z64, static_cast<size_t>(bookOffset) + 8 + static_cast<size_t>(coefficient) * 2));
                }

                if (labels) {
                    if (const auto label = labels->find(sound.bankId, sound.soundId)) sound.label = *label;
                }
                rom.sounds.push_back(std::move(sound));
            }
        }
    } catch (const std::exception& ex) {
        error = std::string("Failed to parse AKI sound banks: ") + ex.what();
        rom.sounds.clear();
        return false;
    }

    if (rom.sounds.empty()) {
        error = "The AKI bank parser completed without producing any sound records.";
        return false;
    }

    ApplyProfileRateRules(rom);
    return true;
}

void ApplyProfileRateRules(LoadedRom& rom) {
    if (!rom.profile) return;

    if (rom.profile->id == GameId::VirtualProWrestling2) {
        // Apply bank-wide rates from the AJ World reference before adding ROM evidence.
        for (auto& sound : rom.sounds) {
            if (sound.label.rate.primaryHz) continue;
            if (sound.bankId == 2) {
                sound.label.rate.primaryHz = 22050;
                sound.label.rate.confidence = RateConfidence::ConfirmedReference;
                sound.label.rate.method = "AJ World VPW2 Bank 02 reference";
            } else if (sound.bankId >= 3 && sound.bankId <= 5) {
                sound.label.rate.primaryHz = 7000;
                sound.label.rate.confidence = RateConfidence::ReferenceEstimate;
                sound.label.rate.method = "AJ World VPW2 announcer-bank estimate";
                sound.label.rate.note =
                    "The original reference labels these banks as 7000 Hz with uncertainty.";
            } else if (sound.bankId == 6 || sound.bankId == 7) {
                sound.label.rate.primaryHz = 11025;
                sound.label.rate.confidence = RateConfidence::ConfirmedReference;
                sound.label.rate.method = "AJ World VPW2 wrestler-voice bank reference";
            }
        }

        for (const auto& bank : rom.profile->banks) {
            if (bank.sequenceObjectOffset == 0) continue;

            uint32_t bankSoundCount = 0;
            for (const auto& sound : rom.sounds) {
                if (sound.bankId == bank.bankId) {
                    bankSoundCount =
                        std::max<uint32_t>(bankSoundCount, sound.soundId + 1);
                }
            }
            if (bankSoundCount == 0) continue;

            const auto pitches =
                TraceAkiBankPitchKeys(rom, bank, bankSoundCount);
            for (auto& sound : rom.sounds) {
                if (sound.bankId != bank.bankId) continue;
                const auto pitchIt = pitches.find(sound.soundId);
                if (pitchIt == pitches.end() || pitchIt->second.empty()) continue;

                sound.pitchKeys = pitchIt->second;
                std::vector<uint32_t> derived;
                for (const uint8_t key : sound.pitchKeys) {
                    const uint32_t rate = AkiPlaybackRateFromPitch(
                        rom.profile->mixerRateHz,
                        key,
                        sound.coarseTuneSemitones,
                        sound.fineTuneCents);
                    if (rate != 0 &&
                        std::find(derived.begin(), derived.end(), rate) ==
                            derived.end()) {
                        derived.push_back(rate);
                    }
                }
                if (derived.empty()) continue;

                if (sound.label.rate.confidence ==
                        RateConfidence::ConfirmedReference &&
                    sound.label.rate.primaryHz) {
                    for (const uint32_t rate : derived) {
                        AddAlternateUnique(sound.label.rate, rate);
                    }
                    sound.label.rate.note +=
                        (sound.label.rate.note.empty() ? "" : " ") +
                        std::string(
                            "ROM playback equivalent from the SFX pitch key "
                            "and per-wave coarse tuning: ") +
                        std::to_string(derived.front()) + " Hz.";
                } else {
                    const auto oldEstimate = sound.label.rate.primaryHz;
                    sound.label.rate.primaryHz = derived.front();
                    sound.label.rate.alternateHz.clear();
                    for (size_t i = 1; i < derived.size(); ++i) {
                        AddAlternateUnique(sound.label.rate, derived[i]);
                    }
                    sound.label.rate.confidence = RateConfidence::RomDerived;
                    sound.label.rate.method =
                        "VPW2 SFX script + per-wave coarse tuning";
                    if (oldEstimate && *oldEstimate != derived.front()) {
                        sound.label.rate.note +=
                            (sound.label.rate.note.empty() ? "" : " ") +
                            std::string("Reference estimate was ") +
                            std::to_string(*oldEstimate) + " Hz.";
                    }
                }
            }
        }
        return;
    }

    if (rom.profile->id == GameId::WrestleMania2000) {
        const auto pitches = TraceWm2kBank1PitchKeys(rom);
        for (auto& sound : rom.sounds) {
            if (sound.bankId == 2 && !sound.label.rate.primaryHz) {
                sound.label.rate.primaryHz = 7200;
                sound.label.rate.confidence = RateConfidence::RomDerived;
                sound.label.rate.method = "WM2000 theme-bank ROM tuning";
                sound.label.rate.note = "ROM-derived replacement/import rate; resolves the old 7000/7350 estimate.";
            }
            if (sound.bankId != 1) continue;
            const auto it = pitches.find(sound.soundId);
            if (it == pitches.end() || it->second.empty()) continue;

            sound.pitchKeys = it->second;
            std::vector<uint32_t> derived;
            for (const uint8_t key : it->second) {
                const uint32_t rate = Wm2kRateFromPitch(
                    key, sound.coarseTuneSemitones, sound.fineTuneCents);
                if (std::find(derived.begin(), derived.end(), rate) == derived.end()) derived.push_back(rate);
            }
            if (derived.empty()) continue;

            if (sound.label.rate.confidence == RateConfidence::ConfirmedReference && sound.label.rate.primaryHz) {
                for (uint32_t rate : derived) AddAlternateUnique(sound.label.rate, rate);
                if (sound.label.rate.method.empty()) sound.label.rate.method = "AJ World confirmed rate";
                sound.label.rate.note += (sound.label.rate.note.empty() ? "" : " ") +
                    std::string("ROM script playback equivalent: ") + std::to_string(derived.front()) + " Hz.";
            } else {
                const auto oldEstimate = sound.label.rate.primaryHz;
                sound.label.rate.primaryHz = derived.front();
                sound.label.rate.alternateHz.clear();
                for (size_t i = 1; i < derived.size(); ++i) AddAlternateUnique(sound.label.rate, derived[i]);
                sound.label.rate.confidence = RateConfidence::RomDerived;
                sound.label.rate.method = "WM2000 SFX selector + pitch-key trace";
                if (oldEstimate && *oldEstimate != derived.front()) {
                    sound.label.rate.note += (sound.label.rate.note.empty() ? "" : " ") +
                        std::string("Reference estimate was ") + std::to_string(*oldEstimate) + " Hz.";
                }
            }
        }
        return;
    }

    if (rom.profile->id == GameId::NoMercy) {
        for (const auto& bank : rom.profile->banks) {
            if (bank.sequenceObjectOffset == 0) continue;
            uint32_t bankSoundCount = 0;
            for (const auto& sound : rom.sounds) {
                if (sound.bankId == bank.bankId) {
                    bankSoundCount = std::max<uint32_t>(bankSoundCount, sound.soundId + 1U);
                }
            }
            const auto pitches = TraceAkiBankPitchKeys(rom, bank, bankSoundCount);
            for (auto& sound : rom.sounds) {
                if (sound.bankId != bank.bankId) continue;
                const auto it = pitches.find(sound.soundId);
                if (it == pitches.end() || it->second.empty()) continue;
                sound.pitchKeys = it->second;
                std::vector<uint32_t> derived;
                for (const uint8_t key : it->second) {
                    const uint32_t rate = Wm2kRateFromPitch(
                        key, sound.coarseTuneSemitones, sound.fineTuneCents);
                    if (rate != 0 && std::find(derived.begin(), derived.end(), rate) == derived.end()) {
                        derived.push_back(rate);
                    }
                }
                if (derived.empty()) continue;
                sound.label.rate.primaryHz = derived.front();
                sound.label.rate.alternateHz.clear();
                for (size_t i = 1; i < derived.size(); ++i) AddAlternateUnique(sound.label.rate, derived[i]);
                sound.label.rate.confidence = RateConfidence::RomDerived;
                sound.label.rate.method = "No Mercy SFX selector + pitch-key trace";
                sound.label.rate.note += (sound.label.rate.note.empty() ? "" : " ") +
                    std::string("Pitch keys and selector mapping were read from the ROM sequence object for this bank; coarse/fine tuning comes from the waveform record.");
            }
        }
        return;
    }

    if (rom.profile->id == GameId::RevengeRedux) {
        const auto pitches = TraceRevengeReduxBank1PitchKeys(rom);
        for (auto& sound : rom.sounds) {
            if (sound.bankId != 1) continue;
            const auto it = pitches.find(sound.soundId);
            if (it == pitches.end() || it->second.empty()) continue;

            sound.pitchKeys = it->second;
            std::vector<uint32_t> derived;
            for (const uint8_t key : it->second) {
                const uint32_t rate = Wm2kRateFromPitch(
                    key, sound.coarseTuneSemitones, sound.fineTuneCents);
                if (rate != 0 &&
                    std::find(derived.begin(), derived.end(), rate) ==
                        derived.end()) {
                    derived.push_back(rate);
                }
            }
            if (derived.empty()) continue;

            const auto oldReference = sound.label.rate.primaryHz;
            sound.label.rate.primaryHz = derived.front();
            sound.label.rate.alternateHz.clear();
            for (size_t i = 1; i < derived.size(); ++i) {
                AddAlternateUnique(sound.label.rate, derived[i]);
            }
            if (oldReference && *oldReference != derived.front()) {
                AddAlternateUnique(sound.label.rate, *oldReference);
                sound.label.rate.note +=
                    (sound.label.rate.note.empty() ? "" : " ") +
                    std::string("Previous WM2000-match reference was ") +
                    std::to_string(*oldReference) + " Hz.";
            }
            sound.label.rate.confidence = RateConfidence::RomDerived;
            sound.label.rate.method =
                "Revenge Redux ROM SFX pointer-script + wave tuning";
            sound.label.rate.note +=
                (sound.label.rate.note.empty() ? "" : " ") +
                std::string("Pitch keys come from the 210-entry script-pointer "
                            "table at ROM 0x00030ACC; coarse/fine tuning comes "
                            "from this waveform's PtrTablesV2 record tables.");
        }
    }
}

bool GetBankAllocation(const LoadedRom& rom, uint16_t bankId, BankAllocation& allocation, std::string& error) {
    error.clear();
    const BankDefinition* bank = FindBankDefinition(rom, bankId);
    if (!bank) {
        error = "Unknown bank " + Hex4(bankId) + ".";
        allocation = {};
        return false;
    }
    return ComputeBankAllocation(rom, *bank, allocation, error);
}

namespace {

bool DecodeVadpcmBuffer(const SoundRecord& sound,
                        const uint8_t* encoded,
                        size_t encodedBytes,
                        std::vector<int16_t>& output,
                        std::string& error) {
    output.clear();
    if (sound.predictorOrder != 2) {
        error = "Only standard order-2 Nintendo VADPCM is supported.";
        return false;
    }
    if (sound.predictorCount == 0 ||
        sound.predictorBook.size() <
            static_cast<size_t>(sound.predictorCount) * 16U) {
        error = "The sound has an invalid predictor book.";
        return false;
    }
    if (!encoded || encodedBytes == 0 || (encodedBytes % 9U) != 0) {
        error = "The encoded VADPCM buffer has an invalid size.";
        return false;
    }

    const uint32_t completeFrames = static_cast<uint32_t>(encodedBytes / 9U);
    output.reserve(static_cast<size_t>(completeFrames) * 16U);
    int32_t historyMostRecent = 0;
    int32_t historySecond = 0;

    for (uint32_t frameIndex = 0; frameIndex < completeFrames; ++frameIndex) {
        const size_t frameOffset = static_cast<size_t>(frameIndex) * 9U;
        const uint8_t header = encoded[frameOffset];
        const uint32_t shift = header >> 4;
        const uint32_t predictor = header & 0x0F;
        if (predictor >= sound.predictorCount || shift > 15) {
            error = "Invalid VADPCM frame header at encoded frame " +
                    std::to_string(frameIndex) + ".";
            output.clear();
            return false;
        }

        const int32_t scale = 1 << shift;
        std::array<int32_t, 16> residuals{};
        for (int byteIndex = 0; byteIndex < 8; ++byteIndex) {
            const uint8_t packed = encoded[frameOffset + 1U +
                                           static_cast<size_t>(byteIndex)];
            int32_t high = packed >> 4;
            int32_t low = packed & 0x0F;
            if (high >= 8) high -= 16;
            if (low >= 8) low -= 16;
            residuals[byteIndex * 2] = high * scale;
            residuals[byteIndex * 2 + 1] = low * scale;
        }

        const size_t bookBase = static_cast<size_t>(predictor) * 16U;
        const int16_t* first = sound.predictorBook.data() + bookBase;
        const int16_t* second = sound.predictorBook.data() + bookBase + 8U;

        for (int group = 0; group < 2; ++group) {
            std::array<int16_t, 8> decoded{};
            const int base = group * 8;
            for (int i = 0; i < 8; ++i) {
                int64_t accumulator =
                    static_cast<int64_t>(first[i]) * historySecond +
                    static_cast<int64_t>(second[i]) * historyMostRecent;
                for (int j = 0; j < i; ++j) {
                    accumulator += static_cast<int64_t>(second[i - j - 1]) *
                                   residuals[base + j];
                }
                const int32_t predicted =
                    static_cast<int32_t>(accumulator >> 11);
                decoded[i] = Clamp16(residuals[base + i] + predicted);
                output.push_back(decoded[i]);
            }
            historySecond = decoded[6];
            historyMostRecent = decoded[7];
        }
    }
    return true;
}

bool BuildAdpcmLoopState(const SoundRecord& sound,
                         const std::vector<uint8_t>& encoded,
                         uint32_t loopStart,
                         std::array<int16_t, 16>& state,
                         std::string& error) {
    state.fill(0);
    std::vector<int16_t> decoded;
    if (!DecodeVadpcmBuffer(sound, encoded.data(), encoded.size(), decoded, error)) {
        return false;
    }
    if (loopStart > decoded.size()) {
        error = "The loop start lies beyond the newly encoded waveform.";
        return false;
    }

    // ALADPCMloop.state is the decoder history immediately preceding the loop
    // point.  Use the reconstructed VADPCM samples—not the source PCM—so the
    // state exactly matches the bytes written to the ROM.
    const size_t historyCount = std::min<size_t>(16U, loopStart);
    const size_t sourceStart = static_cast<size_t>(loopStart) - historyCount;
    const size_t destinationStart = state.size() - historyCount;
    for (size_t i = 0; i < historyCount; ++i) {
        state[destinationStart + i] = decoded[sourceStart + i];
    }
    return true;
}

} // namespace

std::vector<int16_t> DecodeSelectedSound(const LoadedRom& rom,
                                         const SoundRecord& sound,
                                         std::string& error) {
    error.clear();
    if (static_cast<uint64_t>(sound.waveDataOffset) + sound.encodedBytes >
        rom.z64.size()) {
        error = "The sound's encoded data lies outside the ROM.";
        return {};
    }

    std::vector<int16_t> output;
    if (!DecodeVadpcmBuffer(
            sound,
            rom.z64.data() + static_cast<size_t>(sound.waveDataOffset),
            sound.encodedBytes,
            output,
            error)) {
        return {};
    }
    return output;
}


bool ReadPcm16Wav(const std::filesystem::path& path,
                  WavPcm16& wav,
                  std::string& error) {
    error.clear();
    wav = {};

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open WAV file: " + path.string();
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 44 || length > static_cast<std::streamoff>(std::numeric_limits<uint32_t>::max())) {
        error = "The WAV file has an invalid or unsupported size.";
        return false;
    }
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!input) {
        error = "Failed while reading the WAV file.";
        return false;
    }

    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        error = "The selected file is not a RIFF/WAVE file.";
        return false;
    }

    size_t fmtOffset = 0;
    uint32_t fmtSize = 0;
    size_t dataOffset = 0;
    uint32_t dataSize = 0;
    size_t smplOffset = 0;
    uint32_t smplSize = 0;
    size_t position = 12;
    while (position + 8 <= bytes.size()) {
        const uint32_t chunkSize = ReadLe32(bytes, position + 4);
        const size_t payload = position + 8;
        const uint64_t chunkEnd = static_cast<uint64_t>(payload) + chunkSize;
        if (chunkEnd > bytes.size()) {
            error = "The WAV file contains a truncated chunk.";
            return false;
        }
        if (std::memcmp(bytes.data() + position, "fmt ", 4) == 0) {
            fmtOffset = payload;
            fmtSize = chunkSize;
        } else if (std::memcmp(bytes.data() + position, "data", 4) == 0) {
            dataOffset = payload;
            dataSize = chunkSize;
        } else if (std::memcmp(bytes.data() + position, "smpl", 4) == 0) {
            smplOffset = payload;
            smplSize = chunkSize;
        }
        position = static_cast<size_t>(chunkEnd + (chunkSize & 1U));
    }

    if (fmtOffset == 0 || fmtSize < 16 || dataOffset == 0) {
        error = "The WAV file is missing a valid fmt or data chunk.";
        return false;
    }

    const uint16_t format = ReadLe16(bytes, fmtOffset);
    const uint16_t channels = ReadLe16(bytes, fmtOffset + 2);
    const uint32_t sampleRate = ReadLe32(bytes, fmtOffset + 4);
    const uint16_t blockAlign = ReadLe16(bytes, fmtOffset + 12);
    const uint16_t bitsPerSample = ReadLe16(bytes, fmtOffset + 14);

    if (format != 1) {
        error = "Only uncompressed integer PCM WAV files are supported.";
        return false;
    }
    if (channels != 1 && channels != 2) {
        error = "Only mono or stereo WAV files are supported.";
        return false;
    }
    if (bitsPerSample != 16 || blockAlign != channels * 2U) {
        error = "Version 0.3 imports 16-bit PCM WAV files only.";
        return false;
    }
    if (sampleRate == 0) {
        error = "The WAV file declares a zero sample rate.";
        return false;
    }
    if (dataSize % blockAlign != 0) {
        error = "The WAV data size is not aligned to complete PCM frames.";
        return false;
    }

    const size_t frames = dataSize / blockAlign;
    if (frames == 0) {
        error = "The WAV file contains no PCM samples.";
        return false;
    }
    wav.sampleRate = sampleRate;
    wav.sourceChannels = channels;

    if (smplOffset != 0) {
        wav.loopMetadataPresent = true;
        if (smplSize < 36) {
            error = "The WAV smpl chunk is too small.";
            return false;
        }
        const uint32_t loopCount = ReadLe32(bytes, smplOffset + 28);
        if (loopCount != 0) {
            if (smplSize < 60) {
                error = "The WAV smpl chunk declares a loop but does not contain a complete loop record.";
                return false;
            }
            const size_t loopRecord = smplOffset + 36;
            const uint32_t loopType = ReadLe32(bytes, loopRecord + 4);
            if (loopType != 0) {
                error = "Only forward WAV smpl loops are supported.";
                return false;
            }
            const uint32_t loopStart = ReadLe32(bytes, loopRecord + 8);
            const uint32_t loopEndInclusive = ReadLe32(bytes, loopRecord + 12);
            if (loopEndInclusive == std::numeric_limits<uint32_t>::max()) {
                error = "The WAV smpl loop end is invalid.";
                return false;
            }
            const uint32_t loopEndExclusive = loopEndInclusive + 1U;
            if (loopStart >= loopEndExclusive || loopEndExclusive > frames) {
                error = "The WAV smpl loop range lies outside the PCM data.";
                return false;
            }
            const uint32_t playCount = ReadLe32(bytes, loopRecord + 20);
            wav.hasLoop = true;
            wav.loopStart = loopStart;
            wav.loopEnd = loopEndExclusive;
            wav.loopCount = playCount == 0 ? 0xFFFFFFFFU : playCount;
        }
    }

    wav.monoSamples.reserve(frames);
    for (size_t frame = 0; frame < frames; ++frame) {
        const size_t sampleOffset = dataOffset + frame * blockAlign;
        const int16_t left = static_cast<int16_t>(ReadLe16(bytes, sampleOffset));
        if (channels == 1) {
            wav.monoSamples.push_back(left);
        } else {
            const int16_t right = static_cast<int16_t>(ReadLe16(bytes, sampleOffset + 2));
            const int32_t mixed = (static_cast<int32_t>(left) +
                                   static_cast<int32_t>(right)) / 2;
            wav.monoSamples.push_back(static_cast<int16_t>(mixed));
        }
    }
    return true;
}

LoopPreviewPlan ResolveLoopPreviewPlan(const SoundRecord& sound,
                                       size_t decodedSampleCount) {
    LoopPreviewPlan plan;
    if (sound.loopControlOffset == 0 ||
        sound.loopStart >= sound.loopEnd ||
        sound.loopStart >= decodedSampleCount) {
        return plan;
    }
    const size_t clampedEnd = std::min<size_t>(sound.loopEnd, decodedSampleCount);
    if (sound.loopStart >= clampedEnd) return plan;
    plan.hasLoop = true;
    plan.introEnd = sound.loopStart;
    plan.loopStart = sound.loopStart;
    plan.loopEnd = static_cast<uint32_t>(clampedEnd);
    return plan;
}

bool ResampleWavPcm16(const WavPcm16& input,
                      uint32_t targetSampleRate,
                      WavPcm16& output,
                      std::string& error) {
    error.clear();
    output = {};
    if (input.sampleRate == 0 || targetSampleRate == 0) {
        error = "Both source and target sample rates must be non-zero.";
        return false;
    }
    if (input.monoSamples.empty()) {
        error = "The WAV contains no PCM samples to resample.";
        return false;
    }
    if (targetSampleRate > 384000U) {
        error = "The target sample rate exceeds the supported 384000 Hz limit.";
        return false;
    }
    if (input.sampleRate == targetSampleRate) {
        output = input;
        return true;
    }

    const long double ratio = static_cast<long double>(targetSampleRate) /
                              static_cast<long double>(input.sampleRate);
    const uint64_t outputCount64 = std::max<uint64_t>(
        1U, static_cast<uint64_t>(std::llround(
                static_cast<long double>(input.monoSamples.size()) * ratio)));
    if (outputCount64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
        outputCount64 > 0x7FFFFFFFU) {
        error = "The resampled WAV would be too large.";
        return false;
    }

    output = input;
    output.sampleRate = targetSampleRate;
    output.sourceChannels = 1;
    output.monoSamples.assign(static_cast<size_t>(outputCount64), 0);

    // Windowed-sinc interpolation with an anti-alias cutoff for downsampling.
    // Sixteen source samples on either side gives substantially cleaner music
    // conversion than linear interpolation while remaining quick for imports.
    constexpr int kRadius = 16;
    constexpr long double kPi = 3.141592653589793238462643383279502884L;
    const long double cutoff = std::min<long double>(1.0L, ratio);
    const long double sourcePerOutput = 1.0L / ratio;
    const auto sinc = [kPi](long double x) {
        if (std::fabs(x) < 1.0e-12L) return 1.0L;
        const long double px = kPi * x;
        return std::sin(px) / px;
    };

    for (size_t outIndex = 0; outIndex < output.monoSamples.size(); ++outIndex) {
        const long double sourcePosition =
            (static_cast<long double>(outIndex) + 0.5L) * sourcePerOutput - 0.5L;
        const int64_t center = static_cast<int64_t>(std::floor(sourcePosition));
        long double weighted = 0.0L;
        long double weightSum = 0.0L;
        for (int tap = -kRadius + 1; tap <= kRadius; ++tap) {
            const int64_t sourceIndex = center + tap;
            if (sourceIndex < 0 ||
                sourceIndex >= static_cast<int64_t>(input.monoSamples.size())) {
                continue;
            }
            const long double distance = sourcePosition -
                                         static_cast<long double>(sourceIndex);
            const long double normalized = distance / static_cast<long double>(kRadius);
            if (std::fabs(normalized) >= 1.0L) continue;
            // Blackman window.
            const long double window = 0.42L +
                0.5L * std::cos(kPi * normalized) +
                0.08L * std::cos(2.0L * kPi * normalized);
            const long double weight = cutoff * sinc(distance * cutoff) * window;
            weighted += static_cast<long double>(
                            input.monoSamples[static_cast<size_t>(sourceIndex)]) * weight;
            weightSum += weight;
        }
        if (std::fabs(weightSum) > 1.0e-18L) weighted /= weightSum;
        const long long rounded = std::llround(weighted);
        output.monoSamples[outIndex] = static_cast<int16_t>(std::clamp<long long>(
            rounded,
            std::numeric_limits<int16_t>::min(),
            std::numeric_limits<int16_t>::max()));
    }

    if (input.hasLoop) {
        const auto scalePoint = [ratio](uint32_t point) -> uint32_t {
            const long double scaled = static_cast<long double>(point) * ratio;
            return static_cast<uint32_t>(std::llround(scaled));
        };
        output.loopStart = std::min<uint32_t>(
            scalePoint(input.loopStart),
            static_cast<uint32_t>(output.monoSamples.size() - 1U));
        output.loopEnd = std::min<uint32_t>(
            scalePoint(input.loopEnd),
            static_cast<uint32_t>(output.monoSamples.size()));
        if (output.loopEnd <= output.loopStart) {
            output.loopEnd = std::min<uint32_t>(
                output.loopStart + 1U,
                static_cast<uint32_t>(output.monoSamples.size()));
        }
        output.hasLoop = output.loopStart < output.loopEnd;
    }
    return true;
}


bool ApplyWavGain(WavPcm16& wav,
                  double gainDb,
                  bool preventClipping,
                  GainResult& result,
                  std::string& error) {
    result = {};
    result.requestedDb = gainDb;
    if (!std::isfinite(gainDb) || gainDb < -60.0 || gainDb > 60.0) {
        error = "Import gain must be between -60.0 and +60.0 dB.";
        return false;
    }
    if (wav.monoSamples.empty()) {
        error = "The WAV contains no PCM samples to amplify.";
        return false;
    }

    int32_t peak = 0;
    for (int16_t sample : wav.monoSamples) {
        const int32_t magnitude = sample == INT16_MIN ? 32768 : std::abs(static_cast<int32_t>(sample));
        peak = std::max(peak, magnitude);
    }
    result.peakBefore = static_cast<int16_t>(std::min<int32_t>(peak, 32767));

    double appliedDb = gainDb;
    if (preventClipping && gainDb > 0.0 && peak > 0) {
        const double maximumSafeDb = 20.0 * std::log10(32767.0 / static_cast<double>(peak));
        if (appliedDb > maximumSafeDb) {
            appliedDb = maximumSafeDb;
            result.limitedToPreventClipping = true;
        }
    }
    result.appliedDb = appliedDb;
    const double multiplier = std::pow(10.0, appliedDb / 20.0);
    int32_t peakAfter = 0;
    for (int16_t& sample : wav.monoSamples) {
        const long long scaled = std::llround(static_cast<double>(sample) * multiplier);
        if (scaled > 32767) {
            sample = 32767;
            ++result.clippedSamples;
        } else if (scaled < -32768) {
            sample = -32768;
            ++result.clippedSamples;
        } else {
            sample = static_cast<int16_t>(scaled);
        }
        const int32_t magnitude = sample == INT16_MIN ? 32768 : std::abs(static_cast<int32_t>(sample));
        peakAfter = std::max(peakAfter, magnitude);
    }
    result.peakAfter = static_cast<int16_t>(std::min<int32_t>(peakAfter, 32767));
    return true;
}

bool EncodePcmWithOriginalBook(const SoundRecord& sound,
                               const std::vector<int16_t>& samples,
                               std::vector<uint8_t>& encoded,
                               uint32_t& paddedSamples,
                               std::string& error) {
    error.clear();
    encoded.clear();
    paddedSamples = 0;

    if (samples.empty()) {
        error = "The replacement waveform contains no samples.";
        return false;
    }
    if (sound.predictorOrder != 2) {
        error = "Replacement currently supports standard order-2 Nintendo VADPCM only.";
        return false;
    }
    if (sound.predictorCount == 0 || sound.predictorCount > 16 ||
        sound.predictorBook.size() <
            static_cast<size_t>(sound.predictorCount) * 16U) {
        error = "The selected sound has an unsupported predictor book.";
        return false;
    }
    const uint64_t frameCount64 =
        (static_cast<uint64_t>(samples.size()) + 15U) / 16U;
    if (frameCount64 > std::numeric_limits<uint32_t>::max() / 9U) {
        error = "The replacement waveform is too large.";
        return false;
    }
    const uint32_t frameCount = static_cast<uint32_t>(frameCount64);
    paddedSamples = frameCount * 16U;
    encoded.reserve(static_cast<size_t>(frameCount) * 9U);

    int32_t historyMostRecent = 0;
    int32_t historySecond = 0;

    for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        std::array<int16_t, 16> target{};
        for (size_t i = 0; i < target.size(); ++i) {
            const uint64_t sourceIndex =
                static_cast<uint64_t>(frameIndex) * 16U + i;
            if (sourceIndex < samples.size()) {
                target[i] = samples[static_cast<size_t>(sourceIndex)];
            }
        }

        long double bestError =
            std::numeric_limits<long double>::infinity();
        std::array<uint8_t, 9> bestBytes{};
        int32_t bestHistoryMostRecent = historyMostRecent;
        int32_t bestHistorySecond = historySecond;

        for (uint32_t predictor = 0;
             predictor < sound.predictorCount;
             ++predictor) {
            const size_t bookBase = static_cast<size_t>(predictor) * 16U;
            const int16_t* first = sound.predictorBook.data() + bookBase;
            const int16_t* second = sound.predictorBook.data() + bookBase + 8U;

            for (uint32_t shift = 0; shift <= 12; ++shift) {
                const int32_t scale = 1 << shift;
                int32_t candidateMostRecent = historyMostRecent;
                int32_t candidateSecond = historySecond;
                std::array<int32_t, 16> residuals{};
                std::array<int8_t, 16> nibbles{};
                long double squaredError = 0.0L;

                for (int group = 0; group < 2; ++group) {
                    std::array<int16_t, 8> reconstructed{};
                    const int base = group * 8;
                    for (int i = 0; i < 8; ++i) {
                        int64_t accumulator =
                            static_cast<int64_t>(first[i]) *
                                candidateSecond +
                            static_cast<int64_t>(second[i]) *
                                candidateMostRecent;
                        for (int j = 0; j < i; ++j) {
                            accumulator +=
                                static_cast<int64_t>(
                                    second[i - j - 1]) *
                                residuals[base + j];
                        }
                        const int32_t predicted =
                            static_cast<int32_t>(accumulator >> 11);
                        const int64_t difference =
                            static_cast<int64_t>(target[base + i]) -
                            predicted;

                        int64_t quantized = 0;
                        if (difference >= 0) {
                            quantized =
                                (difference + scale / 2) / scale;
                        } else {
                            quantized =
                                -((-difference + scale / 2) / scale);
                        }
                        quantized =
                            std::clamp<int64_t>(quantized, -8, 7);
                        nibbles[base + i] =
                            static_cast<int8_t>(quantized);
                        residuals[base + i] =
                            static_cast<int32_t>(quantized) * scale;

                        reconstructed[i] = Clamp16(
                            predicted + residuals[base + i]);
                        const int64_t errorValue =
                            static_cast<int64_t>(target[base + i]) -
                            reconstructed[i];
                        squaredError +=
                            static_cast<long double>(errorValue) *
                            static_cast<long double>(errorValue);
                    }
                    candidateSecond = reconstructed[6];
                    candidateMostRecent = reconstructed[7];
                }

                if (squaredError < bestError) {
                    bestError = squaredError;
                    bestBytes[0] = static_cast<uint8_t>(
                        (shift << 4) | predictor);
                    for (int byteIndex = 0; byteIndex < 8; ++byteIndex) {
                        const uint8_t high = static_cast<uint8_t>(
                            nibbles[byteIndex * 2] & 0x0F);
                        const uint8_t low = static_cast<uint8_t>(
                            nibbles[byteIndex * 2 + 1] & 0x0F);
                        bestBytes[1 + byteIndex] =
                            static_cast<uint8_t>((high << 4) | low);
                    }
                    bestHistorySecond = candidateSecond;
                    bestHistoryMostRecent = candidateMostRecent;
                }
            }
        }

        encoded.insert(encoded.end(), bestBytes.begin(), bestBytes.end());
        historySecond = bestHistorySecond;
        historyMostRecent = bestHistoryMostRecent;
    }

    return true;
}

namespace {

struct ReplacementLoopPlan {
    bool enabled = false;
    bool importedFromWav = false;
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t count = 0;
    std::array<int16_t, 16> state{};
    uint32_t controlOffset = 0;
};

bool ResolveReplacementLoopPlan(const SoundRecord& sound,
                                const WavPcm16& wav,
                                const std::vector<uint8_t>& encoded,
                                ReplacementLoopPlan& plan,
                                std::string& error) {
    plan = {};

    // standard WAV looping is marker-driven. A WAV either supplies one
    // forward loop through its two saved loop points, or it is non-looping.
    // Never reuse numeric loop positions from the sound being replaced: those
    // positions belong to the old waveform and are meaningless for new music.
    if (wav.hasLoop) {
        plan.enabled = true;
        plan.importedFromWav = true;
        plan.start = wav.loopStart;
        plan.end = wav.loopEnd;
        plan.count = wav.loopCount;
    }

    if (!plan.enabled) return true;
    if (plan.start >= plan.end || plan.end > wav.monoSamples.size()) {
        error = "The replacement loop range lies outside the imported PCM waveform.";
        return false;
    }
    return BuildAdpcmLoopState(sound, encoded, plan.start, plan.state, error);
}

struct ControlInterval {
    uint32_t begin = 0;
    uint32_t end = 0;
};

bool IntervalsOverlap(uint32_t begin,
                      uint32_t end,
                      const std::vector<ControlInterval>& intervals) {
    for (const auto& interval : intervals) {
        if (begin < interval.end && end > interval.begin) return true;
    }
    return false;
}

bool FindFreeLoopControlBlock(const LoadedRom& rom,
                              const BankDefinition& bank,
                              uint32_t allowedControlEnd,
                              uint32_t& loopOffset,
                              std::string& error) {
    constexpr uint32_t kLoopBytes = 0x2C;
    loopOffset = 0;
    if (allowedControlEnd <= bank.controlOffset + kLoopBytes ||
        allowedControlEnd > rom.z64.size()) {
        error = "The selected bank has no valid CTL range for loop metadata.";
        return false;
    }

    const uint32_t count = ReadBe32(rom.z64, bank.controlOffset + 0x20);
    const uint32_t tuningStartRelative =
        ReadBe32(rom.z64, bank.controlOffset + 0x24);
    const uint32_t tuningEndRelative =
        ReadBe32(rom.z64, bank.controlOffset + 0x28);
    const uint32_t recordTableRelative =
        ReadBe32(rom.z64, bank.controlOffset + 0x2C);

    std::vector<ControlInterval> occupied;
    occupied.push_back({bank.controlOffset, bank.controlOffset + 0x30U});
    const uint64_t pointerEnd64 =
        static_cast<uint64_t>(bank.controlOffset) + recordTableRelative +
        static_cast<uint64_t>(count) * 4U;
    if (pointerEnd64 > allowedControlEnd) {
        error = "The bank record-pointer table lies outside the allowed CTL range.";
        return false;
    }
    occupied.push_back({bank.controlOffset + recordTableRelative,
                        static_cast<uint32_t>(pointerEnd64)});

    if (tuningStartRelative != 0 &&
        tuningStartRelative < tuningEndRelative &&
        static_cast<uint64_t>(bank.controlOffset) + tuningEndRelative <=
            allowedControlEnd) {
        occupied.push_back({bank.controlOffset + tuningStartRelative,
                            bank.controlOffset + tuningEndRelative});
    }

    for (uint32_t id = 0; id < count; ++id) {
        const uint32_t recordRelative = ReadBe32(
            rom.z64,
            bank.controlOffset + recordTableRelative + id * 4U);
        const uint32_t recordOffset = bank.controlOffset + recordRelative;
        if (static_cast<uint64_t>(recordOffset) + 0x18U > allowedControlEnd) {
            error = "A sound control record lies outside the allowed CTL range.";
            return false;
        }
        occupied.push_back({recordOffset, recordOffset + 0x18U});

        const uint32_t loopRelative = ReadBe32(rom.z64, recordOffset + 0x0C);
        if (loopRelative != 0) {
            const uint32_t existingLoop = bank.controlOffset + loopRelative;
            if (static_cast<uint64_t>(existingLoop) + kLoopBytes <=
                allowedControlEnd) {
                occupied.push_back({existingLoop, existingLoop + kLoopBytes});
            }
        }

        const uint32_t bookRelative = ReadBe32(rom.z64, recordOffset + 0x10);
        if (bookRelative != 0) {
            const uint32_t bookOffset = bank.controlOffset + bookRelative;
            if (static_cast<uint64_t>(bookOffset) + 8U <= allowedControlEnd) {
                const uint32_t order = ReadBe32(rom.z64, bookOffset);
                const uint32_t predictors = ReadBe32(rom.z64, bookOffset + 4U);
                const uint64_t bookEnd64 =
                    static_cast<uint64_t>(bookOffset) + 8U +
                    static_cast<uint64_t>(order) * predictors * 16U;
                if (bookEnd64 <= allowedControlEnd) {
                    occupied.push_back({bookOffset,
                                        static_cast<uint32_t>(bookEnd64)});
                }
            }
        }
    }

    uint32_t candidate = Align16(bank.controlOffset + 0x30U);
    for (; static_cast<uint64_t>(candidate) + kLoopBytes <= allowedControlEnd;
         candidate += 4U) {
        const uint32_t candidateEnd = candidate + kLoopBytes;
        if (IntervalsOverlap(candidate, candidateEnd, occupied)) continue;
        bool blank = true;
        for (uint32_t offset = candidate; offset < candidateEnd; ++offset) {
            if (!IsBlankRomByte(rom.z64[offset])) {
                blank = false;
                break;
            }
        }
        if (blank) {
            loopOffset = candidate;
            return true;
        }
    }

    error =
        "No free 0x2C-byte loop-state block was found in the bank CTL. "
        "Use a WAV without loop metadata, replace an already-looped slot, or "
        "provide a verified CTL override range.";
    return false;
}

bool RelocateBankForExpansion(LoadedRom& rom,
                              uint16_t bankId,
                              uint32_t extraCtlBytes,
                              uint32_t extraWaveBytes,
                              std::string& error);

bool LoopBlockIsShared(const LoadedRom& rom,
                       const SoundRecord& selected) {
    if (selected.loopControlOffset == 0) return false;
    size_t references = 0;
    for (const auto& item : rom.sounds) {
        if (item.bankId == selected.bankId &&
            item.loopControlOffset == selected.loopControlOffset) {
            ++references;
        }
    }
    return references > 1;
}

} // namespace

bool ReplaceSoundPcm(LoadedRom& rom,
                     SoundRecord& sound,
                     const WavPcm16& wav,
                     ReplacementResult& result,
                     std::string& error) {
    BankWriteOptions options;
    return ReplaceSoundPcm(rom, sound, wav, options, result, error);
}

bool ReplaceSoundPcm(LoadedRom& rom,
                     SoundRecord& sound,
                     const WavPcm16& wav,
                     const BankWriteOptions& options,
                     ReplacementResult& result,
                     std::string& error) {
    error.clear();
    result = {};

    std::vector<uint8_t> encoded;
    uint32_t paddedSamples = 0;
    if (!EncodePcmWithOriginalBook(
            sound, wav.monoSamples, encoded, paddedSamples, error)) {
        return false;
    }

    const BankDefinition* bank = FindBankDefinition(rom, sound.bankId);
    if (!bank) {
        error = "Could not locate the selected sound's bank definition.";
        return false;
    }

    BankAllocation allocation;
    if (!ComputeBankAllocation(rom, *bank, allocation, error)) return false;

    uint32_t allowedControlEnd = 0;
    uint32_t allowedWaveEnd = 0;
    bool overrideUsed = false;
    if (!ResolveBankWriteLimits(rom, allocation, &options,
                                allowedControlEnd, allowedWaveEnd,
                                overrideUsed, error)) {
        return false;
    }

    ReplacementLoopPlan loopPlan;
    if (!ResolveReplacementLoopPlan(sound, wav, encoded, loopPlan, error)) {
        return false;
    }
    if (loopPlan.enabled) {
        if (sound.loopControlOffset != 0 && !LoopBlockIsShared(rom, sound)) {
            loopPlan.controlOffset = sound.loopControlOffset;
        } else if (!FindFreeLoopControlBlock(
                       rom, *bank, allowedControlEnd,
                       loopPlan.controlOffset, error)) {
            return false;
        }
        if (loopPlan.controlOffset < bank->controlOffset ||
            static_cast<uint64_t>(loopPlan.controlOffset) + 0x2CU >
                allowedControlEnd) {
            error = "The selected loop-state block lies outside the allowed CTL range.";
            return false;
        }
    }

    const uint32_t normalTblCapacity = allocation.normalWaveCapacityBytes();
    const uint32_t allowedTblCapacity = allowedWaveEnd - allocation.waveStartOffset;

    size_t selectedIndex = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < rom.sounds.size(); ++i) {
        if (&rom.sounds[i] == &sound) {
            selectedIndex = i;
            break;
        }
    }
    if (selectedIndex == std::numeric_limits<size_t>::max()) {
        error = "The selected sound is not part of the loaded ROM state.";
        return false;
    }

    std::vector<size_t> bankIndices;
    for (size_t i = 0; i < rom.sounds.size(); ++i) {
        if (rom.sounds[i].bankId == sound.bankId) bankIndices.push_back(i);
    }
    std::sort(bankIndices.begin(), bankIndices.end(),
              [&](size_t lhs, size_t rhs) {
                  return rom.sounds[lhs].soundId < rom.sounds[rhs].soundId;
              });

    if (bankIndices.empty()) {
        error = "The selected bank contains no sounds.";
        return false;
    }

    std::map<size_t, std::vector<uint8_t>> waveBytes;
    uint32_t initialRelativeOffset = std::numeric_limits<uint32_t>::max();
    for (const size_t index : bankIndices) {
        const auto& item = rom.sounds[index];
        if (item.waveDataOffset < allocation.waveStartOffset) {
            error = "Bank " + Hex4(sound.bankId) + " sound " + Hex4(item.soundId) +
                    " has a wave offset before the bank TBL start.";
            return false;
        }
        const uint32_t relative = item.waveDataOffset - allocation.waveStartOffset;
        initialRelativeOffset = std::min(initialRelativeOffset, relative);

        if (index == selectedIndex) {
            waveBytes[index] = encoded;
            continue;
        }

        if (static_cast<uint64_t>(item.waveDataOffset) + item.encodedBytes > rom.z64.size()) {
            error = "Bank " + Hex4(sound.bankId) + " sound " + Hex4(item.soundId) +
                    " lies outside the ROM and cannot be repacked.";
            return false;
        }
        const auto begin = rom.z64.begin() + static_cast<std::ptrdiff_t>(item.waveDataOffset);
        waveBytes[index] = std::vector<uint8_t>(begin,
            begin + static_cast<std::ptrdiff_t>(item.encodedBytes));
    }

    if (initialRelativeOffset == std::numeric_limits<uint32_t>::max()) initialRelativeOffset = 0;
    initialRelativeOffset = Align16(initialRelativeOffset);

    std::vector<uint8_t> rebuiltTbl(static_cast<size_t>(initialRelativeOffset), 0);
    std::map<size_t, uint32_t> newRelativeOffsets;
    for (const size_t index : bankIndices) {
        const size_t aligned = Align16Size(rebuiltTbl.size());
        if (aligned > rebuiltTbl.size()) rebuiltTbl.resize(aligned, 0);
        if (rebuiltTbl.size() > std::numeric_limits<uint32_t>::max()) {
            error = "The rebuilt TBL is too large.";
            return false;
        }
        newRelativeOffsets[index] = static_cast<uint32_t>(rebuiltTbl.size());
        const auto& bytes = waveBytes[index];
        rebuiltTbl.insert(rebuiltTbl.end(), bytes.begin(), bytes.end());
    }

    // Do not add final alignment padding after the last waveform. Several AKI
    // banks have another sequence/control blob immediately after the exact last
    // ADPCM byte. Offsets for following waves are aligned before each wave;
    // nothing needs the bank TBL's final byte count to be 16-byte aligned.

    result.inputSampleRate = wav.sampleRate;
    result.inputSamples = static_cast<uint32_t>(wav.monoSamples.size());
    result.paddedSamples = paddedSamples;
    result.encodedBytes = static_cast<uint32_t>(encoded.size());
    result.slotCapacityBytes = sound.slotCapacityBytes();
    result.rebuiltTblBytes = static_cast<uint32_t>(rebuiltTbl.size());
    result.normalTblCapacityBytes = normalTblCapacity;
    result.allowedTblCapacityBytes = allowedTblCapacity;
    result.normalTblEndOffset = allocation.normalWaveEndOffset;
    result.allowedTblEndOffset = allowedWaveEnd;
    result.loopEnabled = loopPlan.enabled;
    result.loopImportedFromWav = loopPlan.importedFromWav;
    result.loopStateRebuilt = loopPlan.enabled;
    result.loopStart = loopPlan.start;
    result.loopEnd = loopPlan.end;
    result.loopCount = loopPlan.count;
    result.bankRepacked = true;
    result.sizeOverrideUsed = overrideUsed;

    if (rebuiltTbl.size() > allowedTblCapacity) {
        // Normal replacement now follows the same safe path as Add Sound and
        // ROM migration: when the bank is genuinely full, relocate it and
        // patch every traced CTL/TBL ASM reference instead of asking the user
        // to overwrite whatever follows the bank. Expert override remains an
        // explicit manual path and is never silently replaced by relocation.
        if (!options.enableSizeOverride) {
            const uint32_t selectedBank = sound.bankId;
            const uint32_t selectedId = sound.soundId;
            const uint32_t shortage = static_cast<uint32_t>(rebuiltTbl.size() - allowedTblCapacity);
            const uint32_t reserve = Align16(shortage) + 0x1000U;
            if (!RelocateBankForExpansion(rom, selectedBank, 0U, reserve, error)) {
                return false;
            }
            (void)selectedId;
            if (!ReplaceSoundPcm(rom, sound, wav, options, result, error)) {
                return false;
            }
            result.bankRelocated = true;
            return true;
        }

        const uint64_t minimumEnd = static_cast<uint64_t>(allocation.waveStartOffset) + rebuiltTbl.size();
        const uint32_t suggestedEnd = Align16(static_cast<uint32_t>(std::min<uint64_t>(minimumEnd, std::numeric_limits<uint32_t>::max())));
        std::ostringstream out;
        out << "The rebuilt Bank " << Hex4(sound.bankId) << " TBL is too large by 0x"
            << std::uppercase << std::hex << std::setfill('0') << std::setw(8)
            << static_cast<uint32_t>(rebuiltTbl.size() - allowedTblCapacity)
            << " bytes.\n\nNormal TBL end: 0x" << Hex8(allocation.normalWaveEndOffset)
            << "\nAllowed TBL end: 0x" << Hex8(allowedWaveEnd)
            << "\nMinimum required end: 0x" << Hex8(static_cast<uint32_t>(minimumEnd))
            << "\nSuggested aligned TBL override end: 0x" << Hex8(suggestedEnd)
            << "\n\nThe Expert override is enabled. Increase its verified range, or disable it to let AKI Sound Studio relocate the bank automatically.";
        error = out.str();
        return false;
    }

    if (rebuiltTbl.empty()) {
        error = "The rebuilt TBL is empty.";
        return false;
    }
    if (static_cast<uint64_t>(allocation.waveStartOffset) + rebuiltTbl.size() > rom.z64.size()) {
        error = "The rebuilt TBL would write outside the ROM.";
        return false;
    }

    // Match N64 Sound Tool's practical model: rebuild the bank-local TBL,
    // rewrite every wave offset/length in the existing CTL, then inject the
    // rebuilt TBL back into the bank's allocated ROM range.
    auto fillBegin = rom.z64.begin() + static_cast<std::ptrdiff_t>(allocation.waveStartOffset);
    auto fillEnd = rom.z64.begin() + static_cast<std::ptrdiff_t>(allowedWaveEnd);
    // Clear only the exact allowed TBL extent. In normal mode that is the
    // original waveform-data extent, not the gap to the next CTL. Expert mode
    // may deliberately extend this range after the blank/force checks above.
    std::fill(fillBegin, fillEnd, 0);
    std::copy(rebuiltTbl.begin(), rebuiltTbl.end(), fillBegin);

    for (const size_t index : bankIndices) {
        auto& item = rom.sounds[index];
        const uint32_t relative = newRelativeOffsets[index];
        const uint32_t length = static_cast<uint32_t>(waveBytes[index].size());
        if (item.controlRecordOffset + 8 > rom.z64.size()) {
            error = "A control record lies outside the ROM while writing the rebuilt bank.";
            return false;
        }
        WriteBe32(rom.z64, item.controlRecordOffset, relative);
        WriteBe32(rom.z64, item.controlRecordOffset + 4, length);
        item.waveDataOffset = allocation.waveStartOffset + relative;
        item.encodedBytes = length;
    }

    if (loopPlan.enabled) {
        const uint32_t loopRelative =
            loopPlan.controlOffset - bank->controlOffset;
        WriteBe32(rom.z64, sound.controlRecordOffset + 0x0C, loopRelative);
        WriteBe32(rom.z64, loopPlan.controlOffset, loopPlan.start);
        WriteBe32(rom.z64, loopPlan.controlOffset + 4U, loopPlan.end);
        WriteBe32(rom.z64, loopPlan.controlOffset + 8U, loopPlan.count);
        for (size_t stateIndex = 0;
             stateIndex < loopPlan.state.size();
             ++stateIndex) {
            WriteBe16(
                rom.z64,
                loopPlan.controlOffset + 0x0CU +
                    static_cast<uint32_t>(stateIndex) * 2U,
                static_cast<uint16_t>(loopPlan.state[stateIndex]));
        }
        sound.loopControlOffset = loopPlan.controlOffset;
        sound.loopStart = loopPlan.start;
        sound.loopEnd = loopPlan.end;
        sound.loopCount = loopPlan.count;
        sound.loopState = loopPlan.state;
    } else {
        // No two-point loop was supplied by the replacement WAV. Clear the old
        // loop pointer instead of leaving stale loop positions/state attached
        // to a different waveform.
        WriteBe32(rom.z64, sound.controlRecordOffset + 0x0C, 0);
        sound.loopControlOffset = 0;
        sound.loopStart = 0;
        sound.loopEnd = 0;
        sound.loopCount = 0;
        sound.loopState.fill(0);
    }

    sound.replacementEncoded = std::move(encoded);
    sound.replacementSampleRate = wav.sampleRate;
    sound.replacementPcmSamples = static_cast<uint32_t>(wav.monoSamples.size());
    sound.modified = true;
    return true;
}

bool RepairN64Crc6102(std::vector<uint8_t>& z64,
                      uint32_t& crc1,
                      uint32_t& crc2,
                      std::string& error) {
    error.clear();
    crc1 = 0;
    crc2 = 0;
    if (z64.size() < 0x101000) {
        error =
            "The ROM is too small for the standard N64 checksum region.";
        return false;
    }

    constexpr uint32_t seed = 0xF8CA4DDCU;
    uint32_t t1 = seed;
    uint32_t t2 = seed;
    uint32_t t3 = seed;
    uint32_t t4 = seed;
    uint32_t t5 = seed;
    uint32_t t6 = seed;

    try {
        for (size_t offset = 0x1000;
             offset < 0x101000;
             offset += 4) {
            const uint32_t value = ReadBe32(z64, offset);
            const uint32_t sum = t6 + value;
            if (sum < t6) ++t4;
            t6 = sum;
            t3 ^= value;
            const uint32_t rotated =
                RotateLeft32(value, value & 0x1FU);
            t5 += rotated;
            if (t2 > value) {
                t2 ^= rotated;
            } else {
                t2 ^= t6 ^ value;
            }
            t1 += t5 ^ value;
        }

        crc1 = t6 ^ t4 ^ t3;
        crc2 = t5 ^ t2 ^ t1;
        WriteBe32(z64, 0x10, crc1);
        WriteBe32(z64, 0x14, crc2);
    } catch (const std::exception& ex) {
        error = std::string("N64 checksum repair failed: ") + ex.what();
        return false;
    }
    return true;
}

bool SaveRomZ64(LoadedRom& rom,
                const std::filesystem::path& path,
                uint32_t& crc1,
                uint32_t& crc2,
                std::string& error) {
    if (!RepairN64Crc6102(rom.z64, crc1, crc2, error)) {
        return false;
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "Could not create output ROM: " + path.string();
        return false;
    }
    output.write(
        reinterpret_cast<const char*>(rom.z64.data()),
        static_cast<std::streamsize>(rom.z64.size()));
    if (!output) {
        error = "Failed while writing the output ROM.";
        return false;
    }
    rom.sha1 = Sha1Hex(rom.z64);
    return true;
}



namespace {

std::string PcmSha1(const std::vector<int16_t>& samples) {
    Sha1 sha;
    for (const int16_t sample : samples) {
        const uint8_t bytes[2]{
            static_cast<uint8_t>(sample & 0xFF),
            static_cast<uint8_t>((static_cast<uint16_t>(sample) >> 8) & 0xFF),
        };
        sha.update(bytes, 2);
    }
    return sha.finish();
}

const SoundRecord* FindSoundRecord(const LoadedRom& rom, uint16_t bankId, uint16_t soundId) {
    for (const auto& sound : rom.sounds) {
        if (sound.bankId == bankId && sound.soundId == soundId) return &sound;
    }
    return nullptr;
}

SoundRecord* FindSoundRecord(LoadedRom& rom, uint16_t bankId, uint16_t soundId) {
    for (auto& sound : rom.sounds) {
        if (sound.bankId == bankId && sound.soundId == soundId) return &sound;
    }
    return nullptr;
}

uint32_t EffectiveRate(const LoadedRom& rom, const SoundRecord& sound) {
    if (sound.label.rate.primaryHz && *sound.label.rate.primaryHz != 0) {
        return *sound.label.rate.primaryHz;
    }
    if (sound.replacementSampleRate != 0) return sound.replacementSampleRate;
    return rom.profile && rom.profile->mixerRateHz ? rom.profile->mixerRateHz : 22050U;
}


void PatchMipsAddress(std::vector<uint8_t>& rom,
                      const AsmBankPointerReference& ref,
                      uint32_t address) {
    if (static_cast<uint64_t>(ref.upperInstructionOffset) + 4 > rom.size() ||
        static_cast<uint64_t>(ref.lowerInstructionOffset) + 4 > rom.size()) return;
    uint32_t first = ReadBe32(rom, ref.upperInstructionOffset);
    uint32_t second = ReadBe32(rom, ref.lowerInstructionOffset);
    const uint16_t low = static_cast<uint16_t>(address & 0xFFFFU);
    const uint16_t high = ref.usesAddiu
        ? static_cast<uint16_t>((address + 0x8000U) >> 16)
        : static_cast<uint16_t>(address >> 16);
    first = (first & 0xFFFF0000U) | high;
    second = (second & 0xFFFF0000U) | low;
    WriteBe32(rom, ref.upperInstructionOffset, first);
    WriteBe32(rom, ref.lowerInstructionOffset, second);
}

bool RelocateBankForExpansion(LoadedRom& rom,
                              uint16_t bankId,
                              uint32_t extraCtlBytes,
                              uint32_t extraWaveBytes,
                              std::string& error) {
    if (!rom.profile || rom.profile != &rom.customProfile) {
        if (!rom.profile) { error = "No profile selected."; return false; }
        rom.customProfile = *rom.profile;
        rom.profile = &rom.customProfile;
    }
    BankDefinition* bank = nullptr;
    for (auto& candidate : rom.customProfile.banks) {
        if (candidate.bankId == bankId) { bank = &candidate; break; }
    }
    if (!bank) { error = "Bank definition not found for relocation."; return false; }
    BankAllocation allocation;
    if (!ComputeBankAllocation(rom, *bank, allocation, error)) return false;
    const uint32_t oldCtl = bank->controlOffset;
    const uint32_t oldTbl = bank->waveOffset;
    const uint32_t ctlBytes = oldTbl - oldCtl;
    const uint32_t tblBytes = allocation.normalWaveEndOffset - oldTbl;
    const uint64_t newCtl64 = Align16Size(rom.z64.size());
    const uint64_t newTbl64 = Align16Size(newCtl64 + ctlBytes + Align16(extraCtlBytes) + 0x100U);
    const uint64_t end64 = newTbl64 + tblBytes + Align16(extraWaveBytes) + 0x100U;
    constexpr uint64_t kMaxRom = 64ULL * 1024ULL * 1024ULL;
    if (end64 > kMaxRom) {
        error = "Relocating this bank for expansion would exceed the 64 MiB N64 ROM limit.";
        return false;
    }
    const uint32_t newCtl = static_cast<uint32_t>(newCtl64);
    const uint32_t newTbl = static_cast<uint32_t>(newTbl64);
    const size_t oldSize = rom.z64.size();
    rom.z64.resize(static_cast<size_t>(end64), 0);
    std::copy_n(rom.z64.begin() + oldCtl, ctlBytes, rom.z64.begin() + newCtl);
    std::copy_n(rom.z64.begin() + oldTbl, tblBytes, rom.z64.begin() + newTbl);

    std::vector<BankTraceResult> traces;
    std::string traceError;
    if (TraceSoundBankAsmPointers(rom, traces, traceError)) {
        for (const auto& trace : traces) {
            if (trace.bankId != bankId) continue;
            for (const auto& ref : trace.controlReferences) PatchMipsAddress(rom.z64, ref, newCtl);
            for (const auto& ref : trace.waveReferences) PatchMipsAddress(rom.z64, ref, newTbl);
        }
    }
    const int64_t ctlDelta = static_cast<int64_t>(newCtl) - oldCtl;
    const int64_t tblDelta = static_cast<int64_t>(newTbl) - oldTbl;
    for (auto& sound : rom.sounds) {
        if (sound.bankId != bankId) continue;
        sound.controlRecordOffset = static_cast<uint32_t>(static_cast<int64_t>(sound.controlRecordOffset) + ctlDelta);
        if (sound.loopControlOffset) sound.loopControlOffset = static_cast<uint32_t>(static_cast<int64_t>(sound.loopControlOffset) + ctlDelta);
        sound.waveDataOffset = static_cast<uint32_t>(static_cast<int64_t>(sound.waveDataOffset) + tblDelta);
    }
    bank->controlOffset = newCtl;
    bank->waveOffset = newTbl;
    (void)oldSize;
    return true;
}

bool FindBlankCtlRegion(const LoadedRom& rom,
                        const BankDefinition& bank,
                        uint32_t bytesNeeded,
                        uint32_t& absoluteOffset) {
    if (bytesNeeded == 0 || bank.controlOffset >= bank.waveOffset || bank.waveOffset > rom.z64.size()) return false;
    std::vector<ControlInterval> occupied;
    occupied.push_back({bank.controlOffset, std::min<uint32_t>(bank.controlOffset + 0x30U, bank.waveOffset)});
    try {
        const uint32_t count = ReadBe32(rom.z64, bank.controlOffset + 0x20);
        const uint32_t coarseRel = ReadBe32(rom.z64, bank.controlOffset + 0x24);
        const uint32_t fineRel = ReadBe32(rom.z64, bank.controlOffset + 0x28);
        const uint32_t ptrRel = ReadBe32(rom.z64, bank.controlOffset + 0x2C);
        if (coarseRel) occupied.push_back({bank.controlOffset + coarseRel, bank.controlOffset + coarseRel + count});
        if (fineRel) occupied.push_back({bank.controlOffset + fineRel, bank.controlOffset + fineRel + count * 4U});
        occupied.push_back({bank.controlOffset + ptrRel, bank.controlOffset + ptrRel + count * 4U});
        for (const auto& sound : rom.sounds) {
            if (sound.bankId != bank.bankId) continue;
            occupied.push_back({sound.controlRecordOffset, sound.controlRecordOffset + 0x18U});
            if (sound.loopControlOffset) occupied.push_back({sound.loopControlOffset, sound.loopControlOffset + 0x2CU});
            const uint32_t bookRel = ReadBe32(rom.z64, sound.controlRecordOffset + 0x10);
            const uint32_t book = bank.controlOffset + bookRel;
            const uint32_t bookBytes = 8U + sound.predictorOrder * sound.predictorCount * 16U;
            occupied.push_back({book, book + bookBytes});
        }
    } catch (...) {
        return false;
    }
    uint32_t candidate = Align16(bank.controlOffset + 0x30U);
    for (; static_cast<uint64_t>(candidate) + bytesNeeded <= bank.waveOffset; candidate += 4U) {
        const uint32_t end = candidate + bytesNeeded;
        if (IntervalsOverlap(candidate, end, occupied)) continue;
        bool blank = true;
        for (uint32_t o = candidate; o < end; ++o) {
            if (!IsBlankRomByte(rom.z64[o])) { blank = false; break; }
        }
        if (blank) { absoluteOffset = candidate; return true; }
    }
    return false;
}

} // namespace

bool BuildWaveformIdentities(const LoadedRom& rom,
                             std::vector<WaveformIdentity>& identities,
                             std::string& error) {
    identities.clear();
    error.clear();
    for (const auto& sound : rom.sounds) {
        std::string decodeError;
        auto pcm = DecodeSelectedSound(rom, sound, decodeError);
        if (!decodeError.empty() && sound.encodedBytes >= 9U && (sound.encodedBytes % 9U) != 0U) {
            // A few AKI tail records include non-frame trailer bytes. The game
            // decodes complete 9-byte VADPCM frames and ignores the remainder.
            SoundRecord completeFrames = sound;
            completeFrames.encodedBytes -= completeFrames.encodedBytes % 9U;
            decodeError.clear();
            pcm = DecodeSelectedSound(rom, completeFrames, decodeError);
        }
        if (!decodeError.empty()) {
            error = "Could not decode Bank " + Hex4(sound.bankId) + " / " + Hex4(sound.soundId) + ": " + decodeError;
            identities.clear();
            return false;
        }
        WaveformIdentity identity;
        identity.bankId = sound.bankId;
        identity.soundId = sound.soundId;
        identity.decodedSamples = static_cast<uint32_t>(pcm.size());
        identity.pcmSha1 = PcmSha1(pcm);
        identities.push_back(std::move(identity));
    }
    return true;
}

bool FindDuplicateWaveforms(const LoadedRom& rom,
                            std::vector<DuplicateGroup>& groups,
                            std::string& error) {
    groups.clear();
    std::vector<WaveformIdentity> identities;
    if (!BuildWaveformIdentities(rom, identities, error)) return false;
    std::map<std::pair<std::string,uint32_t>, std::vector<WaveformIdentity>> grouped;
    for (const auto& identity : identities) grouped[{identity.pcmSha1, identity.decodedSamples}].push_back(identity);
    for (auto& [key, members] : grouped) {
        if (members.size() < 2) continue;
        DuplicateGroup group;
        group.pcmSha1 = key.first;
        group.decodedSamples = key.second;
        group.members = std::move(members);
        groups.push_back(std::move(group));
    }
    std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) {
        if (a.members.size() != b.members.size()) return a.members.size() > b.members.size();
        return a.pcmSha1 < b.pcmSha1;
    });
    return true;
}

bool MatchExactWaveforms(const LoadedRom& source,
                         const LoadedRom& target,
                         std::vector<SoundMatch>& matches,
                         std::string& error) {
    matches.clear();
    std::vector<WaveformIdentity> left, right;
    if (!BuildWaveformIdentities(source, left, error)) return false;
    if (!BuildWaveformIdentities(target, right, error)) return false;
    std::multimap<std::pair<std::string,uint32_t>, WaveformIdentity> targetMap;
    for (const auto& id : right) targetMap.emplace(std::make_pair(id.pcmSha1,id.decodedSamples), id);
    for (const auto& src : left) {
        auto range = targetMap.equal_range({src.pcmSha1,src.decodedSamples});
        for (auto it = range.first; it != range.second; ++it) {
            SoundMatch match;
            match.sourceBankId = src.bankId;
            match.sourceSoundId = src.soundId;
            match.targetBankId = it->second.bankId;
            match.targetSoundId = it->second.soundId;
            match.pcmSha1 = src.pcmSha1;
            match.decodedSamples = src.decodedSamples;
            matches.push_back(std::move(match));
        }
    }
    return true;
}

bool TraceSoundBankAsmPointers(const LoadedRom& rom,
                               std::vector<BankTraceResult>& traces,
                               std::string& error) {
    traces.clear();
    error.clear();
    if (!rom.profile) { error = "No profile is selected."; return false; }
    std::map<uint32_t,std::vector<AsmBankPointerReference>> refs;
    for (uint32_t o = 0; static_cast<uint64_t>(o) + 8 <= rom.z64.size(); o += 4) {
        const uint32_t first = ReadBe32(rom.z64, o);
        if ((first >> 26) != 0x0F) continue; // LUI
        const uint8_t rt = static_cast<uint8_t>((first >> 16) & 0x1F);
        const uint32_t upper = (first & 0xFFFFU) << 16;
        for (uint32_t delta : {4U, 8U, 12U}) {
            if (static_cast<uint64_t>(o) + delta + 4 > rom.z64.size()) continue;
            const uint32_t second = ReadBe32(rom.z64, o + delta);
            const uint8_t op = static_cast<uint8_t>(second >> 26);
            const uint8_t rs = static_cast<uint8_t>((second >> 21) & 0x1F);
            const uint8_t rt2 = static_cast<uint8_t>((second >> 16) & 0x1F);
            if (rs != rt || rt2 != rt || (op != 0x09 && op != 0x0D)) continue;
            uint32_t resolved = 0;
            if (op == 0x09) resolved = upper + static_cast<int16_t>(second & 0xFFFFU);
            else resolved = upper | (second & 0xFFFFU);
            AsmBankPointerReference ref;
            ref.upperInstructionOffset = o;
            ref.lowerInstructionOffset = o + delta;
            ref.resolvedAddress = resolved;
            ref.targetRegister = rt;
            ref.usesAddiu = op == 0x09;
            refs[resolved].push_back(ref);
        }
    }
    for (const auto& bank : rom.profile->banks) {
        BankTraceResult trace;
        trace.bankId = bank.bankId;
        trace.controlOffset = bank.controlOffset;
        trace.waveOffset = bank.waveOffset;
        try { trace.soundCount = ReadBe32(rom.z64, bank.controlOffset + 0x20); } catch (...) {}
        trace.controlReferences = refs[bank.controlOffset];
        trace.waveReferences = refs[bank.waveOffset];
        traces.push_back(std::move(trace));
    }
    return true;
}

bool AppendSoundFromWav(LoadedRom& rom,
                        uint16_t bankId,
                        uint16_t templateSoundId,
                        const WavPcm16& wav,
                        const BankWriteOptions& options,
                        BankExpansionResult& result,
                        std::string& error) {
    result = {};
    error.clear();
    if (!rom.profile) { error = "No profile is selected."; return false; }
    const BankDefinition* bank = FindBankDefinition(rom, bankId);
    SoundRecord* templ = FindSoundRecord(rom, bankId, templateSoundId);
    if (!bank || !templ) { error = "The selected bank or template sound does not exist."; return false; }
    const uint32_t oldCount = ReadBe32(rom.z64, bank->controlOffset + 0x20);
    if (oldCount >= 0xFFFFU) { error = "The bank sound count cannot be expanded further."; return false; }
    const uint32_t newCount = oldCount + 1U;
    const uint32_t oldCoarseRel = ReadBe32(rom.z64, bank->controlOffset + 0x24);
    const uint32_t oldFineRel = ReadBe32(rom.z64, bank->controlOffset + 0x28);
    const uint32_t oldPtrRel = ReadBe32(rom.z64, bank->controlOffset + 0x2C);
    const uint32_t bookBytes = 8U + templ->predictorOrder * templ->predictorCount * 16U;
    const bool needsLoop = wav.hasLoop;
    const uint32_t coarseBytes = newCount;
    const uint32_t fineBytes = newCount * 4U;
    const uint32_t ptrBytes = newCount * 4U;
    uint32_t total = 0;
    const uint32_t coarseAt = total; total += Align16(coarseBytes);
    const uint32_t fineAt = total; total += Align16(fineBytes);
    const uint32_t ptrAt = total; total += Align16(ptrBytes);
    const uint32_t recordAt = total; total += Align16(0x18U);
    const uint32_t bookAt = total; total += Align16(bookBytes);
    const uint32_t loopAt = total; if (needsLoop) total += Align16(0x2CU);
    uint32_t block = 0;
    if (!FindBlankCtlRegion(rom, *bank, total, block)) {
        const uint32_t extraWaveBytes = static_cast<uint32_t>(((wav.monoSamples.size() + 15U) / 16U) * 9U + 0x1000U);
        if (!RelocateBankForExpansion(rom, bankId, total, extraWaveBytes, error)) return false;
        bank = FindBankDefinition(rom, bankId);
        templ = FindSoundRecord(rom, bankId, templateSoundId);
        if (!bank || !templ || !FindBlankCtlRegion(rom, *bank, total, block)) {
            error = "The bank was relocated, but the expanded metadata block could not be allocated.";
            return false;
        }
    }
    const uint32_t coarseAbs = block + coarseAt;
    const uint32_t fineAbs = block + fineAt;
    const uint32_t ptrAbs = block + ptrAt;
    const uint32_t recordAbs = block + recordAt;
    const uint32_t bookAbs = block + bookAt;
    const uint32_t loopAbs = needsLoop ? block + loopAt : 0;
    std::fill(rom.z64.begin() + block, rom.z64.begin() + block + total, 0);
    if (oldCoarseRel) std::copy_n(rom.z64.begin() + bank->controlOffset + oldCoarseRel, oldCount, rom.z64.begin() + coarseAbs);
    if (oldFineRel) std::copy_n(rom.z64.begin() + bank->controlOffset + oldFineRel, oldCount * 4U, rom.z64.begin() + fineAbs);
    std::copy_n(rom.z64.begin() + bank->controlOffset + oldPtrRel, oldCount * 4U, rom.z64.begin() + ptrAbs);
    rom.z64[coarseAbs + oldCount] = static_cast<uint8_t>(templ->coarseTuneSemitones & 0xFF);
    rom.z64[fineAbs + oldCount * 4U] = static_cast<uint8_t>(templ->fineTuneCents & 0xFF);
    WriteBe32(rom.z64, ptrAbs + oldCount * 4U, recordAbs - bank->controlOffset);
    std::copy_n(rom.z64.begin() + templ->controlRecordOffset, 0x18U, rom.z64.begin() + recordAbs);
    const uint32_t templateBookRel = ReadBe32(rom.z64, templ->controlRecordOffset + 0x10);
    std::copy_n(rom.z64.begin() + bank->controlOffset + templateBookRel, bookBytes, rom.z64.begin() + bookAbs);
    WriteBe32(rom.z64, recordAbs + 0x10, bookAbs - bank->controlOffset);
    WriteBe32(rom.z64, recordAbs + 0x0C, needsLoop ? loopAbs - bank->controlOffset : 0U);
    WriteBe32(rom.z64, bank->controlOffset + 0x20, newCount);
    WriteBe32(rom.z64, bank->controlOffset + 0x24, coarseAbs - bank->controlOffset);
    WriteBe32(rom.z64, bank->controlOffset + 0x28, fineAbs - bank->controlOffset);
    WriteBe32(rom.z64, bank->controlOffset + 0x2C, ptrAbs - bank->controlOffset);

    SoundRecord added = *templ;
    added.soundId = static_cast<uint16_t>(oldCount);
    added.controlRecordOffset = recordAbs;
    added.loopControlOffset = loopAbs;
    added.label = {};
    added.modified = false;
    added.replacementEncoded.clear();
    rom.sounds.push_back(added);
    SoundRecord* newSound = FindSoundRecord(rom, bankId, static_cast<uint16_t>(oldCount));
    ReplacementResult replacement;
    if (!newSound || !ReplaceSoundPcm(rom, *newSound, wav, options, replacement, error)) {
        return false;
    }
    result.bankId = bankId;
    result.newSoundId = static_cast<uint16_t>(oldCount);
    result.oldSoundCount = oldCount;
    result.newSoundCount = newCount;
    result.relocatedCoarseTableOffset = coarseAbs;
    result.relocatedFineTableOffset = fineAbs;
    result.relocatedPointerTableOffset = ptrAbs;
    result.newControlRecordOffset = recordAbs;
    result.newPredictorBookOffset = bookAbs;
    result.newLoopOffset = loopAbs;
    result.replacement = replacement;
    return true;
}

bool MigrateSoundToSlot(const LoadedRom& sourceRom,
                        const SoundRecord& sourceSound,
                        LoadedRom& targetRom,
                        SoundRecord& targetSound,
                        const MigrationOptions& options,
                        MigrationResult& result,
                        std::string& error) {
    result = {};
    error.clear();
    std::string decodeError;
    auto pcm = DecodeSelectedSound(sourceRom, sourceSound, decodeError);
    if (!decodeError.empty()) { error = decodeError; return false; }
    WavPcm16 wav;
    wav.sampleRate = EffectiveRate(sourceRom, sourceSound);
    wav.sourceChannels = 1;
    wav.monoSamples = std::move(pcm);
    if (sourceSound.loopEnd > sourceSound.loopStart && sourceSound.loopEnd <= wav.monoSamples.size()) {
        wav.loopMetadataPresent = true;
        wav.hasLoop = true;
        wav.loopStart = sourceSound.loopStart;
        wav.loopEnd = sourceSound.loopEnd;
        wav.loopCount = sourceSound.loopCount;
    }
    result.sourceRateHz = wav.sampleRate;
    result.targetRateHz = EffectiveRate(targetRom, targetSound);
    if (options.resampleToTargetRate && result.targetRateHz && wav.sampleRate != result.targetRateHz) {
        WavPcm16 converted;
        if (!ResampleWavPcm16(wav, result.targetRateHz, converted, error)) return false;
        wav = std::move(converted);
        result.resampled = true;
    }
    if (!ApplyWavGain(wav, options.gainDb, options.preventClipping, result.gain, error)) return false;
    if (!ReplaceSoundPcm(targetRom, targetSound, wav, options.bankWrite, result.replacement, error)) {
        if (error.find("TBL is too large") == std::string::npos || options.bankWrite.enableSizeOverride) return false;
        const uint16_t bankId = targetSound.bankId;
        const uint16_t soundId = targetSound.soundId;
        const uint32_t extraWaveBytes = static_cast<uint32_t>(((wav.monoSamples.size() + 15U) / 16U) * 9U + 0x2000U);
        std::string relocateError;
        if (!RelocateBankForExpansion(targetRom, bankId, 0x100U, extraWaveBytes, relocateError)) {
            error += " Automatic bank relocation also failed: " + relocateError;
            return false;
        }
        SoundRecord* relocatedTarget = FindSoundRecord(targetRom, bankId, soundId);
        if (!relocatedTarget || !ReplaceSoundPcm(targetRom, *relocatedTarget, wav, options.bankWrite, result.replacement, error)) return false;
    }
    return true;
}

bool ExportWaveformAnalysisCsv(const LoadedRom& rom,
                               const std::filesystem::path& path,
                               std::string& error) {
    std::vector<WaveformIdentity> identities;
    if (!BuildWaveformIdentities(rom, identities, error)) return false;
    std::map<std::pair<std::string,uint32_t>, size_t> counts;
    for (const auto& i : identities) ++counts[{i.pcmSha1,i.decodedSamples}];
    std::ofstream out(path, std::ios::binary);
    if (!out) { error = "Could not create waveform-analysis CSV."; return false; }
    out << "bank,id,name,decoded_samples,pcm_sha1,duplicate_count\n";
    for (const auto& i : identities) {
        const auto* sound = FindSoundRecord(rom, i.bankId, i.soundId);
        out << "0x" << Hex4(i.bankId) << ",0x" << Hex4(i.soundId) << ","
            << CsvEscape(sound ? sound->label.name : std::string{}) << ","
            << i.decodedSamples << "," << i.pcmSha1 << ","
            << counts[{i.pcmSha1,i.decodedSamples}] << "\n";
    }
    if (!out) { error = "Failed while writing waveform-analysis CSV."; return false; }
    return true;
}

bool AutoDetectSoundBankLocations(LoadedRom& rom, std::string& error) {
    error.clear();
    if (!rom.profile) {
        error = "No AKI game profile is selected.";
        return false;
    }
    rom.customProfile = *rom.profile;
    rom.profile = &rom.customProfile;

    const auto candidates = FindPtrTableCandidates(rom.z64);
    const auto waveCandidates = FindWaveTableCandidates(rom.z64);
    if (candidates.empty() || waveCandidates.empty()) {
        error = "No complete PtrTablesV2/WaveTables bank pairs were found.";
        return false;
    }

    std::set<uint32_t> usedControls;
    std::set<uint32_t> usedWaves;
    for (auto& bank : rom.customProfile.banks) {
        const uint32_t expectedCount = ExpectedSoundCount(rom.customProfile.id, bank.bankId);
        const int64_t sequenceDelta = bank.sequenceObjectOffset == 0
            ? 0
            : static_cast<int64_t>(bank.sequenceObjectOffset) - bank.controlOffset;
        uint32_t bestControl = 0;
        uint32_t bestWave = 0;
        uint64_t bestDistance = std::numeric_limits<uint64_t>::max();
        for (const uint32_t control : candidates) {
            if (usedControls.count(control)) continue;
            if (ReadBe32(rom.z64, control + 0x20) != expectedCount) continue;
            for (const uint32_t wave : waveCandidates) {
                if (usedWaves.count(wave) || wave <= control) continue;
                if (!LooksLikeAkiBankAt(rom.z64, control, expectedCount, wave)) continue;
                const uint64_t distance = static_cast<uint64_t>(wave) - control;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestControl = control;
                    bestWave = wave;
                }
            }
        }
        if (bestControl == 0 || bestWave == 0) {
            error = "Could not auto-detect a structurally valid CTL/TBL pair for bank " +
                    Hex4(bank.bankId) + " with " + std::to_string(expectedCount) + " sounds.";
            return false;
        }
        usedControls.insert(bestControl);
        usedWaves.insert(bestWave);
        const uint32_t oldControl = bank.controlOffset;
        bank.controlOffset = bestControl;
        bank.waveOffset = bestWave;
        if (bank.sequenceObjectOffset != 0) {
            const int64_t guessedSequence = static_cast<int64_t>(bestControl) + sequenceDelta;
            bank.sequenceObjectOffset = guessedSequence > 0 && guessedSequence < static_cast<int64_t>(rom.z64.size())
                ? static_cast<uint32_t>(guessedSequence) : 0;
        }
        (void)oldControl;
    }
    return true;
}

bool ImportMetadataCsv(LoadedRom& rom,
                       const std::filesystem::path& path,
                       std::string& error) {
    error.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open metadata CSV: " + path.string();
        return false;
    }
    std::string line;
    if (!std::getline(input, line)) {
        error = "The metadata CSV is empty.";
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto header = ParseCsvLine(line);
    const int bankColumn = FindHeaderIndex(header, "bank");
    const int idColumn = FindHeaderIndex(header, "id");
    const int nameColumn = FindHeaderIndex(header, "name");
    const int rateColumn = FindHeaderIndex(header, "rate_hz");
    const int confidenceColumn = FindHeaderIndex(header, "confidence");
    const int methodColumn = FindHeaderIndex(header, "method");
    int noteColumn = FindHeaderIndex(header, "notes");
    if (noteColumn < 0) noteColumn = FindHeaderIndex(header, "note");
    if (bankColumn < 0 || idColumn < 0) {
        error = "The metadata CSV must contain bank and id columns.";
        return false;
    }

    size_t applied = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (Trim(line).empty()) continue;
        const auto fields = ParseCsvLine(line);
        const uint16_t bank = static_cast<uint16_t>(ParseHex(FieldOrEmpty(fields, bankColumn)));
        const uint16_t id = static_cast<uint16_t>(ParseHex(FieldOrEmpty(fields, idColumn)));
        auto it = std::find_if(rom.sounds.begin(), rom.sounds.end(), [&](const SoundRecord& sound) {
            return sound.bankId == bank && sound.soundId == id;
        });
        if (it == rom.sounds.end()) continue;
        if (nameColumn >= 0) it->label.name = FieldOrEmpty(fields, nameColumn);
        const std::string rate = Trim(FieldOrEmpty(fields, rateColumn));
        if (!rate.empty()) it->label.rate.primaryHz = static_cast<uint32_t>(std::stoul(rate));
        else it->label.rate.primaryHz.reset();
        if (confidenceColumn >= 0) it->label.rate.confidence = ParseConfidenceText(FieldOrEmpty(fields, confidenceColumn));
        if (methodColumn >= 0) it->label.rate.method = FieldOrEmpty(fields, methodColumn);
        if (noteColumn >= 0) it->label.rate.note = FieldOrEmpty(fields, noteColumn);
        ++applied;
    }
    if (applied == 0) {
        error = "No metadata rows matched the loaded ROM's bank/id list.";
        return false;
    }
    return true;
}

bool ExportHackProfileCsv(const LoadedRom& rom,
                          const std::filesystem::path& path,
                          std::string& error) {
    error.clear();
    if (!rom.profile) {
        error = "No ROM profile is loaded.";
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "Could not create hack profile CSV: " + path.string();
        return false;
    }
    out << "kind,game_code,bank,id,name,rate_hz,confidence,method,control_offset,wave_offset,sequence_offset,description,note\r\n";
    for (const auto& bank : rom.profile->banks) {
        out << "bank," << CsvEscape(rom.profile->gameCode) << ','
            << Hex4(bank.bankId) << ",,,,,,0x" << Hex8(bank.controlOffset)
            << ",0x" << Hex8(bank.waveOffset)
            << ",0x" << Hex8(bank.sequenceObjectOffset)
            << ',' << CsvEscape(bank.description) << ",\r\n";
    }
    for (const auto& sound : rom.sounds) {
        out << "sound," << CsvEscape(rom.profile->gameCode) << ','
            << Hex4(sound.bankId) << ',' << Hex4(sound.soundId) << ','
            << CsvEscape(sound.label.name) << ',';
        if (sound.label.rate.primaryHz) out << *sound.label.rate.primaryHz;
        out << ',' << CsvEscape(RateConfidenceText(sound.label.rate.confidence))
            << ',' << CsvEscape(sound.label.rate.method)
            << ",,,,," << CsvEscape(sound.label.rate.note) << "\r\n";
    }
    if (!out) {
        error = "Failed while writing the hack profile CSV.";
        return false;
    }
    return true;
}

bool ImportHackProfileCsv(LoadedRom& rom,
                          const std::filesystem::path& path,
                          std::string& error) {
    error.clear();
    if (!rom.profile) {
        error = "No ROM profile is loaded.";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open hack profile CSV: " + path.string();
        return false;
    }
    std::string line;
    if (!std::getline(input, line)) {
        error = "The hack profile CSV is empty.";
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto header = ParseCsvLine(line);
    const int kindColumn = FindHeaderIndex(header, "kind");
    const int gameColumn = FindHeaderIndex(header, "game_code");
    const int bankColumn = FindHeaderIndex(header, "bank");
    const int idColumn = FindHeaderIndex(header, "id");
    const int nameColumn = FindHeaderIndex(header, "name");
    const int rateColumn = FindHeaderIndex(header, "rate_hz");
    const int confidenceColumn = FindHeaderIndex(header, "confidence");
    const int methodColumn = FindHeaderIndex(header, "method");
    const int controlColumn = FindHeaderIndex(header, "control_offset");
    const int waveColumn = FindHeaderIndex(header, "wave_offset");
    const int sequenceColumn = FindHeaderIndex(header, "sequence_offset");
    const int descriptionColumn = FindHeaderIndex(header, "description");
    int noteColumn = FindHeaderIndex(header, "note");
    if (noteColumn < 0) noteColumn = FindHeaderIndex(header, "notes");
    if (kindColumn < 0 || bankColumn < 0) {
        error = "The hack profile CSV must contain kind and bank columns.";
        return false;
    }

    struct PendingLabel {
        uint16_t bank = 0;
        uint16_t id = 0;
        SoundLabel label;
        bool hasRate = false;
    };
    std::vector<PendingLabel> labels;
    bool bankLocationsChanged = false;

    rom.customProfile = *rom.profile;
    rom.profile = &rom.customProfile;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (Trim(line).empty()) continue;
        const auto fields = ParseCsvLine(line);
        const std::string rowGame = Trim(FieldOrEmpty(fields, gameColumn));
        if (!rowGame.empty() && rowGame != rom.profile->gameCode) continue;
        std::string kind = Trim(FieldOrEmpty(fields, kindColumn));
        std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        const uint16_t bankId = static_cast<uint16_t>(ParseHex(FieldOrEmpty(fields, bankColumn)));
        if (kind == "bank") {
            auto bankIt = std::find_if(rom.customProfile.banks.begin(), rom.customProfile.banks.end(), [&](const BankDefinition& bank) {
                return bank.bankId == bankId;
            });
            if (bankIt == rom.customProfile.banks.end()) continue;
            const uint32_t control = ParseHexOffsetFlexible(FieldOrEmpty(fields, controlColumn));
            const uint32_t wave = ParseHexOffsetFlexible(FieldOrEmpty(fields, waveColumn));
            const uint32_t sequence = ParseHexOffsetFlexible(FieldOrEmpty(fields, sequenceColumn));
            if (control != 0 && wave != 0) {
                bankIt->controlOffset = control;
                bankIt->waveOffset = wave;
                bankIt->sequenceObjectOffset = sequence;
                bankLocationsChanged = true;
            }
            if (descriptionColumn >= 0) bankIt->description = FieldOrEmpty(fields, descriptionColumn);
        } else if (kind == "sound") {
            const std::string idText = FieldOrEmpty(fields, idColumn);
            if (Trim(idText).empty()) continue;
            PendingLabel pending;
            pending.bank = bankId;
            pending.id = static_cast<uint16_t>(ParseHex(idText));
            if (nameColumn >= 0) pending.label.name = FieldOrEmpty(fields, nameColumn);
            const std::string rate = Trim(FieldOrEmpty(fields, rateColumn));
            if (!rate.empty()) {
                pending.label.rate.primaryHz = static_cast<uint32_t>(std::stoul(rate));
                pending.hasRate = true;
            }
            if (confidenceColumn >= 0) pending.label.rate.confidence = ParseConfidenceText(FieldOrEmpty(fields, confidenceColumn));
            if (methodColumn >= 0) pending.label.rate.method = FieldOrEmpty(fields, methodColumn);
            if (noteColumn >= 0) pending.label.rate.note = FieldOrEmpty(fields, noteColumn);
            labels.push_back(std::move(pending));
        }
    }

    if (bankLocationsChanged) {
        if (!ParseAkiBanks(rom, nullptr, error)) return false;
    }

    size_t applied = 0;
    for (const auto& pending : labels) {
        auto it = std::find_if(rom.sounds.begin(), rom.sounds.end(), [&](const SoundRecord& sound) {
            return sound.bankId == pending.bank && sound.soundId == pending.id;
        });
        if (it == rom.sounds.end()) continue;
        it->label = pending.label;
        ++applied;
    }
    if (!labels.empty() && applied == 0) {
        error = "No sound label rows matched the loaded ROM after importing the profile.";
        return false;
    }
    return true;
}

namespace {

bool WriteMonoPcm16WavInternal(const std::filesystem::path& path,
                               const std::vector<int16_t>& samples,
                               uint32_t sampleRate,
                               bool writeLoop,
                               uint32_t loopStart,
                               uint32_t loopEnd,
                               uint32_t loopCount,
                               std::string& error) {
    error.clear();
    if (sampleRate == 0) {
        error = "A non-zero sample rate is required.";
        return false;
    }
    if (samples.size() >
        (std::numeric_limits<uint32_t>::max() - 104U) / 2U) {
        error = "The decoded waveform is too large for a standard WAV file.";
        return false;
    }
    if (writeLoop &&
        (loopStart >= loopEnd || loopEnd > samples.size())) {
        error = "The loop range is outside the decoded waveform.";
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "Could not create WAV file: " + path.string();
        return false;
    }

    const uint32_t dataBytes =
        static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t riffSize = writeLoop ? 104U + dataBytes
                                        : 36U + dataBytes;
    out.write("RIFF", 4);
    WriteLe32(out, riffSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    WriteLe32(out, 16);
    WriteLe16(out, 1);
    WriteLe16(out, 1);
    WriteLe32(out, sampleRate);
    WriteLe32(out, sampleRate * 2U);
    WriteLe16(out, 2);
    WriteLe16(out, 16);
    out.write("data", 4);
    WriteLe32(out, dataBytes);
    for (const int16_t sample : samples) {
        WriteLe16(out, static_cast<uint16_t>(sample));
    }

    if (writeLoop) {
        out.write("smpl", 4);
        WriteLe32(out, 60);
        WriteLe32(out, 0); // manufacturer
        WriteLe32(out, 0); // product
        WriteLe32(out, static_cast<uint32_t>(
            std::llround(1000000000.0 / static_cast<double>(sampleRate))));
        WriteLe32(out, 60); // MIDI unity note
        WriteLe32(out, 0);  // pitch fraction
        WriteLe32(out, 0);  // SMPTE format
        WriteLe32(out, 0);  // SMPTE offset
        WriteLe32(out, 1);  // sample loops
        WriteLe32(out, 0);  // sampler data
        WriteLe32(out, 0);  // cue point id
        WriteLe32(out, 0);  // forward loop
        WriteLe32(out, loopStart);
        WriteLe32(out, loopEnd - 1U); // WAV smpl end is inclusive
        WriteLe32(out, 0);            // fraction
        WriteLe32(out, loopCount == 0xFFFFFFFFU ? 0U : loopCount);
    }

    if (!out) {
        error = "Failed while writing WAV data.";
        return false;
    }
    return true;
}

} // namespace

bool WriteMonoPcm16Wav(const std::filesystem::path& path,
                       const std::vector<int16_t>& samples,
                       uint32_t sampleRate,
                       std::string& error) {
    return WriteMonoPcm16WavInternal(
        path, samples, sampleRate, false, 0, 0, 0, error);
}

bool WriteMonoPcm16Wav(const std::filesystem::path& path,
                       const std::vector<int16_t>& samples,
                       uint32_t sampleRate,
                       uint32_t loopStart,
                       uint32_t loopEnd,
                       uint32_t loopCount,
                       std::string& error) {
    return WriteMonoPcm16WavInternal(
        path, samples, sampleRate, true,
        loopStart, loopEnd, loopCount, error);
}

bool ExportSoundToWav(const LoadedRom& rom,
                      const SoundRecord& sound,
                      uint32_t sampleRate,
                      const std::filesystem::path& path,
                      std::string& error) {
    const auto samples = DecodeSelectedSound(rom, sound, error);
    if (!error.empty()) return false;
    if (sound.loopControlOffset != 0 &&
        sound.loopStart < sound.loopEnd &&
        sound.loopEnd <= samples.size()) {
        return WriteMonoPcm16Wav(
            path, samples, sampleRate,
            sound.loopStart, sound.loopEnd, sound.loopCount, error);
    }
    return WriteMonoPcm16Wav(path, samples, sampleRate, error);
}

bool ExportMetadataCsv(const LoadedRom& rom,
                       const std::filesystem::path& path,
                       std::string& error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "Could not create CSV file: " + path.string();
        return false;
    }
    out << "bank,id,name,rate_hz,confidence,method,alternate_rates,coarse_tune_semitones,fine_tune_cents,pitch_keys,encoded_bytes,decoded_samples,control_record,wave_data,loop_record,loop_start,loop_end,loop_count,notes\r\n";
    for (const auto& sound : rom.sounds) {
        std::ostringstream alternates;
        for (size_t i = 0; i < sound.label.rate.alternateHz.size(); ++i) {
            if (i) alternates << ';';
            alternates << sound.label.rate.alternateHz[i];
        }
        std::ostringstream pitchKeys;
        for (size_t i = 0; i < sound.pitchKeys.size(); ++i) {
            if (i) pitchKeys << ' ';
            pitchKeys << "0x" << std::uppercase << std::hex
                      << std::setw(2) << std::setfill('0')
                      << static_cast<unsigned>(sound.pitchKeys[i])
                      << std::dec;
        }

        out << Hex4(sound.bankId) << ','
            << Hex4(sound.soundId) << ','
            << CsvEscape(sound.label.name) << ',';
        if (sound.label.rate.primaryHz) out << *sound.label.rate.primaryHz;
        out << ',' << CsvEscape(RateConfidenceText(sound.label.rate.confidence))
            << ',' << CsvEscape(sound.label.rate.method)
            << ',' << CsvEscape(alternates.str())
            << ',' << sound.coarseTuneSemitones
            << ',' << sound.fineTuneCents
            << ',' << CsvEscape(pitchKeys.str())
            << ',' << sound.encodedBytes
            << ',' << sound.decodedSampleCount()
            << ",0x" << Hex8(sound.controlRecordOffset)
            << ",0x" << Hex8(sound.waveDataOffset)
            << ",0x" << Hex8(sound.loopControlOffset)
            << ',' << sound.loopStart
            << ',' << sound.loopEnd
            << ',' << sound.loopCount
            << ',' << CsvEscape(sound.label.rate.note)
            << "\r\n";
    }
    if (!out) {
        error = "Failed while writing the metadata CSV.";
        return false;
    }
    return true;
}

std::string RateConfidenceText(RateConfidence confidence) {
    switch (confidence) {
        case RateConfidence::ConfirmedReference: return "Confirmed reference";
        case RateConfidence::RomDerived: return "ROM-derived";
        case RateConfidence::ReferenceEstimate: return "Reference estimate";
        case RateConfidence::ManualOverride: return "Manual override";
        default: return "Unknown";
    }
}

std::string Hex4(uint32_t value) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << (value & 0xFFFFU);
    return out.str();
}

std::string Hex8(uint32_t value) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

} // namespace aki
