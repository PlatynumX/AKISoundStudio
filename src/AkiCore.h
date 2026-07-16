#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aki {

enum class GameId {
    Unknown,
    WrestleMania2000,
    VirtualProWrestling2,
    RevengeRedux,
    NoMercy,
};

enum class RomByteOrder {
    Unknown,
    Z64BigEndian,
    V64ByteSwapped,
    N64LittleEndian,
};

enum class RateConfidence {
    Unknown,
    ReferenceEstimate,
    RomDerived,
    ConfirmedReference,
    ManualOverride,
};

struct BankDefinition {
    uint16_t bankId = 0;
    uint32_t controlOffset = 0;
    uint32_t waveOffset = 0;
    uint32_t sequenceObjectOffset = 0;
    std::string description;
};

struct GameProfile {
    GameId id = GameId::Unknown;
    std::string gameCode;
    std::string displayName;
    std::string knownSha1;
    uint32_t mixerRateHz = 0;
    std::vector<BankDefinition> banks;
};

struct RateInfo {
    std::optional<uint32_t> primaryHz;
    std::vector<uint32_t> alternateHz;
    RateConfidence confidence = RateConfidence::Unknown;
    std::string method;
    std::string note;
};

struct SoundLabel {
    std::string name;
    RateInfo rate;
};

struct SoundRecord {
    uint16_t bankId = 0;
    uint16_t soundId = 0;
    uint32_t controlRecordOffset = 0;
    uint32_t waveDataOffset = 0;
    uint32_t encodedBytes = 0;
    uint32_t originalEncodedBytes = 0;
    uint32_t loopControlOffset = 0;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    uint32_t loopCount = 0;
    std::array<int16_t, 16> loopState{};
    uint32_t predictorOrder = 0;
    uint32_t predictorCount = 0;
    int16_t coarseTuneSemitones = 0;
    int16_t fineTuneCents = 0;
    std::vector<uint8_t> pitchKeys;
    std::vector<int16_t> predictorBook;
    std::vector<uint8_t> replacementEncoded;
    SoundLabel label;
    uint32_t replacementSampleRate = 0;
    uint32_t replacementPcmSamples = 0;
    bool modified = false;

    [[nodiscard]] uint32_t decodedSampleCount() const;
    [[nodiscard]] uint32_t slotCapacityBytes() const;
};

struct WavPcm16 {
    uint32_t sampleRate = 0;
    uint16_t sourceChannels = 0;
    std::vector<int16_t> monoSamples;
    // RIFF/WAVE loop metadata. One forward loop is defined by exactly two
    // points: loop start and loop end.
    // A replacement WAV without those points is treated as non-looping.
    bool loopMetadataPresent = false;
    bool hasLoop = false;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0; // exclusive, matching ALADPCMloop
    uint32_t loopCount = 0xFFFFFFFFU;
};

struct LoopPreviewPlan {
    bool hasLoop = false;
    uint32_t introEnd = 0;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0; // exclusive
};

LoopPreviewPlan ResolveLoopPreviewPlan(const SoundRecord& sound,
                                       size_t decodedSampleCount);

struct GainResult {
    double requestedDb = 0.0;
    double appliedDb = 0.0;
    bool limitedToPreventClipping = false;
    uint64_t clippedSamples = 0;
    int16_t peakBefore = 0;
    int16_t peakAfter = 0;
};

struct BankAllocation {
    uint16_t bankId = 0;
    uint32_t controlStartOffset = 0;
    uint32_t normalControlEndOffset = 0;
    uint32_t waveStartOffset = 0;
    uint32_t normalWaveEndOffset = 0;
    uint32_t safeWaveEndOffset = 0;

    [[nodiscard]] uint32_t normalControlCapacityBytes() const;
    [[nodiscard]] uint32_t normalWaveCapacityBytes() const;
};

struct BankWriteOptions {
    bool enableSizeOverride = false;
    uint32_t controlEndOffset = 0;
    uint32_t waveEndOffset = 0;
    bool forceNonBlankOverride = false;
};

struct ReplacementResult {
    uint32_t inputSampleRate = 0;
    uint32_t inputSamples = 0;
    uint32_t paddedSamples = 0;
    uint32_t encodedBytes = 0;
    uint32_t slotCapacityBytes = 0;
    uint32_t rebuiltTblBytes = 0;
    uint32_t normalTblCapacityBytes = 0;
    uint32_t allowedTblCapacityBytes = 0;
    uint32_t normalTblEndOffset = 0;
    uint32_t allowedTblEndOffset = 0;
    bool loopEnabled = false;
    bool loopImportedFromWav = false;
    bool loopStateRebuilt = false;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    uint32_t loopCount = 0;
    bool bankRepacked = false;
    bool bankRelocated = false;
    bool sizeOverrideUsed = false;
};


