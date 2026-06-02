#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <sys/types.h>

#include "util.hpp"
#include "title_extractor.hpp"

extern bool load_homebrew_titles;

std::vector<App> apps;

namespace {
constexpr int MAX_DISCOVERED_APPS = 512;
constexpr int ICON_CACHE_MAX = 10;
constexpr int ICON_KEEP_RADIUS = 2;
constexpr uint32_t ICON_KEEP_ALIVE_FRAMES = 120;
constexpr SDL_Color DEFAULT_ICON_BG = {75, 75, 75, 255};

bool file_exists(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

void assign_icon_if_exists(App& app, const std::string& path, AppIconFormat format) {
    if (!file_exists(path)) return;

    app.icon_path = path;
    app.icon_format = format;
    app.icon_load_failed = false;
}

Uint32 get_surface_pixel(SDL_Surface* surface, int x, int y) {
    const int bpp = surface->format->BytesPerPixel;
    Uint8* pixel = static_cast<Uint8*>(surface->pixels) + y * surface->pitch + x * bpp;

    switch (bpp) {
        case 1:
            return *pixel;
        case 2:
            return *reinterpret_cast<Uint16*>(pixel);
        case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            return pixel[0] << 16 | pixel[1] << 8 | pixel[2];
#else
            return pixel[0] | pixel[1] << 8 | pixel[2] << 16;
#endif
        case 4:
            return *reinterpret_cast<Uint32*>(pixel);
        default:
            return 0;
    }
}

SDL_Color sample_icon_background(SDL_Surface* surface) {
    if (!surface || !surface->pixels || !surface->format || surface->w <= 0 || surface->h <= 0) {
        return DEFAULT_ICON_BG;
    }

    if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) {
        return DEFAULT_ICON_BG;
    }

    Uint32 pixel = get_surface_pixel(surface, 0, 0);
    Uint8 r = DEFAULT_ICON_BG.r;
    Uint8 g = DEFAULT_ICON_BG.g;
    Uint8 b = DEFAULT_ICON_BG.b;
    Uint8 a = DEFAULT_ICON_BG.a;
    SDL_GetRGBA(pixel, surface->format, &r, &g, &b, &a);

    if (SDL_MUSTLOCK(surface)) {
        SDL_UnlockSurface(surface);
    }

    if (a == 0) {
        return DEFAULT_ICON_BG;
    }

    return {r, g, b, 255};
}

SDL_Surface* load_icon_surface(const App& app) {
    if (app.icon_path.empty() || app.icon_format == AppIconFormat::None) {
        return nullptr;
    }

    SDL_RWops* rw = SDL_RWFromFile(app.icon_path.c_str(), "rb");
    if (!rw) {
        printf("SDL_RWFromFile failed for icon %s: %s\n", app.icon_path.c_str(), SDL_GetError());
        return nullptr;
    }

    SDL_Surface* surface = nullptr;
    if (app.icon_format == AppIconFormat::Tga) {
        surface = IMG_LoadTGA_RW(rw);
        SDL_FreeRW(rw);
    } else {
        surface = IMG_Load_RW(rw, 1);
    }

    if (!surface) {
        printf("Failed to load icon surface %s: %s\n", app.icon_path.c_str(), IMG_GetError());
    }

    return surface;
}

void unload_app_icon(App& app) {
    if (!app.icon) return;

    SDL_DestroyTexture(app.icon);
    app.icon = nullptr;
    app.icon_last_used_frame = 0;
}

} // namespace

std::unordered_set<std::string> load_ignored_apps() {
    std::unordered_set<std::string> ignored;
    std::ifstream file(SD_CARD_PATH "switchU/ignore.txt");
    if (!file.is_open()) {
        printf("No ignore.txt found or failed to open.\n");
        return ignored;
    }

    std::string line;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;

        size_t end = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(start, end - start + 1);
        printf("Ignoring app: %s\n", trimmed.c_str());
        ignored.insert(trimmed);
    }

    file.close();
    return ignored;
}

std::string get_title_from_meta(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) return "Unknown";

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char* start = strstr(line, "<title>");
        if (start) {
            start += strlen("<title>");
            char* end = strstr(start, "</title>");
            if (end) {
                *end = '\0';
                fclose(file);
                return std::string(start);
            }
        }
    }

    fclose(file);
    return "Unknown";
}

