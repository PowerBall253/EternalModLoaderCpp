/*
* This file is part of EternalModLoaderCpp (https://github.com/PowerBall253/EternalModLoaderCpp).
* Copyright (C) 2021 PowerBall253
*
* EternalModLoaderCpp is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* EternalModLoaderCpp is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with EternalModLoaderCpp. If not, see <https://www.gnu.org/licenses/>.
*/

#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <chrono>
#include <thread>
#include <sstream>

#include "EternalModLoader.hpp"

namespace chrono = std::chrono;

const int32_t Version = 9;
const std::string ResourceDataFileName = "rs_data";

char Separator;
std::string BasePath;
bool Verbose = false;
bool SlowMode = false;
bool CompressTextures = false;
bool MultiThreading = true;

std::vector<ResourceContainer> ResourceContainerList;
std::vector<SoundContainer> SoundContainerList;
std::map<uint64_t, ResourceDataEntry> ResourceDataMap;

std::vector<std::stringstream> stringStreams;
int32_t streamIndex = 0;

std::byte *Buffer = NULL;
int64_t BufferSize = -1;

std::mutex mtx;

/**
 * @brief Program's main entrypoint
 * 
 * @param argc Number of arguments passed to program
 * @param argv Array containing the arguments passed
 * @return Exit code indicating success/failure
 */
