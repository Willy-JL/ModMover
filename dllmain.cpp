#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

#include <jsoncons/json.hpp>

#include <Windows.h>

typedef std::basic_ifstream<char32_t> u32ifstream;
typedef std::basic_ofstream<char32_t> u32ofstream;


static std::filesystem::path rootDir;
static HANDLE modInstanceMutex { nullptr };
static std::vector<std::u32string> exclusions;

static bool restoreAfterGameClosed = true;
static bool useExclusions = true;
static bool archiveModsEnabled = true;
static bool redsModsEnabled = true;

static std::filesystem::path archivePcContent;
static std::filesystem::path archivePcMod;
static std::filesystem::path r6Scripts;


void u32strReplace(std::u32string &data, std::u32string original, std::u32string replace) {
    size_t pos = data.find(original);
    while (pos != std::u32string::npos) {
        data.replace(pos, original.size(), replace);
        pos = data.find(original, pos + replace.size());
    }
}

void moveMod(std::filesystem::path originalPath, std::filesystem::path destPath) {
    std::filesystem::path movedPath = destPath / "__ModMover_" += originalPath.filename();
    std::filesystem::rename(originalPath, movedPath);
    u32ofstream originalPathNote (movedPath += ".originalpath");
    if (originalPathNote.is_open()) {
        originalPathNote << originalPath.u32string();
        originalPathNote.close();
    }
}

void restoreMod(std::filesystem::path originalPathFile) {
    std::filesystem::path modFile = originalPathFile.parent_path() / originalPathFile.stem();

    u32ifstream originalPathContainer (originalPathFile);
    if (originalPathContainer.is_open() && std::filesystem::exists(modFile)) {

        std::u32string originalPathStr;
        std::getline(originalPathContainer, originalPathStr);
        originalPathContainer.close();

        std::filesystem::path originalPath (originalPathStr);
        std::filesystem::rename(modFile, originalPath);

        std::filesystem::remove(originalPathFile);
    }
}