struct AsmBankPointerReference {
    uint32_t upperInstructionOffset = 0;
    uint32_t lowerInstructionOffset = 0;
    uint32_t resolvedAddress = 0;
    uint8_t targetRegister = 0;
    bool usesAddiu = false;
};

struct BankTraceResult {
    uint16_t bankId = 0;
    uint32_t controlOffset = 0;
    uint32_t waveOffset = 0;
    uint32_t soundCount = 0;
    std::vector<AsmBankPointerReference> controlReferences;
    std::vector<AsmBankPointerReference> waveReferences;
};

struct WaveformIdentity {
    uint16_t bankId = 0;
    uint16_t soundId = 0;
    uint32_t decodedSamples = 0;
    std::string pcmSha1;
};

struct DuplicateGroup {
    std::string pcmSha1;
    uint32_t decodedSamples = 0;
    std::vector<WaveformIdentity> members;
};

struct SoundMatch {
    uint16_t sourceBankId = 0;
    uint16_t sourceSoundId = 0;
    uint16_t targetBankId = 0;
    uint16_t targetSoundId = 0;
    std::string pcmSha1;
    uint32_t decodedSamples = 0;
};

struct BankExpansionResult {
    uint16_t bankId = 0;
    uint16_t newSoundId = 0;
    uint32_t oldSoundCount = 0;
    uint32_t newSoundCount = 0;
    uint32_t relocatedCoarseTableOffset = 0;
    uint32_t relocatedFineTableOffset = 0;
    uint32_t relocatedPointerTableOffset = 0;
    uint32_t newControlRecordOffset = 0;
    uint32_t newPredictorBookOffset = 0;
    uint32_t newLoopOffset = 0;
    ReplacementResult replacement;
};

struct MigrationOptions {
    bool resampleToTargetRate = true;
    double gainDb = 0.0;
    bool preventClipping = true;
    BankWriteOptions bankWrite;
};

struct MigrationResult {
    uint32_t sourceRateHz = 0;
    uint32_t targetRateHz = 0;
    bool resampled = false;
    GainResult gain;
    ReplacementResult replacement;
};

struct LoadedRom {
    LoadedRom() = default;
    LoadedRom(const LoadedRom& other);
    LoadedRom& operator=(const LoadedRom& other);
    LoadedRom(LoadedRom&& other) noexcept;
    LoadedRom& operator=(LoadedRom&& other) noexcept;

    std::filesystem::path sourcePath;
    RomByteOrder originalByteOrder = RomByteOrder::Unknown;
    std::vector<uint8_t> z64;
    std::string title;
    std::string gameCode;
    std::string sha1;
    GameProfile customProfile;
    const GameProfile* profile = nullptr;
    std::vector<SoundRecord> sounds;
};

class LabelDatabase {
public:
    bool loadCsv(const std::filesystem::path& path, std::string* error = nullptr);
    [[nodiscard]] std::optional<SoundLabel> find(uint16_t bankId, uint16_t soundId) const;

private:
    std::map<uint32_t, SoundLabel> labels_;
};

const GameProfile& WrestleMania2000Profile();
const GameProfile& VirtualProWrestling2Profile();
const GameProfile& RevengeReduxProfile();
const GameProfile& NoMercyProfile();
const GameProfile* DetectProfile(const std::string& gameCode);

bool LoadRom(const std::filesystem::path& path, LoadedRom& out, std::string& error);
bool ParseAkiBanks(LoadedRom& rom, const LabelDatabase* labels, std::string& error);
void ApplyProfileRateRules(LoadedRom& rom);
bool AutoDetectSoundBankLocations(LoadedRom& rom, std::string& error);
bool GetBankAllocation(const LoadedRom& rom, uint16_t bankId, BankAllocation& allocation, std::string& error);

std::vector<int16_t> DecodeSelectedSound(const LoadedRom& rom, const SoundRecord& sound, std::string& error);
bool ReadPcm16Wav(const std::filesystem::path& path,
                  WavPcm16& wav,
                  std::string& error);

// High-quality mono PCM resampling used by the import workflow. Loop points
// are scaled to the new sample timeline and remain end-exclusive.
bool ResampleWavPcm16(const WavPcm16& input,
                      uint32_t targetSampleRate,
                      WavPcm16& output,
                      std::string& error);
