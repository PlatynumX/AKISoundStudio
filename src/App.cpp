#include "AkiCore.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"AKISoundStudioWindow";
constexpr wchar_t kAppTitle[] = L"AKI Sound Studio 0.5.9";

constexpr int IDC_OPEN_ROM = 1001;
constexpr int IDC_EXPORT_CSV = 1002;
constexpr int IDC_BANK_FILTER = 1003;
constexpr int IDC_SEARCH = 1004;
constexpr int IDC_SOUND_LIST = 1005;
constexpr int IDC_DETAILS = 1006;
constexpr int IDC_RATE = 1007;
constexpr int IDC_PLAY = 1008;
constexpr int IDC_STOP = 1009;
constexpr int IDC_EXPORT_WAV = 1010;
constexpr int IDC_OPEN_FOLDER = 1011;
constexpr int IDC_ROM_SUMMARY = 1012;
constexpr int IDC_STATUS = 1013;
constexpr int IDC_REPLACE_WAV = 1014;
constexpr int IDC_SAVE_ROM = 1015;
constexpr int IDC_OVERRIDE_ENABLE = 1016;
constexpr int IDC_CTL_OVERRIDE_END = 1017;
constexpr int IDC_TBL_OVERRIDE_END = 1018;
constexpr int IDC_FORCE_OVERRIDE = 1019;
constexpr int IDC_OVERRIDE_LABEL = 1020;
constexpr int IDC_NAME = 1021;
constexpr int IDC_APPLY_METADATA = 1022;

constexpr int ID_FILE_OPEN = 40001;
constexpr int ID_FILE_EXPORT_WAV = 40002;
constexpr int ID_FILE_EXPORT_CSV = 40003;
constexpr int ID_FILE_EXIT = 40004;
constexpr int ID_PLAY_SELECTED = 40005;
constexpr int ID_STOP_PLAYBACK = 40006;
constexpr int ID_HELP_ABOUT = 40007;
constexpr int ID_FILE_REPLACE_WAV = 40008;
constexpr int ID_FILE_SAVE_ROM = 40009;
constexpr int ID_FILE_IMPORT_PROFILE = 40010;
constexpr int ID_FILE_EXPORT_PROFILE = 40011;
constexpr int ID_TOOLS_AUTODETECT = 40012;
constexpr int ID_EDIT_APPLY_METADATA = 40013;

struct AppState {
    HWND mainWindow = nullptr;
    HWND openButton = nullptr;
    HWND saveRomButton = nullptr;
    HWND exportCsvButton = nullptr;
    HWND bankCombo = nullptr;
    HWND searchEdit = nullptr;
    HWND soundList = nullptr;
    HWND detailsEdit = nullptr;
    HWND nameEdit = nullptr;
    HWND rateEdit = nullptr;
    HWND applyMetadataButton = nullptr;
    HWND playButton = nullptr;
    HWND stopButton = nullptr;
    HWND exportWavButton = nullptr;
    HWND replaceWavButton = nullptr;
    HWND overrideEnableCheck = nullptr;
    HWND ctlOverrideEndEdit = nullptr;
    HWND tblOverrideEndEdit = nullptr;
    HWND forceOverrideCheck = nullptr;
    HWND overrideLabel = nullptr;
    HWND openFolderButton = nullptr;
    HWND romSummary = nullptr;
    HWND status = nullptr;

    std::filesystem::path executableDirectory;
    std::filesystem::path dataDirectory;
    std::filesystem::path lastExportDirectory;
    std::filesystem::path temporaryPreview;

    aki::LoadedRom rom;
    aki::LabelDatabase labels;
    std::vector<size_t> visibleIndices;
    bool romLoaded = false;
    bool dirty = false;

    HWAVEOUT previewWaveOut = nullptr;
    std::vector<int16_t> previewIntroSamples;
    std::vector<int16_t> previewLoopSamples;
    std::vector<int16_t> previewFullSamples;
    WAVEHDR previewHeaders[2]{};
    size_t previewHeaderCount = 0;
};

AppState gApp;

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring output(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), output.data(), count);
    return output;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string output(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        output.data(), count, nullptr, nullptr);
    return output;
}

std::wstring GetWindowTextString(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) return {};
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(window, value.data(), length + 1);
    value.resize(static_cast<size_t>(length));
    return value;
}

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

void ShowError(const std::wstring& message) {
    MessageBoxW(gApp.mainWindow, message.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
}

void ShowInfo(const std::wstring& message) {
    MessageBoxW(gApp.mainWindow, message.c_str(), kAppTitle, MB_OK | MB_ICONINFORMATION);
}

void SetStatus(const std::wstring& text) {
    SetWindowTextW(gApp.status, text.c_str());
}

std::filesystem::path ModuleDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

std::filesystem::path FindDataDirectory(const std::filesystem::path& executableDirectory) {
    const std::vector<std::filesystem::path> candidates{
        executableDirectory / L"data",
        executableDirectory.parent_path() / L"data",
        std::filesystem::current_path() / L"data",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate / L"vpw2_sounds.csv") &&
            std::filesystem::exists(candidate / L"wm2k_sounds.csv") &&
            std::filesystem::exists(candidate / L"revenge_redux_sounds.csv")) {
            return candidate;
        }
    }
    return executableDirectory / L"data";
}

