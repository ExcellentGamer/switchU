#pragma once

#include <coreinit/mcp.h>
#include <SDL2/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

enum class AppIconFormat {
    None,
    Png,
    Tga
};

struct App {
    std::string title;
    std::string app_path;
    std::string storage_device;
    uint64_t titleid;
    std::string icon_path;
    AppIconFormat icon_format = AppIconFormat::None;
    SDL_Texture* icon = nullptr;
    SDL_Color icon_bg_color = {75, 75, 75, 255};
    uint32_t icon_last_used_frame = 0;
    bool icon_load_failed = false;
};

static const std::vector<MCPAppType> supported_sys_app_type {
    MCP_APP_TYPE_GAME,
    MCP_APP_TYPE_GAME_WII
};

static const char device_odd[10] = "odd";
static const char device_usb[10] = "usb";
static const char device_mlc[10] = "mlc";

extern std::vector<App> apps;
extern int cur_selected_tile;

SDL_Texture* load_texture(const char* path, SDL_Renderer* renderer);

// Parses and returns the <name> from a given meta.xml path
std::string get_title_from_meta(const char* path);

// Returns a new App entry for a system app. Icon textures are lazy-loaded later.
App create_sysapp_entry(const MCPTitleListType& title_info);

// Fills the apps vector with valid launchable apps (populates metadata and icon paths).
void scan_apps(SDL_Renderer* renderer);

// Loads the icon texture for an app on demand and marks it as recently used.
SDL_Texture* ensure_app_icon_loaded(App& app, SDL_Renderer* renderer, uint32_t frame);

// Frees icon textures that are no longer near the visible/selected window.
void trim_app_icon_cache(uint32_t current_frame, int selected_index);

// Frees every app icon texture currently resident in memory.
void unload_all_app_icons();

// Returns the selected app's path to pass into RPXLoader_LaunchHomebrew()
const char* get_selected_app_path();