// Applies import gain to the PCM only. Sample count, sample rate, and the two
// loop-marker positions remain unchanged. With preventClipping enabled, the
// effective positive gain is reduced only as much as needed to fit int16 PCM.
bool ApplyWavGain(WavPcm16& wav,
                  double gainDb,
                  bool preventClipping,
                  GainResult& result,
                  std::string& error);
bool EncodePcmWithOriginalBook(const SoundRecord& sound,
                               const std::vector<int16_t>& samples,
                               std::vector<uint8_t>& encoded,
                               uint32_t& paddedSamples,
                               std::string& error);
bool ReplaceSoundPcm(LoadedRom& rom,
                     SoundRecord& sound,
                     const WavPcm16& wav,
                     ReplacementResult& result,
                     std::string& error);
bool ReplaceSoundPcm(LoadedRom& rom,
                     SoundRecord& sound,
                     const WavPcm16& wav,
                     const BankWriteOptions& options,
                     ReplacementResult& result,
                     std::string& error);
bool RepairN64Crc6102(std::vector<uint8_t>& z64,
                      uint32_t& crc1,
                      uint32_t& crc2,
                      std::string& error);
bool SaveRomZ64(LoadedRom& rom,
                const std::filesystem::path& path,
                uint32_t& crc1,
                uint32_t& crc2,
                std::string& error);
bool WriteMonoPcm16Wav(const std::filesystem::path& path,
                       const std::vector<int16_t>& samples,
                       uint32_t sampleRate,
                       std::string& error);
bool WriteMonoPcm16Wav(const std::filesystem::path& path,
                       const std::vector<int16_t>& samples,
                       uint32_t sampleRate,
                       uint32_t loopStart,
                       uint32_t loopEnd,
                       uint32_t loopCount,
                       std::string& error);
bool ExportSoundToWav(const LoadedRom& rom,
                      const SoundRecord& sound,
                      uint32_t sampleRate,
                      const std::filesystem::path& path,
                      std::string& error);
bool ExportMetadataCsv(const LoadedRom& rom,
                       const std::filesystem::path& path,
                       std::string& error);
bool ImportMetadataCsv(LoadedRom& rom,
                       const std::filesystem::path& path,
                       std::string& error);
bool ExportHackProfileCsv(const LoadedRom& rom,
                          const std::filesystem::path& path,
                          std::string& error);
bool ImportHackProfileCsv(LoadedRom& rom,
                          const std::filesystem::path& path,
                          std::string& error);


// Scans MIPS LUI + ADDIU/ORI address construction pairs and ties them to
// structurally validated PtrTablesV2/WaveTables banks. This is used for hacked
// ROMs whose CTL/TBL blocks have moved and whose code pointers were updated.
bool TraceSoundBankAsmPointers(const LoadedRom& rom,
                               std::vector<BankTraceResult>& traces,
                               std::string& error);

// Produces a stable identity from the fully decoded mono PCM. Exact identities
// are safe for duplicate detection and cross-game label transfer.
bool BuildWaveformIdentities(const LoadedRom& rom,
                             std::vector<WaveformIdentity>& identities,
                             std::string& error);
bool FindDuplicateWaveforms(const LoadedRom& rom,
                            std::vector<DuplicateGroup>& groups,
                            std::string& error);
bool MatchExactWaveforms(const LoadedRom& source,
                         const LoadedRom& target,
                         std::vector<SoundMatch>& matches,
                         std::string& error);

// Expands an existing PtrTablesV2 bank by one entry. The metadata tables are
// relocated into verified blank CTL space, the predictor book is cloned from a
// template entry, and the new WAV is then encoded/repacked normally.
bool AppendSoundFromWav(LoadedRom& rom,
                        uint16_t bankId,
                        uint16_t templateSoundId,
                        const WavPcm16& wav,
                        const BankWriteOptions& options,
                        BankExpansionResult& result,
                        std::string& error);

// Transfers a decoded sound from one loaded AKI ROM into an existing target
// slot, carrying its loop points and automatically handling rate conversion,
// gain, VADPCM encoding, bank repacking, and target metadata constraints.
bool MigrateSoundToSlot(const LoadedRom& sourceRom,
                        const SoundRecord& sourceSound,
                        LoadedRom& targetRom,
                        SoundRecord& targetSound,
                        const MigrationOptions& options,
                        MigrationResult& result,
                        std::string& error);

bool ExportWaveformAnalysisCsv(const LoadedRom& rom,
                               const std::filesystem::path& path,
                               std::string& error);

std::string RateConfidenceText(RateConfidence confidence);
std::string Hex4(uint32_t value);
std::string Hex8(uint32_t value);

} // namespace aki