std::wstring OpenRomDialog() {
    wchar_t filename[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = gApp.mainWindow;
    dialog.lpstrFilter = L"Nintendo 64 ROMs (*.z64;*.v64;*.n64;*.rom;*.bak)\0*.z64;*.v64;*.n64;*.rom;*.bak\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    dialog.lpstrTitle = L"Open VPW2, WrestleMania 2000, or Revenge Redux ROM";
    if (!GetOpenFileNameW(&dialog)) return {};
    return filename;
}

std::wstring OpenWavDialog() {
    wchar_t filename[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = gApp.mainWindow;
    dialog.lpstrFilter =
        L"PCM Wave audio (*.wav)\0*.wav\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
    dialog.Flags =
        OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    dialog.lpstrTitle = L"Choose replacement PCM WAV";
    if (!gApp.lastExportDirectory.empty()) {
        dialog.lpstrInitialDir =
            gApp.lastExportDirectory.c_str();
    }
    if (!GetOpenFileNameW(&dialog)) return {};
    return filename;
}


std::wstring OpenCsvDialog(const wchar_t* title) {
    wchar_t filename[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = gApp.mainWindow;
    dialog.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    dialog.lpstrTitle = title;
    if (!gApp.lastExportDirectory.empty()) dialog.lpstrInitialDir = gApp.lastExportDirectory.c_str();
    if (!GetOpenFileNameW(&dialog)) return {};
    return filename;
}

std::wstring SaveFileDialog(const wchar_t* title,
                            const wchar_t* filter,
                            const wchar_t* defaultExtension,
                            const std::wstring& suggestedName) {
    wchar_t filename[32768]{};
    const size_t copyLength = std::min(suggestedName.size(), std::size(filename) - 1);
    std::copy_n(suggestedName.data(), copyLength, filename);
    filename[copyLength] = L'\0';
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = gApp.mainWindow;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    dialog.lpstrTitle = title;
    dialog.lpstrDefExt = defaultExtension;
    if (!gApp.lastExportDirectory.empty()) dialog.lpstrInitialDir = gApp.lastExportDirectory.c_str();
    if (!GetSaveFileNameW(&dialog)) return {};
    return filename;
}

void UpdateSaveAction() {
    EnableWindow(gApp.saveRomButton, gApp.romLoaded && gApp.dirty);
}

void EnableRomActions(bool enabled) {
    EnableWindow(gApp.exportCsvButton, enabled);
    EnableWindow(gApp.bankCombo, enabled);
    EnableWindow(gApp.searchEdit, enabled);
    EnableWindow(gApp.soundList, enabled);
    EnableWindow(gApp.detailsEdit, enabled);
    EnableWindow(gApp.nameEdit, enabled);
    EnableWindow(gApp.rateEdit, enabled);
    EnableWindow(gApp.applyMetadataButton, enabled);
    EnableWindow(gApp.playButton, enabled);
    EnableWindow(gApp.stopButton, enabled);
    EnableWindow(gApp.exportWavButton, enabled);
    EnableWindow(gApp.replaceWavButton, enabled);
    EnableWindow(gApp.overrideEnableCheck, enabled);
    EnableWindow(gApp.ctlOverrideEndEdit, enabled);
    EnableWindow(gApp.tblOverrideEndEdit, enabled);
    EnableWindow(gApp.forceOverrideCheck, enabled);
    EnableWindow(gApp.openFolderButton, enabled);
    UpdateSaveAction();
}

std::optional<size_t> SelectedSoundIndex() {
    const int selected = ListView_GetNextItem(gApp.soundList, -1, LVNI_SELECTED);
    if (selected < 0) return std::nullopt;
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = selected;
    if (!ListView_GetItem(gApp.soundList, &item)) return std::nullopt;
    const size_t index = static_cast<size_t>(item.lParam);
    if (index >= gApp.rom.sounds.size()) return std::nullopt;
    return index;
}

std::wstring TrimWide(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool ParseOptionalHexOffset(HWND edit,
                            const wchar_t* label,
                            uint32_t& value,
                            std::wstring& error) {
    value = 0;
    std::wstring text = TrimWide(GetWindowTextString(edit));
    if (text.empty()) return true;
    if (text.size() > 2 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
        text = text.substr(2);
    }
    if (text.empty()) return true;
    for (wchar_t ch : text) {
        if (!iswxdigit(ch)) {
            error = std::wstring(label) + L" must be a hexadecimal ROM offset.";
            return false;
        }
    }
    try {
        const unsigned long parsed = std::stoul(text, nullptr, 16);
        value = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        error = std::wstring(label) + L" is outside the supported 32-bit range.";
        return false;
    }
}

bool CurrentBankWriteOptions(aki::BankWriteOptions& options,
                             std::wstring& error) {
    options = {};
    if (Button_GetCheck(gApp.overrideEnableCheck) != BST_CHECKED) return true;
    options.enableSizeOverride = true;
    options.forceNonBlankOverride =
        Button_GetCheck(gApp.forceOverrideCheck) == BST_CHECKED;
    if (!ParseOptionalHexOffset(gApp.ctlOverrideEndEdit, L"CTL override end", options.controlEndOffset, error)) {
        return false;
    }
    if (!ParseOptionalHexOffset(gApp.tblOverrideEndEdit, L"TBL override end", options.waveEndOffset, error)) {
        return false;
    }
    if (options.controlEndOffset == 0 && options.waveEndOffset == 0) {
        error = L"Expert override is enabled, but no CTL or TBL override end offset was entered.";
        return false;
    }
    return true;
}

uint32_t SelectedRateOrZero() {
    const std::wstring value = GetWindowTextString(gApp.rateEdit);
    try {
        const unsigned long parsed = std::stoul(value);
        if (parsed == 0 || parsed > 384000) return 0;
        return static_cast<uint32_t>(parsed);
    } catch (...) {
        return 0;
    }
}

std::wstring RateDisplay(const aki::SoundRecord& sound) {
    if (!sound.label.rate.primaryHz) return L"Unknown";
    return std::to_wstring(*sound.label.rate.primaryHz);
}

std::wstring SoundDetails(const aki::SoundRecord& sound) {
    std::wostringstream out;
    out << L"Bank " << Utf8ToWide(aki::Hex4(sound.bankId))
        << L" / Sound " << Utf8ToWide(aki::Hex4(sound.soundId)) << L"\r\n";
    out << L"Name: " << (sound.label.name.empty() ? L"(unlabeled)" : Utf8ToWide(sound.label.name)) << L"\r\n\r\n";

    out << L"Sample rate: " << RateDisplay(sound) << L" Hz\r\n";
    out << L"Confidence: " << Utf8ToWide(aki::RateConfidenceText(sound.label.rate.confidence)) << L"\r\n";
    out << L"Method: " << (sound.label.rate.method.empty() ? L"Unknown" : Utf8ToWide(sound.label.rate.method)) << L"\r\n";
    if (!sound.pitchKeys.empty()) {
        out << L"ROM pitch keys: ";
        for (size_t i = 0; i < sound.pitchKeys.size(); ++i) {
            if (i) out << L", ";
            out << L"0x" << std::uppercase << std::hex
                << std::setw(2) << std::setfill(L'0')
                << static_cast<unsigned>(sound.pitchKeys[i])
                << std::dec;
        }
        out << L"\r\n";
    }
    out << L"Wave tuning: " << sound.coarseTuneSemitones
        << L" semitones, " << sound.fineTuneCents << L" cents\r\n";
    if (!sound.label.rate.alternateHz.empty()) {
        out << L"Alternate playback-equivalent rates: ";
        for (size_t i = 0; i < sound.label.rate.alternateHz.size(); ++i) {
            if (i) out << L", ";
            out << sound.label.rate.alternateHz[i] << L" Hz";
        }
        out << L"\r\n";
    }
    if (!sound.label.rate.note.empty()) out << L"Notes: " << Utf8ToWide(sound.label.rate.note) << L"\r\n";

    out << L"\r\nEncoded VADPCM bytes: " << sound.encodedBytes
        << L" / original slot " << sound.slotCapacityBytes() << L"\r\n";
    out << L"Decoded PCM samples: " << sound.decodedSampleCount() << L"\r\n";
    out << L"Modified in memory: " << (sound.modified ? L"Yes" : L"No") << L"\r\n";
    if (sound.modified) {
        out << L"Imported WAV: " << sound.replacementPcmSamples
            << L" samples at " << sound.replacementSampleRate << L" Hz\r\n";
    }
    out << L"Control record: 0x" << Utf8ToWide(aki::Hex8(sound.controlRecordOffset)) << L"\r\n";
    out << L"Wave data: 0x" << Utf8ToWide(aki::Hex8(sound.waveDataOffset)) << L"\r\n";
    out << L"Predictor order/count: " << sound.predictorOrder << L" / " << sound.predictorCount << L"\r\n";
    if (sound.loopControlOffset != 0) {
        out << L"Loop: " << sound.loopStart << L" - " << sound.loopEnd
            << L"; count "
            << (sound.loopCount == 0xFFFFFFFFU
                    ? std::wstring(L"infinite")
                    : std::to_wstring(sound.loopCount))
            << L"\r\n";
        out << L"Loop state record: 0x"
            << Utf8ToWide(aki::Hex8(sound.loopControlOffset)) << L"\r\n";
    } else {
        out << L"Loop: disabled\r\n";
    }

    aki::BankAllocation allocation;
    std::string allocationError;
    if (gApp.romLoaded && aki::GetBankAllocation(gApp.rom, sound.bankId, allocation, allocationError)) {
        out << L"\r\nBank allocation:\r\n";
        out << L"  CTL: 0x" << Utf8ToWide(aki::Hex8(allocation.controlStartOffset))
            << L" - 0x" << Utf8ToWide(aki::Hex8(allocation.normalControlEndOffset))
            << L" (0x" << Utf8ToWide(aki::Hex8(allocation.normalControlCapacityBytes())) << L" bytes)\r\n";
        out << L"  TBL: 0x" << Utf8ToWide(aki::Hex8(allocation.waveStartOffset))
            << L" - 0x" << Utf8ToWide(aki::Hex8(allocation.normalWaveEndOffset))
            << L" (0x" << Utf8ToWide(aki::Hex8(allocation.normalWaveCapacityBytes())) << L" bytes)\r\n";
        if (allocation.safeWaveEndOffset > allocation.normalWaveEndOffset) {
            out << L"  Safe trailing TBL padding through 0x"
                << Utf8ToWide(aki::Hex8(allocation.safeWaveEndOffset))
                << L" (+0x"
                << Utf8ToWide(aki::Hex8(
                       allocation.safeWaveEndOffset -
                       allocation.normalWaveEndOffset))
                << L" bytes)\r\n";
        }
    }
    return out.str();
}

void UpdateSelectionDetails() {
    const auto selected = SelectedSoundIndex();
    if (!selected) {
        SetWindowTextW(gApp.detailsEdit, L"Select a sound to inspect its ROM metadata and rate evidence.");
        SetWindowTextW(gApp.nameEdit, L"");
        SetWindowTextW(gApp.rateEdit, L"");
        return;
    }
    const auto& sound = gApp.rom.sounds[*selected];
    const std::wstring details = SoundDetails(sound);
    SetWindowTextW(gApp.detailsEdit, details.c_str());
    SetWindowTextW(gApp.nameEdit, Utf8ToWide(sound.label.name).c_str());
    SetWindowTextW(gApp.rateEdit,
                   sound.label.rate.primaryHz ? std::to_wstring(*sound.label.rate.primaryHz).c_str() : L"");
}

void AddListColumn(int index, int width, const wchar_t* text) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<wchar_t*>(text);
    column.cx = width;
    column.iSubItem = index;
    ListView_InsertColumn(gApp.soundList, index, &column);
}

void SetListSubItem(int row, int column, const std::wstring& value) {
    ListView_SetItemText(gApp.soundList, row, column, const_cast<wchar_t*>(value.c_str()));
}

void RefreshSoundList() {
    ListView_DeleteAllItems(gApp.soundList);
    gApp.visibleIndices.clear();
    if (!gApp.romLoaded) return;

    const int selectedBank = ComboBox_GetCurSel(gApp.bankCombo);
    int bankFilter = -1;
    if (selectedBank > 0) {
        const LRESULT itemData = ComboBox_GetItemData(gApp.bankCombo, selectedBank);
        if (itemData != CB_ERR) bankFilter = static_cast<int>(itemData);
    }
    const std::wstring search = ToLower(GetWindowTextString(gApp.searchEdit));

    int row = 0;
    for (size_t index = 0; index < gApp.rom.sounds.size(); ++index) {
        const auto& sound = gApp.rom.sounds[index];
        if (bankFilter >= 0 && sound.bankId != bankFilter) continue;

        std::wostringstream haystack;
        haystack << Utf8ToWide(aki::Hex4(sound.bankId)) << L' '
                 << Utf8ToWide(aki::Hex4(sound.soundId)) << L' '
                 << Utf8ToWide(sound.label.name) << L' '
                 << RateDisplay(sound) << L' '
                 << Utf8ToWide(aki::RateConfidenceText(sound.label.rate.confidence));
        if (!search.empty() && ToLower(haystack.str()).find(search) == std::wstring::npos) continue;

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        const std::wstring bank = Utf8ToWide(aki::Hex4(sound.bankId));
        item.pszText = const_cast<wchar_t*>(bank.c_str());
        item.lParam = static_cast<LPARAM>(index);
        const int insertedRow = ListView_InsertItem(gApp.soundList, &item);
        if (insertedRow < 0) continue;
        SetListSubItem(insertedRow, 1, Utf8ToWide(aki::Hex4(sound.soundId)));
        SetListSubItem(insertedRow, 2, sound.label.name.empty() ? L"(unlabeled)" : Utf8ToWide(sound.label.name));
        SetListSubItem(insertedRow, 3, RateDisplay(sound));
        SetListSubItem(insertedRow, 4, Utf8ToWide(aki::RateConfidenceText(sound.label.rate.confidence)));
        SetListSubItem(insertedRow, 5, std::to_wstring(sound.encodedBytes));
        SetListSubItem(insertedRow, 6, std::to_wstring(sound.decodedSampleCount()));
        SetListSubItem(insertedRow, 7, sound.modified ? L"Modified" : L"Original");
        gApp.visibleIndices.push_back(index);
        ++row;
    }

    std::wstring status = L"Showing " + std::to_wstring(row) + L" of " +
                          std::to_wstring(gApp.rom.sounds.size()) + L" sounds.";
    SetStatus(status);
    if (row > 0) {
        ListView_SetItemState(gApp.soundList, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(gApp.soundList, 0, FALSE);
    } else {
        UpdateSelectionDetails();
    }
}

void PopulateBankFilter() {
    ComboBox_ResetContent(gApp.bankCombo);
    const int allBanksIndex = ComboBox_AddString(gApp.bankCombo, L"All banks");
    if (allBanksIndex >= 0) ComboBox_SetItemData(gApp.bankCombo, allBanksIndex, static_cast<LPARAM>(-1));
    if (gApp.rom.profile) {
        for (const auto& bank : gApp.rom.profile->banks) {
            const std::wstring label = L"Bank " + Utf8ToWide(aki::Hex4(bank.bankId)) + L" — " + Utf8ToWide(bank.description);
            const int item = ComboBox_AddString(gApp.bankCombo, label.c_str());
            if (item >= 0) ComboBox_SetItemData(gApp.bankCombo, item, static_cast<LPARAM>(bank.bankId));
        }
    }
    ComboBox_SetCurSel(gApp.bankCombo, 0);
}

bool LoadLabelsForCurrentRom(std::string& error) {
    if (!gApp.rom.profile) return false;
    const wchar_t* filename = nullptr;
    switch (gApp.rom.profile->id) {
        case aki::GameId::WrestleMania2000:
            filename = L"wm2k_sounds.csv";
            break;
        case aki::GameId::VirtualProWrestling2:
            filename = L"vpw2_sounds.csv";
            break;
        case aki::GameId::RevengeRedux:
            filename = L"revenge_redux_sounds.csv";
            break;
        default:
            error = "No label database is configured for the selected game profile.";
            return false;
    }
    return gApp.labels.loadCsv(gApp.dataDirectory / filename, &error);
}

void LoadRomFromPath(const std::filesystem::path& path) {
    PlaySoundW(nullptr, nullptr, 0);
    SetStatus(L"Reading ROM and parsing AKI banks...");
    UpdateWindow(gApp.mainWindow);

    aki::LoadedRom loaded;
    std::string error;
    if (!aki::LoadRom(path, loaded, error)) {
        ShowError(Utf8ToWide(error));
        SetStatus(L"ROM load failed.");
        return;
    }

    gApp.rom = std::move(loaded);
    if (!gApp.rom.profile || gApp.rom.profile->banks.empty()) {
        ShowError(L"Internal profile-binding failure: the detected game profile has no sound banks.");
        gApp.romLoaded = false;
        EnableRomActions(false);
        SetStatus(L"ROM load failed before bank parsing.");
        return;
    }

    std::string labelError;
    if (!LoadLabelsForCurrentRom(labelError)) {
        const std::wstring warning = L"The ROM was recognized, but its label database could not be loaded:\r\n\r\n" +
                                     Utf8ToWide(labelError) + L"\r\n\r\nThe raw banks will still be parsed.";
        MessageBoxW(gApp.mainWindow, warning.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
    }

    if (!aki::ParseAkiBanks(gApp.rom, &gApp.labels, error)) {
        ShowError(Utf8ToWide(error));
        gApp.romLoaded = false;
        EnableRomActions(false);
        SetStatus(L"AKI bank parsing failed.");
        return;
    }

    gApp.romLoaded = true;
    gApp.dirty = false;
    gApp.lastExportDirectory = path.parent_path();
    EnableRomActions(true);
    PopulateBankFilter();

    std::wostringstream summary;
    summary << Utf8ToWide(gApp.rom.profile->displayName)
            << L"  |  Code " << Utf8ToWide(gApp.rom.gameCode)
            << L"  |  " << gApp.rom.sounds.size() << L" sounds"
            << L"  |  Mixer " << gApp.rom.profile->mixerRateHz << L" Hz"
            << L"  |  SHA-1 " << Utf8ToWide(gApp.rom.sha1);
    SetWindowTextW(gApp.romSummary, summary.str().c_str());

    SetWindowTextW(gApp.searchEdit, L"");
    RefreshSoundList();
    SetWindowTextW(gApp.mainWindow,
                   (std::wstring(kAppTitle) + L" — " + path.filename().wstring()).c_str());
}

void OpenRom() {
    const std::wstring selected = OpenRomDialog();
    if (!selected.empty()) LoadRomFromPath(selected);
}

std::wstring SuggestedSoundFilename(const aki::SoundRecord& sound) {
    std::wstring name = L"BANK_" + Utf8ToWide(aki::Hex4(sound.bankId)) +
                        L"_SND_" + Utf8ToWide(aki::Hex4(sound.soundId));
    if (!sound.label.name.empty()) {
        name += L"_";
        for (wchar_t ch : Utf8ToWide(sound.label.name)) {
            if (iswalnum(ch) || ch == L'-' || ch == L'_') name.push_back(ch);
            else if (ch == L' ' && !name.empty() && name.back() != L'_') name.push_back(L'_');
        }
    }
    return name + L".wav";
}

void ExportSelectedWav() {
    const auto selected = SelectedSoundIndex();
    if (!selected) {
        ShowInfo(L"Select a sound first.");
        return;
    }
    const uint32_t sampleRate = SelectedRateOrZero();
    if (sampleRate == 0) {
        ShowError(L"Enter a valid sample rate between 1 and 384000 Hz. Unknown rates require a manual value.");
        return;
    }

    const auto& sound = gApp.rom.sounds[*selected];
    const std::wstring path = SaveFileDialog(L"Export decoded WAV",
                                              L"Wave audio (*.wav)\0*.wav\0All files (*.*)\0*.*\0",
                                              L"wav", SuggestedSoundFilename(sound));
    if (path.empty()) return;

    std::string error;
    if (!aki::ExportSoundToWav(gApp.rom, sound, sampleRate, path, error)) {
        ShowError(Utf8ToWide(error));
        return;
    }
    gApp.lastExportDirectory = std::filesystem::path(path).parent_path();
    SetStatus(L"Exported " + std::filesystem::path(path).filename().wstring() + L" at " +
              std::to_wstring(sampleRate) + L" Hz.");
}

void ExportMetadataCsv() {
    if (!gApp.romLoaded) return;
    const std::wstring suggested = Utf8ToWide(gApp.rom.gameCode) + L"_aki_sound_report.csv";
    const std::wstring path = SaveFileDialog(L"Export sound metadata",
                                              L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0",
                                              L"csv", suggested);
    if (path.empty()) return;
    std::string error;
    if (!aki::ExportMetadataCsv(gApp.rom, path, error)) {
        ShowError(Utf8ToWide(error));
        return;
    }
    gApp.lastExportDirectory = std::filesystem::path(path).parent_path();
    SetStatus(L"Exported metadata CSV.");
}


void ExportHackProfileCsv() {
    if (!gApp.romLoaded) return;
    const std::wstring suggested = gApp.rom.profile
        ? Utf8ToWide(gApp.rom.profile->gameCode) + L"_hack_profile.csv"
        : L"aki_hack_profile.csv";
    const std::wstring path = SaveFileDialog(L"Export hack profile / editable sound list",
                                              L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0",
                                              L"csv",
                                              suggested);
    if (path.empty()) return;
    std::string error;
    if (!aki::ExportHackProfileCsv(gApp.rom, path, error)) {
        ShowError(Utf8ToWide(error));
        return;
    }
    gApp.lastExportDirectory = std::filesystem::path(path).parent_path();
    SetStatus(L"Exported hack profile CSV with bank locations and editable list entries.");
}

void ImportHackProfileCsv() {
    if (!gApp.romLoaded) return;
    const std::wstring path = OpenCsvDialog(L"Import hack profile / editable sound list");
    if (path.empty()) return;
    std::string error;
    if (!aki::ImportHackProfileCsv(gApp.rom, path, error)) {
        ShowError(Utf8ToWide(error));
        return;
    }
    gApp.lastExportDirectory = std::filesystem::path(path).parent_path();
    RefreshSoundList();
    UpdateSelectionDetails();
    SetStatus(L"Imported hack profile CSV.");
}

void AutoDetectSoundLocations() {
    if (!gApp.romLoaded) return;
    std::string error;
    if (!aki::AutoDetectSoundBankLocations(gApp.rom, error)) {
        ShowError(Utf8ToWide(error));
        return;
    }
    if (!aki::ParseAkiBanks(gApp.rom, &gApp.labels, error)) {
        ShowError(Utf8ToWide(error));
        return;
    }
    RefreshSoundList();
    UpdateSelectionDetails();
    SetStatus(L"Auto-detected AKI sound bank locations and reparsed the ROM.");
}

void ApplyMetadataEdit() {
    const auto selected = SelectedSoundIndex();
    if (!selected) return;
    auto& sound = gApp.rom.sounds[*selected];
    sound.label.name = WideToUtf8(GetWindowTextString(gApp.nameEdit));
    const std::wstring rateText = TrimWide(GetWindowTextString(gApp.rateEdit));
    if (rateText.empty()) {
        sound.label.rate.primaryHz.reset();
        sound.label.rate.confidence = aki::RateConfidence::Unknown;
        sound.label.rate.method.clear();
    } else {
        const uint32_t rate = SelectedRateOrZero();
        if (rate == 0) {
            ShowError(L"Rate must be blank or a whole-number Hz value between 1 and 384000.");
            return;
        }
        sound.label.rate.primaryHz = rate;
        sound.label.rate.confidence = aki::RateConfidence::ManualOverride;
        sound.label.rate.method = "User-edited list entry";
    }
    RefreshSoundList();
    UpdateSelectionDetails();
    SetStatus(L"Updated list entry in memory. Export a hack profile CSV to reuse it with this hack.");
}


void ReplaceSelectedWav() {
    const auto selected = SelectedSoundIndex();
    if (!selected) {
        ShowInfo(L"Select a sound first.");
        return;
    }

    const std::wstring selectedPath = OpenWavDialog();
    if (selectedPath.empty()) return;

    aki::WavPcm16 wav;
    std::string error;
    if (!aki::ReadPcm16Wav(selectedPath, wav, error)) {
        ShowError(Utf8ToWide(error));
        return;
    }

    const uint32_t expectedRate = SelectedRateOrZero();
    bool resampledOnImport = false;
    uint32_t originalRate = wav.sampleRate;
    size_t originalSampleCount = wav.monoSamples.size();
    if (expectedRate != 0 && wav.sampleRate != expectedRate) {
        std::wostringstream prompt;
        prompt
            << L"The WAV is " << wav.sampleRate << L" Hz, but this sound is set to "
            << expectedRate << L" Hz.\r\n\r\n"
            << L"Would you like AKI Sound Studio to resample it automatically?\r\n\r\n"
            << L"Yes: resample to " << expectedRate
            << L" Hz and scale the two loop-marker positions.\r\n"
            << L"No: import the original PCM unchanged.\r\n"
            << L"Cancel: do not import.";
        const int choice = MessageBoxW(gApp.mainWindow, prompt.str().c_str(),
                                       kAppTitle, MB_YESNOCANCEL | MB_ICONQUESTION);
        if (choice == IDCANCEL) return;
        if (choice == IDYES) {
            aki::WavPcm16 converted;
            if (!aki::ResampleWavPcm16(wav, expectedRate, converted, error)) {
                ShowError(L"Automatic resampling failed:\r\n\r\n" + Utf8ToWide(error));
                return;
            }
            wav = std::move(converted);
            resampledOnImport = true;
        }
    }

    aki::BankWriteOptions writeOptions;
    std::wstring optionError;
    if (!CurrentBankWriteOptions(writeOptions, optionError)) {
        ShowError(optionError);
        return;
    }

    auto& sound = gApp.rom.sounds[*selected];
    aki::ReplacementResult result;
    if (!aki::ReplaceSoundPcm(gApp.rom, sound, wav, writeOptions, result, error)) {
        ShowError(Utf8ToWide(error));
        return;
    }

    gApp.dirty = true;
    UpdateSaveAction();
    gApp.lastExportDirectory =
        std::filesystem::path(selectedPath).parent_path();

    RefreshSoundList();
    std::wostringstream status;
    status << L"Repacked Bank " << Utf8ToWide(aki::Hex4(sound.bankId))
           << L" after replacing " << Utf8ToWide(aki::Hex4(sound.soundId))
           << L": " << result.inputSamples << L" PCM samples -> "
           << result.encodedBytes << L" VADPCM bytes; rebuilt TBL "
           << result.rebuiltTblBytes << L" / " << result.allowedTblCapacityBytes
           << L" bytes" << (result.sizeOverrideUsed ? L" (expert override)" : L"");
    if (resampledOnImport) {
        status << L"; resampled " << originalRate << L" Hz / "
               << originalSampleCount << L" samples to " << wav.sampleRate
               << L" Hz / " << wav.monoSamples.size() << L" samples";
    }
    if (result.loopEnabled) {
        status << L"; Wavosaur/WAV loop points "
               << result.loopStart << L"-" << result.loopEnd
               << (resampledOnImport ? L" scaled, imported, and rebuilt"
                                     : L" imported and rebuilt");
    } else {
        status << L"; no WAV loop points, target loop disabled";
    }
    status << L". Save the ROM to keep it.";
    SetStatus(status.str());
}

void SavePatchedRom() {
    if (!gApp.romLoaded) return;
    if (!gApp.dirty) {
        ShowInfo(L"There are no unsaved waveform replacements.");
        return;
    }

    std::wstring base = gApp.rom.sourcePath.stem().wstring();
    if (base.empty()) base = Utf8ToWide(gApp.rom.gameCode);
    const std::wstring suggested = base + L"_AKI_Sound_Mod.z64";
    const std::wstring path = SaveFileDialog(
        L"Save patched AKI ROM",
        L"Nintendo 64 big-endian ROM (*.z64)\0*.z64\0All files (*.*)\0*.*\0",
        L"z64",
        suggested);
    if (path.empty()) return;

    uint32_t crc1 = 0;
    uint32_t crc2 = 0;
    std::string error;
    if (!aki::SaveRomZ64(gApp.rom, path, crc1, crc2, error)) {
        ShowError(Utf8ToWide(error));
        return;
    }

    for (auto& sound : gApp.rom.sounds) sound.modified = false;
    gApp.dirty = false;
    UpdateSaveAction();
    gApp.lastExportDirectory =
        std::filesystem::path(path).parent_path();
    RefreshSoundList();

    std::wostringstream message;
    message << L"Saved " << std::filesystem::path(path).filename().wstring()
            << L"\r\n\r\n"
            << L"Output format: big-endian .z64\r\n"
            << L"CRC1: 0x" << std::uppercase << std::hex
            << std::setw(8) << std::setfill(L'0') << crc1 << L"\r\n"
            << L"CRC2: 0x" << std::setw(8) << crc2;
    ShowInfo(message.str());
    SetStatus(L"Patched ROM saved with repaired N64 CRC.");
}

void ReleasePreviewPlayback() {
    PlaySoundW(nullptr, nullptr, 0);
    if (gApp.previewWaveOut != nullptr) {
        waveOutReset(gApp.previewWaveOut);
        for (size_t i = 0; i < 2; ++i) {
            if ((gApp.previewHeaders[i].dwFlags & WHDR_PREPARED) != 0) {
                waveOutUnprepareHeader(gApp.previewWaveOut,
                                       &gApp.previewHeaders[i],
                                       sizeof(WAVEHDR));
            }
        }
        waveOutClose(gApp.previewWaveOut);
        gApp.previewWaveOut = nullptr;
    }
    gApp.previewHeaderCount = 0;
    gApp.previewHeaders[0] = {};
    gApp.previewHeaders[1] = {};
    gApp.previewIntroSamples.clear();
    gApp.previewLoopSamples.clear();
    gApp.previewFullSamples.clear();
}

bool QueuePreviewBuffer(WAVEHDR& header,
                        std::vector<int16_t>& samples,
                        DWORD flags,
                        DWORD loops,
                        std::wstring& error) {
    if (samples.empty()) return true;
    header = {};
    header.lpData = reinterpret_cast<LPSTR>(samples.data());
    header.dwBufferLength = static_cast<DWORD>(samples.size() * sizeof(int16_t));
    header.dwFlags = flags;
    header.dwLoops = loops;
    MMRESULT result = waveOutPrepareHeader(gApp.previewWaveOut, &header, sizeof(header));
    if (result != MMSYSERR_NOERROR) {
        error = L"Windows could not prepare the preview audio buffer.";
        return false;
    }
    ++gApp.previewHeaderCount;
    result = waveOutWrite(gApp.previewWaveOut, &header, sizeof(header));
    if (result != MMSYSERR_NOERROR) {
        error = L"Windows could not queue the preview audio buffer.";
        return false;
    }
    return true;
}

void PlaySelected() {
    const auto selected = SelectedSoundIndex();
    if (!selected) {
        ShowInfo(L"Select a sound first.");
        return;
    }
    const uint32_t sampleRate = SelectedRateOrZero();
    if (sampleRate == 0) {
        ShowError(L"Enter a valid sample rate before playback.");
        return;
    }

    const auto& sound = gApp.rom.sounds[*selected];
    std::string decodeError;
    auto samples = aki::DecodeSelectedSound(gApp.rom, sound, decodeError);
    if (!decodeError.empty() || samples.empty()) {
        ShowError(decodeError.empty() ? L"The selected sound decoded to no samples."
                                      : Utf8ToWide(decodeError));
        return;
    }

    ReleasePreviewPlayback();

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = sizeof(int16_t);
    format.nAvgBytesPerSec = sampleRate * format.nBlockAlign;

    MMRESULT result = waveOutOpen(&gApp.previewWaveOut,
                                  WAVE_MAPPER,
                                  &format,
                                  0,
                                  0,
                                  CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        gApp.previewWaveOut = nullptr;
        ShowError(L"Windows could not open the audio output device.");
        return;
    }

    const bool hasValidLoop = sound.loopControlOffset != 0 &&
                              sound.loopStart < sound.loopEnd &&
                              sound.loopStart < samples.size();
    std::wstring playbackError;
    if (hasValidLoop) {
        const size_t loopStart = std::min<size_t>(sound.loopStart, samples.size());
        const size_t loopEnd = std::min<size_t>(sound.loopEnd, samples.size());
        if (loopStart < loopEnd) {
            gApp.previewIntroSamples.assign(samples.begin(), samples.begin() + loopStart);
            gApp.previewLoopSamples.assign(samples.begin() + loopStart,
                                           samples.begin() + loopEnd);

            if (!QueuePreviewBuffer(gApp.previewHeaders[0],
                                    gApp.previewIntroSamples,
                                    0,
                                    0,
                                    playbackError) ||
                !QueuePreviewBuffer(gApp.previewHeaders[1],
                                    gApp.previewLoopSamples,
                                    WHDR_BEGINLOOP | WHDR_ENDLOOP,
                                    0xFFFFFFFFU,
                                    playbackError)) {
                ReleasePreviewPlayback();
                ShowError(playbackError);
                return;
            }
            SetStatus(L"Loop-previewing Bank " + Utf8ToWide(aki::Hex4(sound.bankId)) +
                      L" / " + Utf8ToWide(aki::Hex4(sound.soundId)) +
                      L" at " + std::to_wstring(sampleRate) + L" Hz; markers " +
                      std::to_wstring(loopStart) + L"-" + std::to_wstring(loopEnd) + L".");
            return;
        }
    }

    gApp.previewFullSamples = std::move(samples);
    if (!QueuePreviewBuffer(gApp.previewHeaders[0],
                            gApp.previewFullSamples,
                            0,
                            0,
                            playbackError)) {
        ReleasePreviewPlayback();
        ShowError(playbackError);
        return;
    }
    SetStatus(L"Playing Bank " + Utf8ToWide(aki::Hex4(sound.bankId)) + L" / " +
              Utf8ToWide(aki::Hex4(sound.soundId)) + L" at " + std::to_wstring(sampleRate) + L" Hz.");
}

void StopPlayback() {
    ReleasePreviewPlayback();
    SetStatus(L"Playback stopped.");
}

void OpenExportFolder() {
    if (gApp.lastExportDirectory.empty()) return;
    ShellExecuteW(gApp.mainWindow, L"open", gApp.lastExportDirectory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ShowAbout() {
    const wchar_t* text =
        L"AKI Sound Studio 0.5.9\r\n\r\n"
        L"Windows-only sound-bank editor for Virtual Pro-Wrestling 2, WWF WrestleMania 2000, and WCW/nWo Revenge Redux.\r\n\r\n"
        L"Current features:\r\n"
        L"• Stock and compatible-hack ROM detection\r\n"
        L"• Searchable sound lists with editable names and rates\r\n"
        L"• WAV export, PCM WAV import, and Nintendo VADPCM encoding\r\n"
        L"• Bank-local TBL repacking with automatic safe-padding use and expert CTL/TBL end overrides\r\n"
        L"• Wavosaur-compatible two-point WAV loops with rebuilt ADPCM loop state\r\n"
        L"• Hack profile CSV import/export and relocated-bank auto-detection\r\n"
        L"• Big-endian .z64 save-as with CIC-6102 CRC repair\r\n\r\n"
        L"Version 0.5.9 adds marker-aware preview playback: the intro plays once, then the sound loops continuously between its two stored loop points until Stop is pressed. Revenge Redux Bank 01 rates remain traced from ROM playback scripts and waveform tuning.";
    MessageBoxW(gApp.mainWindow, text, kAppTitle, MB_OK | MB_ICONINFORMATION);
}

void LayoutControls(HWND window, int width, int height) {
    constexpr int margin = 10;
    constexpr int topRowHeight = 29;
    constexpr int summaryHeight = 25;
    constexpr int filterHeight = 27;
    constexpr int detailsWidth = 390;
    constexpr int bottomHeight = 28;

    int y = margin;
    MoveWindow(gApp.openButton, margin, y, 105, topRowHeight, TRUE);
    MoveWindow(gApp.saveRomButton, margin + 113, y, 125, topRowHeight, TRUE);
    MoveWindow(gApp.exportCsvButton, margin + 246, y, 125, topRowHeight, TRUE);
    MoveWindow(gApp.openFolderButton, margin + 379, y, 125, topRowHeight, TRUE);
    MoveWindow(gApp.romSummary, margin + 518, y + 3, std::max(100, width - margin - 518), summaryHeight, TRUE);
    y += topRowHeight + 8;

    MoveWindow(gApp.bankCombo, margin, y, 285, 250, TRUE);
    MoveWindow(gApp.searchEdit, margin + 295, y, std::max(100, width - detailsWidth - margin * 3 - 295), filterHeight, TRUE);
    y += filterHeight + 8;

    const int contentHeight = std::max(150, height - y - bottomHeight - margin * 2);
    const int listWidth = std::max(250, width - detailsWidth - margin * 3);
    MoveWindow(gApp.soundList, margin, y, listWidth, contentHeight, TRUE);

    const int detailsX = margin * 2 + listWidth;
    MoveWindow(gApp.detailsEdit, detailsX, y, detailsWidth, contentHeight - 184, TRUE);
    MoveWindow(gApp.nameEdit, detailsX, y + contentHeight - 176, 265, 26, TRUE);
    MoveWindow(gApp.applyMetadataButton, detailsX + 273, y + contentHeight - 176, 117, 26, TRUE);
    MoveWindow(gApp.rateEdit, detailsX, y + contentHeight - 140, 105, 26, TRUE);
    MoveWindow(gApp.playButton, detailsX + 113, y + contentHeight - 140, 72, 26, TRUE);
    MoveWindow(gApp.stopButton, detailsX + 193, y + contentHeight - 140, 62, 26, TRUE);
    MoveWindow(gApp.exportWavButton, detailsX + 263, y + contentHeight - 140, 127, 26, TRUE);
    MoveWindow(gApp.replaceWavButton, detailsX, y + contentHeight - 106, detailsWidth, 28, TRUE);
    MoveWindow(gApp.overrideLabel, detailsX, y + contentHeight - 70, 104, 24, TRUE);
    MoveWindow(gApp.overrideEnableCheck, detailsX + 106, y + contentHeight - 72, 74, 24, TRUE);
    MoveWindow(gApp.forceOverrideCheck, detailsX + 184, y + contentHeight - 72, 70, 24, TRUE);
    MoveWindow(gApp.ctlOverrideEndEdit, detailsX, y + contentHeight - 40, 188, 26, TRUE);
    MoveWindow(gApp.tblOverrideEndEdit, detailsX + 200, y + contentHeight - 40, 190, 26, TRUE);

    MoveWindow(gApp.status, margin, height - bottomHeight, width - margin * 2, 22, TRUE);
}

HWND CreateControl(const wchar_t* className, const wchar_t* text, DWORD style,
                   int id, HWND parent, DWORD exStyle = 0) {
    return CreateWindowExW(exStyle, className, text, style | WS_CHILD | WS_VISIBLE,
                           0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

void CreateMainControls(HWND window) {
    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    gApp.openButton = CreateControl(L"BUTTON", L"Open ROM...", BS_PUSHBUTTON | WS_TABSTOP, IDC_OPEN_ROM, window);
    gApp.saveRomButton = CreateControl(L"BUTTON", L"Save ROM As...", BS_PUSHBUTTON | WS_TABSTOP, IDC_SAVE_ROM, window);
    gApp.exportCsvButton = CreateControl(L"BUTTON", L"Export report...", BS_PUSHBUTTON | WS_TABSTOP, IDC_EXPORT_CSV, window);
    gApp.openFolderButton = CreateControl(L"BUTTON", L"Open export folder", BS_PUSHBUTTON | WS_TABSTOP, IDC_OPEN_FOLDER, window);
    gApp.romSummary = CreateControl(L"STATIC", L"No ROM loaded.", SS_LEFT | SS_NOPREFIX, IDC_ROM_SUMMARY, window);
    gApp.bankCombo = CreateControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, IDC_BANK_FILTER, window);
    gApp.searchEdit = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, IDC_SEARCH, window, WS_EX_CLIENTEDGE);
    Edit_SetCueBannerText(gApp.searchEdit, L"Search ID, name, rate, or confidence...");

    gApp.soundList = CreateControl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
                                   IDC_SOUND_LIST, window, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(gApp.soundList,
                                      LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    AddListColumn(0, 58, L"Bank");
    AddListColumn(1, 58, L"ID");
    AddListColumn(2, 270, L"Name");
    AddListColumn(3, 82, L"Rate Hz");
    AddListColumn(4, 132, L"Confidence");
    AddListColumn(5, 105, L"Encoded bytes");
    AddListColumn(6, 110, L"PCM samples");
    AddListColumn(7, 82, L"State");

    gApp.detailsEdit = CreateControl(L"EDIT", L"Select a sound to inspect its ROM metadata and rate evidence.",
                                     ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_BORDER,
                                     IDC_DETAILS, window, WS_EX_CLIENTEDGE);
    gApp.nameEdit = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                                  IDC_NAME, window, WS_EX_CLIENTEDGE);
    Edit_SetCueBannerText(gApp.nameEdit, L"Editable list name...");
    gApp.applyMetadataButton = CreateControl(L"BUTTON", L"Apply list edit", BS_PUSHBUTTON | WS_TABSTOP, IDC_APPLY_METADATA, window);
    gApp.rateEdit = CreateControl(L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                                  IDC_RATE, window, WS_EX_CLIENTEDGE);
    gApp.playButton = CreateControl(L"BUTTON", L"Play", BS_PUSHBUTTON | WS_TABSTOP, IDC_PLAY, window);
    gApp.stopButton = CreateControl(L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP, IDC_STOP, window);
    gApp.exportWavButton = CreateControl(L"BUTTON", L"Export WAV...", BS_PUSHBUTTON | WS_TABSTOP, IDC_EXPORT_WAV, window);
    gApp.replaceWavButton = CreateControl(L"BUTTON", L"Replace selected sound from WAV...", BS_PUSHBUTTON | WS_TABSTOP, IDC_REPLACE_WAV, window);
    gApp.overrideLabel = CreateControl(L"STATIC", L"Expert override:", SS_LEFT | SS_NOPREFIX, IDC_OVERRIDE_LABEL, window);
    gApp.overrideEnableCheck = CreateControl(L"BUTTON", L"Enable", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_OVERRIDE_ENABLE, window);
    gApp.ctlOverrideEndEdit = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, IDC_CTL_OVERRIDE_END, window, WS_EX_CLIENTEDGE);
    gApp.tblOverrideEndEdit = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, IDC_TBL_OVERRIDE_END, window, WS_EX_CLIENTEDGE);
    gApp.forceOverrideCheck = CreateControl(L"BUTTON", L"Force", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_FORCE_OVERRIDE, window);
    Edit_SetCueBannerText(gApp.ctlOverrideEndEdit, L"CTL end hex");
    Edit_SetCueBannerText(gApp.tblOverrideEndEdit, L"TBL end hex");
    gApp.status = CreateControl(L"STATIC", L"Open a VPW2 or WM2000 ROM to begin.", SS_LEFT | SS_NOPREFIX,
                                IDC_STATUS, window);

    const HWND controls[]{gApp.openButton, gApp.saveRomButton, gApp.exportCsvButton, gApp.openFolderButton, gApp.romSummary,
                          gApp.bankCombo, gApp.searchEdit, gApp.soundList, gApp.detailsEdit, gApp.nameEdit, gApp.rateEdit,
                          gApp.applyMetadataButton, gApp.playButton, gApp.stopButton, gApp.exportWavButton, gApp.replaceWavButton, gApp.overrideLabel, gApp.overrideEnableCheck, gApp.ctlOverrideEndEdit, gApp.tblOverrideEndEdit, gApp.forceOverrideCheck, gApp.status};
    for (HWND control : controls) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    EnableRomActions(false);
}

HMENU CreateAppMenu() {
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_FILE_OPEN, L"&Open ROM...\tCtrl+O");
    AppendMenuW(file, MF_STRING, ID_FILE_SAVE_ROM, L"&Save patched ROM as...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_FILE_REPLACE_WAV, L"&Replace selected sound from WAV...");
    AppendMenuW(file, MF_STRING, ID_FILE_EXPORT_WAV, L"Export selected &WAV...");
    AppendMenuW(file, MF_STRING, ID_FILE_EXPORT_CSV, L"Export sound &report...");
    AppendMenuW(file, MF_STRING, ID_FILE_IMPORT_PROFILE, L"Import hack &profile / list...");
    AppendMenuW(file, MF_STRING, ID_FILE_EXPORT_PROFILE, L"Export hack profile / &list...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_FILE_EXIT, L"E&xit");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");

    HMENU tools = CreatePopupMenu();
    AppendMenuW(tools, MF_STRING, ID_EDIT_APPLY_METADATA, L"Apply selected list &edit");
    AppendMenuW(tools, MF_STRING, ID_TOOLS_AUTODETECT, L"Auto-detect sound &locations");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), L"&Tools");

    HMENU playback = CreatePopupMenu();
    AppendMenuW(playback, MF_STRING, ID_PLAY_SELECTED, L"&Play selected\tSpace");
    AppendMenuW(playback, MF_STRING, ID_STOP_PLAYBACK, L"&Stop");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(playback), L"&Playback");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, L"&About");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
    return menu;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            gApp.mainWindow = window;
            CreateMainControls(window);
            return 0;

        case WM_SIZE:
            LayoutControls(window, LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);
            if (id == IDC_OPEN_ROM || id == ID_FILE_OPEN) OpenRom();
            else if (id == IDC_SAVE_ROM || id == ID_FILE_SAVE_ROM) SavePatchedRom();
            else if (id == IDC_REPLACE_WAV || id == ID_FILE_REPLACE_WAV) ReplaceSelectedWav();
            else if (id == IDC_APPLY_METADATA || id == ID_EDIT_APPLY_METADATA) ApplyMetadataEdit();
            else if (id == IDC_EXPORT_CSV || id == ID_FILE_EXPORT_CSV) ExportMetadataCsv();
            else if (id == ID_FILE_IMPORT_PROFILE) ImportHackProfileCsv();
            else if (id == ID_FILE_EXPORT_PROFILE) ExportHackProfileCsv();
            else if (id == ID_TOOLS_AUTODETECT) AutoDetectSoundLocations();
            else if (id == IDC_EXPORT_WAV || id == ID_FILE_EXPORT_WAV) ExportSelectedWav();
            else if (id == IDC_PLAY || id == ID_PLAY_SELECTED) PlaySelected();
            else if (id == IDC_STOP || id == ID_STOP_PLAYBACK) StopPlayback();
            else if (id == IDC_OPEN_FOLDER) OpenExportFolder();
            else if (id == ID_FILE_EXIT) DestroyWindow(window);
            else if (id == ID_HELP_ABOUT) ShowAbout();
            else if (id == IDC_BANK_FILTER && notification == CBN_SELCHANGE) RefreshSoundList();
            else if (id == IDC_SEARCH && notification == EN_CHANGE) RefreshSoundList();
            return 0;
        }

        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header && header->idFrom == IDC_SOUND_LIST) {
                if (header->code == LVN_ITEMCHANGED) UpdateSelectionDetails();
                else if (header->code == NM_DBLCLK) PlaySelected();
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_SPACE && GetFocus() == gApp.soundList) {
                PlaySelected();
                return 0;
            }
            break;

        case WM_DROPFILES: {
            wchar_t path[32768]{};
            const HDROP drop = reinterpret_cast<HDROP>(wParam);
            if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path)))) LoadRomFromPath(path);
            DragFinish(drop);
            return 0;
        }

        case WM_CLOSE:
            if (gApp.dirty) {
                const int answer = MessageBoxW(
                    window,
                    L"There are unsaved sound replacements. Exit without saving?",
                    kAppTitle,
                    MB_YESNO | MB_ICONWARNING);
                if (answer != IDYES) return 0;
            }
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            ReleasePreviewPlayback();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&commonControls);

    gApp.executableDirectory = ModuleDirectory();
    gApp.dataDirectory = FindDataDirectory(gApp.executableDirectory);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&windowClass)) return 1;

    const HWND window = CreateWindowExW(WS_EX_ACCEPTFILES, kWindowClass, kAppTitle,
                                         WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                         CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760,
                                         nullptr, CreateAppMenu(), instance, nullptr);
    if (!window) return 1;

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    if (commandLine && *commandLine) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc > 1) LoadRomFromPath(argv[1]);
        if (argv) LocalFree(argv);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