App create_sysapp_entry(const MCPTitleListType& title_info) {
    std::string title = "Unknown / Error";
    std::string base_path = ROOT_PATH + std::string(title_info.path);
    std::string meta_path = base_path + "/meta/meta.xml";
    std::string app_icon = base_path + "/meta/iconTex.tga";

    // Parse title from meta.xml
    FILE* file = fopen(meta_path.c_str(), "r");
    if (file) {
        char line[512];
        while (fgets(line, sizeof(line), file)) {
            char* start = strstr(line, "<longname_en type=\"string\" length=\"512\">");
            if (start) {
                start += strlen("<longname_en type=\"string\" length=\"512\">");
                char* end = strstr(start, "</longname_en>");
                if (end) {
                    *end = '\0';
                    title = std::string(start);
                    break;
                }
            }
        }
        fclose(file);
    } else {
        printf("Failed to open meta.xml for %s\n", meta_path.c_str());
    }

    App entry;
    entry.title = title;
    entry.app_path = base_path;
    entry.storage_device = title_info.indexedDevice;
    entry.titleid = title_info.titleId;

    std::string safe_folder_name = sanitize_title_for_path(title);
    std::string custom_icon_path = SD_CARD_PATH "switchU/custom_icons/" + safe_folder_name + "/icon.png";
    assign_icon_if_exists(entry, custom_icon_path, AppIconFormat::Png);
    if (entry.icon_format == AppIconFormat::None) {
        assign_icon_if_exists(entry, app_icon, AppIconFormat::Tga);
    }

    return entry;
}

static std::string find_launchable_file(const std::string& app_dir) {
    DIR* dir = opendir(app_dir.c_str());
    if (!dir) return "";

    std::string found_rpx, found_wuhb;
    struct dirent* entry;

    while ((entry = readdir(dir)) != nullptr) {
        std::string fname = entry->d_name;

        if (fname.size() >= 5 && fname.substr(fname.size() - 5) == ".wuhb") {
            found_wuhb = app_dir + "/" + fname;
            break;
        } else if (fname.size() >= 4 && fname.substr(fname.size() - 4) == ".rpx") {
            found_rpx = app_dir + "/" + fname;
        }
    }

    closedir(dir);

    if (!found_wuhb.empty()) return found_wuhb;
    return found_rpx;
}

void scan_apps(SDL_Renderer* renderer) {
    (void)renderer;
    unload_all_app_icons();
    apps.clear();

    const char* apps_dir = SD_CARD_PATH "wiiu/apps/";
    const char* custom_icons_dir = SD_CARD_PATH "switchU/custom_icons/";

    std::unordered_set<std::string> ignored_apps = load_ignored_apps();

    if (load_homebrew_titles) {
        DIR* dir = opendir(apps_dir);
        if (!dir) {
            printf("Failed to open apps directory\n");
            return;
        }

        printf("Starting Hombrew app scan...\n");
        struct dirent* entry;
        int loaded_count = 0;

        while ((entry = readdir(dir)) != nullptr && loaded_count < MAX_DISCOVERED_APPS) {
            if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
                std::string app_folder = entry->d_name;

                if (ignored_apps.find(app_folder) != ignored_apps.end()) {
                    printf("Skipping ignored app: %s\n", app_folder.c_str());
                    continue;
                }

                std::string app_path = std::string(apps_dir) + app_folder;

                std::string launch_file = find_launchable_file(app_path);
                if (launch_file.empty()) {
                    printf("No launchable .wuhb or .rpx found in %s\n", app_folder.c_str());
                    continue;
                }

                std::string custom_icon_path = std::string(custom_icons_dir) + app_folder + "/icon.png";
                std::string default_icon_path = app_path + "/icon.png";

                App app_entry;
                app_entry.title = app_folder;
                app_entry.app_path = launch_file;
                app_entry.storage_device = "sd";
                app_entry.titleid = 0;

                assign_icon_if_exists(app_entry, custom_icon_path, AppIconFormat::Png);
                if (app_entry.icon_format == AppIconFormat::None) {
                    assign_icon_if_exists(app_entry, default_icon_path, AppIconFormat::Png);
                }

                if (app_entry.icon_format == AppIconFormat::None) {
                    printf("No icon for app: %s\n", app_folder.c_str());
                }

                apps.push_back(app_entry);
                loaded_count++;
                printf("Loaded app: %s -> %s\n", app_folder.c_str(), launch_file.c_str());
            }
        }
        closedir(dir);
    } else {
        printf("Skipping Homebrew application scan due to its setting being disabled.\n");
    }

    printf("Starting System app scan...\n");
    MCPError handle = MCP_Open();
    if (handle < 0) {
        printf("Failed to start MCP\n");
        return;
    }

    uint32_t game_count = 0;
    uint32_t title_count = MCP_TitleCount(handle);
    if (title_count == 0) {
        printf("No titles found\n");
        MCP_Close(handle);
        return;
    }
    printf("Found %u apps\n", static_cast<unsigned int>(title_count));

    // More stuff from Launchiine, my way only worked on Cemu for some reason
    // Fixed for Cemu: use MCP_TitleList instead of MCP_TitleListByAppType for better compatibility
    std::vector<MCPTitleListType> titles(title_count);
    MCPError err = MCP_TitleList(handle, &game_count, titles.data(), titles.size() * sizeof(MCPTitleListType));

    if (err < 0) {
        printf("MCP_TitleList failed, falling back to MCP_TitleListByAppType\n");
        // Fallback to original method
        for (MCPAppType type : supported_sys_app_type) {
            if (game_count >= titles.size()) break;

            uint32_t game_count_per_type = 0;
            MCPError err2 = MCP_TitleListByAppType(
                     handle, type, &game_count_per_type, titles.data() + game_count,
                     (titles.size() - game_count) * sizeof(decltype(titles)::value_type));

            if (err2 < 0) {
                printf("Failed to get installed games of type %d; continuing\n", static_cast<int>(type));
                continue;
            }

            if (game_count + game_count_per_type > titles.size()) {
                game_count_per_type = titles.size() - game_count;
            }
            game_count += game_count_per_type;
        }
    }

    if (game_count != titles.size()) {
        titles.resize(game_count);
    }
    printf("Found %u system games\n", static_cast<unsigned int>(game_count));

    for (auto game : titles) {
        if (apps.size() >= static_cast<size_t>(MAX_DISCOVERED_APPS)) break;
        App entry = create_sysapp_entry(game);
        std::string safe_name = sanitize_title_for_path(entry.title);

        if (ignored_apps.find(safe_name) != ignored_apps.end()) {
            printf("Skipping ignored system app: %s (safe: %s)\n", entry.title.c_str(), safe_name.c_str());
            continue;
        }

        if (entry.storage_device == device_odd) {
            apps.insert(apps.begin(), entry); // Making ODD Always First
        } else {
            apps.push_back(entry);
        }
        printf("Loaded system app: %s -> %s\n", entry.title.c_str(), entry.app_path.c_str());
    }
    MCP_Close(handle);

    const char* path_char = SD_CARD_PATH "scanresult.txt";
    std::string path = path_char;
    FILE* out = fopen(path.c_str(), "w");
    if (out) {
        for (const auto& app : apps) {
            fprintf(out,    "App: %s, Path: %s, Device: %s, TitleID: %llu, Icon: %s\n",
                   app.title.c_str(), app.app_path.c_str(), app.storage_device.c_str(),
                   static_cast<unsigned long long>(app.titleid), app.icon_path.c_str());
        }
        fclose(out);
    } else {
        printf("Failed to write scan results to %s\n", path.c_str());
    }
}