BOOL APIENTRY DllMain(HMODULE module, DWORD reasonForCall, LPVOID) {

    DisableThreadLibraryCalls(module);

    switch(reasonForCall) {
        case DLL_PROCESS_ATTACH: {

            // Check for correct product name
            wchar_t exePathBuf[MAX_PATH] { 0 };
            GetModuleFileName(GetModuleHandle(nullptr), exePathBuf, std::size(exePathBuf));
            std::filesystem::path exePath = exePathBuf;
            rootDir = exePath.parent_path().parent_path().parent_path();

            bool exeValid = false;
            int verInfoSz = GetFileVersionInfoSize(exePathBuf, nullptr);
            if (verInfoSz) {
                auto verInfo = std::make_unique<BYTE[]>(verInfoSz);
                if (GetFileVersionInfo(exePathBuf, 0, verInfoSz, verInfo.get())) {
                    struct {
                        WORD Language;
                        WORD CodePage;
                    } *pTranslations;
                    // Thanks WhySoSerious?, I have no idea what this block is doing but it works :D
                    UINT transBytes = 0;
                    if (VerQueryValueW(verInfo.get(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&pTranslations), &transBytes)) {
                        UINT dummy;
                        TCHAR* productName = nullptr;
                        TCHAR subBlock[64];
                        for (UINT i = 0; i < (transBytes / sizeof(*pTranslations)); i++) {
                            swprintf(subBlock, L"\\StringFileInfo\\%04x%04x\\ProductName", pTranslations[i].Language, pTranslations[i].CodePage);
                            if (VerQueryValueW(verInfo.get(), subBlock, reinterpret_cast<void**>(&productName), &dummy)) {
                                if (wcscmp(productName, L"Cyberpunk 2077") == 0) {
                                    exeValid = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            // Check for correct exe name if product name check fails
            exeValid = exeValid || (exePath.filename() == L"Cyberpunk2077.exe");

            // Quit if not attaching to CP77
            if (!exeValid) {
                break;
            }

            // Create mutex for single instancing
            modInstanceMutex = CreateMutexW(NULL, TRUE, L"ModMover Instance");
            if (!modInstanceMutex) {
                break;
            }

            // Read user config
            if (std::filesystem::exists(rootDir / "bin/x64/plugins/ModMover_config.json")) {
                std::ifstream configFile (rootDir / "bin/x64/plugins/ModMover_config.json");
                if (configFile.is_open()) {
                    jsoncons::json config = jsoncons::json::parse(configFile);
                    configFile.close();

                    restoreAfterGameClosed  = config["restore_after_game_closed"]   .as<bool>();
                    useExclusions          = config["use_exclusions"]               .as<bool>();
                    archiveModsEnabled     = config["enabled_mod_types"][".archive"].as<bool>();
                    redsModsEnabled        = config["enabled_mod_types"][".reds"]   .as<bool>();
                }
            }

            // Read custom user exclusions
            if (std::filesystem::exists(rootDir / "bin/x64/plugins/ModMover_exclusions.txt")) {
                if (useExclusions) {
                    u32ifstream exclusionsFile (rootDir / "bin/x64/plugins/ModMover_exclusions.txt");
                    if (exclusionsFile.is_open()) {
                        std::u32string str;
                        while (std::getline(exclusionsFile, str)) {
                            if (str.size() > 0) {
                                u32strReplace(str, U"/", U"\\");
                                exclusions.push_back(str);
                            }
                        }
                        exclusionsFile.close();
                    }
                }
            } else {
                u32ofstream exclusionsFile (rootDir / "bin/x64/plugins/ModMover_exclusions.txt");
                if (exclusionsFile.is_open()) {
                    exclusionsFile.close();
                }
            }

            // Path constants
            archivePcContent = (rootDir / "archive/pc/content").lexically_normal();
            archivePcMod     = (rootDir / "archive/pc/mod")    .lexically_normal();
            r6Scripts        = (rootDir / "r6/scripts")        .lexically_normal();

            // Recursive folder  iterator
            const std::filesystem::recursive_directory_iterator end;
            for (std::filesystem::recursive_directory_iterator iter{rootDir}; iter != end; iter++) {
                // Only if a regular file
                if (std::filesystem::is_regular_file(*iter)) {
                    std::filesystem::path file = iter->path().lexically_normal();


                    // Skip if string match with exclusions
                    bool skip = false;
                    for (auto exclusion : exclusions) {
                        if (file.u32string().find(exclusion) != std::u32string::npos) {
                            skip = true;
                        }
                    }
                    if (skip) {
                        continue;
                    }


                    // .archive files
                    if (archiveModsEnabled) {
                        if (file.extension().u32string() == U".archive") {

                            // Don't move if directly inside archive/pc/content
                            if (std::filesystem::equivalent(file.parent_path(), archivePcContent)) {
                                continue;
                            }

                            // Don't move if directly inside archive/pc/mod
                            if (std::filesystem::equivalent(file.parent_path(), archivePcMod)) {
                                continue;
                            }

                            // Move otherwise
                            moveMod(file, archivePcMod);
                            continue;

                        }
                    }


                    // .reds files
                    if (redsModsEnabled) {
                        if (file.extension().u32string() == U".reds") {

                            // Don't move if anywhere inside r6/scripts
                            if (file.u32string().find(r6Scripts.u32string()) != std::u32string::npos) {
                                continue;
                            }

                            // Move otherwise
                            moveMod(file, r6Scripts);
                            continue;

                        }
                    }


                }
            }

            break;
        }

        case DLL_PROCESS_DETACH: {
            if (modInstanceMutex) {

                // Restore mods to original locations
                if (restoreAfterGameClosed) {
                    const std::filesystem::recursive_directory_iterator end;
                    for (std::filesystem::recursive_directory_iterator iter{rootDir}; iter != end; iter++) {
                        // Only if a regular file
                        if (std::filesystem::is_regular_file(*iter)) {
                            std::filesystem::path file = iter->path().lexically_normal();

                            if (file.stem().u32string().substr(0,11) == U"__ModMover_" && file.extension().u32string() == U".originalpath") {
                                restoreMod(file);
                            }

                        }
                    }
                }

                // Release mutex instance
                ReleaseMutex(modInstanceMutex);
                modInstanceMutex = nullptr;
            }
            break;
        }

        default: {
            break;
        }
    }

    return TRUE;
}