int main(int argc, char **argv)
{
    // Disable sync with stdio
    std::ios::sync_with_stdio(false);

    // Make cout fully buffered to increase program speed
    char coutBuf[8192];
    std::cout.rdbuf()->pubsetbuf(coutBuf, 8192);

    Separator = std::filesystem::path::preferred_separator;

    // Enable colored output
    EnableColors();

    // Display help
    if (argc == 1) {
        std::cout << "EternalModLoaderCpp by PowerBall253, based on EternalModLoader by proteh\n\n";
        std::cout << "Loads DOOM Eternal mods from ZIPs or loose files in 'Mods' folder into the .resources files in the specified directory.\n\n";
        std::cout << "USAGE: " << argv[0] << " <game path | --version> [OPTIONS]\n";
        std::cout << "\t--version - Prints the version number of the mod loader and exits with exit code same as the version number.\n\n";
        std::cout << "OPTIONS:\n";
        std::cout << "\t--list-res - List the .resources files that will be modified and exit.\n";
        std::cout << "\t--verbose - Print more information during the mod loading process.\n";
        std::cout << "\t--slow - Slow mod loading mode that produces lighter files.\n";
        std::cout << "\t--compress-textures - Compress texture files during the mod loading process.\n";
        std::cout << "\t--disable-multithreading - Disables multi-threaded mod loading." << std::endl;
        return 1;
    }

    // Display and return program version
    if (!strcmp(argv[1], "--version")) {
        std::cout << Version << std::endl;
        return Version;
    }

    BasePath = std::string(argv[1]) + Separator + "base" + Separator;

    if (!std::filesystem::exists(BasePath)) {
        std::cout << RED << "ERROR: " << RESET << "Game directory does not exist!" << std::endl;
        return 1;
    }

    bool listResources = false;

    // Check arguments passed to program
    if (argc > 2) {
        for (int32_t i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "--list-res")) {
                listResources = true;
            }
            else if (!strcmp(argv[i], "--verbose")) {
                Verbose = true;
                std::cout << YELLOW << "INFO: Verbose logging is enabled." << RESET << std::endl;
            }
            else if (!strcmp(argv[i], "--slow")) {
                SlowMode = true;
                std::cout << YELLOW << "INFO: Slow mod loading mode is enabled." << RESET << std::endl;
            }
            else if (!strcmp(argv[i], "--compress-textures")) {
                CompressTextures = true;
                std::cout << YELLOW << "INFO: Texture compression is enabled." << RESET << std::endl;
            }
            else if (!strcmp(argv[i], "--disable-multithreading")) {
                MultiThreading = false;
                std::cout << YELLOW << "INFO: Multi-threading is disabled." << RESET << std::endl;
            }
            else {
                std::cout << RED << "ERROR: " << RESET << "Unknown argument: " << argv[i] << std::endl;
                return 1;
            }
        }
    }

    // Reserve enough space for all resource/sound containers
    ResourceContainerList.reserve(80);
    SoundContainerList.reserve(40);

    // Parse rs_data
    if (!listResources) {
        std::string resourceDataFilePath = BasePath + ResourceDataFileName;

        if (std::filesystem::exists(resourceDataFilePath)) {
            try {
                ResourceDataMap = ParseResourceData(resourceDataFilePath);

                if (ResourceDataMap.empty())
                    throw std::exception();
            }
            catch (...) {
                std::cout << RED << "ERROR: " << RESET << "Failed to parse " << ResourceDataFileName << '\n';
            }
        }
        else {
            if (Verbose) {
                std::cout << RED << "WARNING: " << RESET << ResourceDataFileName << " was not found! There will be issues when adding existing new assets to containers..." << '\n';
            }
        }
    }

    // Find mods
    std::vector<std::string> zippedMods;
    std::vector<std::string> unzippedMods;
    std::vector<std::string> notFoundContainers;

    for (const auto &file : std::filesystem::recursive_directory_iterator(std::string(argv[1]) + Separator + "Mods")) {
        if (!std::filesystem::is_regular_file(file.path()))
            continue;

        if (file.path().extension() == ".zip" && file.path() == std::string(argv[1]) + Separator + "Mods" + Separator + file.path().filename().string()) {
            zippedMods.push_back(file.path().string());
        }
        else if (file.path().extension() != ".zip") {
            unzippedMods.push_back(file.path().string());
        }
    }

    // Get the resource container paths
    GetResourceContainerPathList();

    // Load zipped mods
    chrono::steady_clock::time_point zippedModsBegin = chrono::steady_clock::now();

    if (MultiThreading) {
        std::vector<std::thread> zippedModLoadingThreads;
        zippedModLoadingThreads.reserve(zippedMods.size());

        for (const auto &zippedMod : zippedMods)
            zippedModLoadingThreads.push_back(std::thread(LoadZippedMod, zippedMod, listResources, std::ref(notFoundContainers)));

        for (auto &thread : zippedModLoadingThreads)
            thread.join();
    }
    else {
        for (const auto &zippedMod : zippedMods)
            LoadZippedMod(zippedMod, listResources, notFoundContainers);
    }

    chrono::steady_clock::time_point zippedModsEnd = chrono::steady_clock::now();
    double zippedModsTime = chrono::duration_cast<chrono::microseconds>(zippedModsEnd - zippedModsBegin).count() / 1000000.0;

    // Load unzipped mods
    chrono::steady_clock::time_point unzippedModsBegin = chrono::steady_clock::now();

    std::atomic<int32_t> unzippedModCount = 0;
    Mod globalLooseMod;
    globalLooseMod.LoadPriority = INT_MIN;

    if (MultiThreading) {
        std::vector<std::thread> unzippedModLoadingThreads;
        unzippedModLoadingThreads.reserve(unzippedMods.size());

        for (const auto &unzippedMod : unzippedMods)
            unzippedModLoadingThreads.push_back(std::thread(LoadUnzippedMod, unzippedMod, listResources, std::ref(globalLooseMod), std::ref(unzippedModCount), std::ref(notFoundContainers)));

        for (auto &thread : unzippedModLoadingThreads)
            thread.join();
        }
    else {
        for (const auto &unzippedMod : unzippedMods)
            LoadUnzippedMod(unzippedMod, listResources, globalLooseMod, unzippedModCount, notFoundContainers);
    }

    if (unzippedModCount > 0 && !listResources)
        std::cout << "Found " << BLUE << unzippedModCount << " file(s) " << RESET << "in " << YELLOW << "'Mods' " << RESET << "folder..." << '\n';

    chrono::steady_clock::time_point unzippedModsEnd = chrono::steady_clock::now();
    double unzippedModsTime = chrono::duration_cast<chrono::microseconds>(unzippedModsEnd - unzippedModsBegin).count() / 1000000.0;

    // List resources to be modified and exit
    if (listResources) {
        for (auto &resourceContainer : ResourceContainerList) {
            if (resourceContainer.Path.empty())
                continue;

            bool shouldListResource = false;

            for (auto &modFile : resourceContainer.ModFileList) {
                if (!modFile.IsAssetsInfoJson) {
                    shouldListResource = true;
                    break;
                }

                if (!modFile.AssetsInfo.has_value())
                    continue;

                if (modFile.AssetsInfo.value().Assets.empty()
                    && modFile.AssetsInfo.value().Layers.empty()
                    && modFile.AssetsInfo.value().Maps.empty())
                        continue;

                shouldListResource = true;
                break;
            }

            if (shouldListResource)
                std::cout << resourceContainer.Path << '\n';
        }

        for (auto &soundContainer : SoundContainerList) {
            if (soundContainer.Path.empty())
                continue;

            std::cout << soundContainer.Path << '\n';
        }

        std::cout.flush();
        return 0;
    }

    // Display not found containers
    for (auto &container : notFoundContainers)
        std::cout << RED << "WARNING: " << YELLOW << container << RESET << " was not found! Skipping..." << std::endl;

    std::cout.flush();

    // Set buffer for file i/o
    try {
        SetOptimalBufferSize(std::filesystem::absolute(".").root_path().string());
    }
    catch (...) {
        std::cout << RED << "ERROR: " << RESET << "Error while determining the optimal buffer size, using 4096 as the default." << std::endl;

        if (Buffer != NULL)
            delete[] Buffer;

        Buffer = new std::byte[4096];
    }

    // Load mods
    chrono::steady_clock::time_point modLoadingBegin = chrono::steady_clock::now();

    stringStreams.resize(ResourceContainerList.size() + SoundContainerList.size());

    if (MultiThreading) {
        std::vector<std::thread> modLoadingThreads;
        modLoadingThreads.reserve(ResourceContainerList.size() + SoundContainerList.size());

        for (auto &resourceContainer : ResourceContainerList)
            modLoadingThreads.push_back(std::thread(LoadResourceMods, std::ref(resourceContainer)));

        for (auto &soundContainer : SoundContainerList)
            modLoadingThreads.push_back(std::thread(LoadSoundMods, std::ref(soundContainer)));

        for (int32_t i = 0; i < modLoadingThreads.size(); i++) {
            modLoadingThreads[i].join();
            std::cout << stringStreams[i].rdbuf();
        }
    }
    else {
        for (auto &resourceContainer : ResourceContainerList)
            LoadResourceMods(resourceContainer);

        for (auto &soundContainer : SoundContainerList)
            LoadSoundMods(soundContainer);
    }

    // Modify PackageMapSpec JSON file in disk
    PackageMapSpecInfo.ModifyPackageMapSpec();

    // Delete buffer
    delete[] Buffer;

    // Display metrics
    chrono::steady_clock::time_point modLoadingEnd = chrono::steady_clock::now();
    double modLoadingTime = chrono::duration_cast<chrono::microseconds>(modLoadingEnd - modLoadingBegin).count() / 1000000.0;

    if (Verbose) {
        std::cout << GREEN << "Zipped mods loaded in " << zippedModsTime << " seconds.\n";
        std::cout << "Unzipped mods loaded in " << unzippedModsTime << " seconds.\n";
        std::cout << "Injection finished in " << modLoadingTime << " seconds.\n";
    }

    std::cout << GREEN << "Total time taken: " << zippedModsTime + unzippedModsTime + modLoadingTime << " seconds." << RESET << std::endl;

    // Exit the program with error code 0
    return 0;
}