SDL_Texture* ensure_app_icon_loaded(App& app, SDL_Renderer* renderer, uint32_t frame) {
    app.icon_last_used_frame = frame;

    if (app.icon || app.icon_format == AppIconFormat::None || app.icon_load_failed) {
        return app.icon;
    }

    SDL_Surface* surface = load_icon_surface(app);
    if (!surface) {
        app.icon_load_failed = true;
        return nullptr;
    }

    app.icon_bg_color = sample_icon_background(surface);
    app.icon = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);

    if (!app.icon) {
        printf("SDL_CreateTextureFromSurface failed for icon %s: %s\n", app.icon_path.c_str(), SDL_GetError());
        app.icon_load_failed = true;
        return nullptr;
    }

    SDL_SetTextureBlendMode(app.icon, SDL_BLENDMODE_BLEND);
    return app.icon;
}

void trim_app_icon_cache(uint32_t current_frame, int selected_index) {
    int loaded_count = 0;

    for (size_t i = 0; i < apps.size(); ++i) {
        App& app = apps[i];
        if (!app.icon) continue;

        const int distance = selected_index >= 0 ? std::abs(static_cast<int>(i) - selected_index) : ICON_KEEP_RADIUS + 1;
        const bool near_selected = distance <= ICON_KEEP_RADIUS;
        const bool stale = current_frame > app.icon_last_used_frame + ICON_KEEP_ALIVE_FRAMES;

        if (!near_selected && stale) {
            unload_app_icon(app);
            continue;
        }

        loaded_count++;
    }

    while (loaded_count > ICON_CACHE_MAX) {
        int victim_index = -1;
        uint32_t oldest_frame = std::numeric_limits<uint32_t>::max();
        int farthest_distance = -1;

        for (size_t i = 0; i < apps.size(); ++i) {
            const App& app = apps[i];
            if (!app.icon || app.icon_last_used_frame == current_frame) continue;

            const int distance = selected_index >= 0 ? std::abs(static_cast<int>(i) - selected_index) : ICON_KEEP_RADIUS + 1;
            if (distance <= ICON_KEEP_RADIUS) continue;

            if (app.icon_last_used_frame < oldest_frame ||
                (app.icon_last_used_frame == oldest_frame && distance > farthest_distance)) {
                victim_index = static_cast<int>(i);
                oldest_frame = app.icon_last_used_frame;
                farthest_distance = distance;
            }
        }

        if (victim_index < 0) break;

        unload_app_icon(apps[victim_index]);
        loaded_count--;
    }
}

void unload_all_app_icons() {
    for (auto& app : apps) {
        unload_app_icon(app);
    }
}

const char* get_selected_app_path() {
    if (apps.empty() || cur_selected_tile < 0 || static_cast<size_t>(cur_selected_tile) >= apps.size()) return nullptr;
    const std::string& full_path = apps[cur_selected_tile].app_path;
    size_t pos = full_path.find("wiiu/apps/");
    if (pos == std::string::npos) return nullptr;
    printf("Selected index: %d, full path: %s, trimmed path: %s\n", cur_selected_tile, full_path.c_str(), full_path.c_str() + pos);
    return full_path.c_str() + pos;
}
