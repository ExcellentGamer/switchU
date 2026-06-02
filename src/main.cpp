#include <SDL.h>
#include <SDL_image.h>

#include <rpxloader/rpxloader.h>
#include <coreinit/debug.h>
#include <coreinit/title.h>
#include <padscore/kpad.h>
#include <sndcore2/core.h>
#include <sysapp/launch.h>
#include <sysapp/title.h>
#include <nn/acp/title.h>
#include <whb/proc.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <algorithm>
#include <cstdint>
#include <nn/act.h>

#include "input/CombinedInput.h"
#include "input/VPADInput.h"
#include "input/WPADInput.h"

#include "render.hpp"
#include "util.hpp"
#include "title_extractor.hpp"
#include "font.hpp"

enum RowSelection {
    ROW_TOP = 0,
    ROW_MIDDLE = 1,
    ROW_BOTTOM = 2
};

enum Menu {
    MENU_MAIN = 0,
    MENU_APPS = 1,
    MENU_USER = 2,
    MENU_MORE = 3,
    MENU_SETTINGS = 4,
    MENU_SCREENSHOT = 5
};

namespace Config {
    constexpr int WINDOW_WIDTH = 1280;
    constexpr int WINDOW_HEIGHT = 720;

    constexpr int SCROLL_INITIAL_DELAY = 500;
    constexpr int SCROLL_REPEAT_INTERVAL = 75;

    constexpr int TILE_COUNT_BOTTOM = 8;

    constexpr int circle_diameter = 75;
    constexpr int spawn_box_size = 256;
    constexpr int settings_row_count = 4;
}

struct UITextures {
    // Hud Elements
    SDL_Texture* circle = nullptr;
    SDL_Texture* circle_selection = nullptr;
    SDL_Texture* circle_big = nullptr;
    SDL_Texture* circle_big_selection = nullptr;
    SDL_Texture* battery_full = nullptr;
    SDL_Texture* battery_three_fourths = nullptr;
    SDL_Texture* battery_half = nullptr;
    SDL_Texture* battery_needs_charge = nullptr;
    SDL_Texture* battery_base = nullptr;
    SDL_Texture* all_titles = nullptr;

    // Bottom Row
    SDL_Texture* miiverse = nullptr;
    SDL_Texture* eshop = nullptr;
    SDL_Texture* screenshots = nullptr;
    SDL_Texture* browser = nullptr;
    SDL_Texture* controller = nullptr;
    SDL_Texture* downloads = nullptr;
    SDL_Texture* settings = nullptr;
    SDL_Texture* power = nullptr;
    SDL_Texture* reference = nullptr;

    // Buttons
    SDL_Texture* a_button = nullptr;
    SDL_Texture* plus_button = nullptr;

    void destroyAll() {
        auto destroy = [](SDL_Texture*& tex) {
            if (tex) SDL_DestroyTexture(tex);
            tex = nullptr;
        };
        destroy(circle); destroy(circle_selection); destroy(circle_big);
        destroy(circle_big_selection); destroy(battery_full);
        destroy(battery_three_fourths); destroy(battery_half);
        destroy(battery_needs_charge); destroy(battery_base);
        destroy(all_titles); destroy(miiverse); destroy(eshop);
        destroy(screenshots); destroy(browser); destroy(controller);
        destroy(downloads); destroy(settings); destroy(power);
        destroy(reference); destroy(a_button); destroy(plus_button);
    }
};

static Uint32 left_hold_time = 0;
static Uint32 right_hold_time = 0;
static Uint32 up_hold_time = 0;
static Uint32 down_hold_time = 0;
static bool left_scrolling = false;
static bool right_scrolling = false;
static bool up_scrolling = false;
static bool down_scrolling = false;
bool menuOpen = false;
bool load_homebrew_titles = false;
int battery_level = 0;

int seperation_space = 264;
int target_camera_offset_x = 0;
int camera_offset_x = 0;
int cur_menu = MENU_MAIN;

int cur_selected_tile = 0;
int cur_selected_row = ROW_MIDDLE;
int cur_selected_subtile = 0;
int cur_selected_subrow = 0;

int tiles_x = Config::WINDOW_WIDTH / 6;
int tiles_y = Config::WINDOW_HEIGHT / 2;

SDL_Window *main_window;
SDL_Renderer *main_renderer;
SDL_Event event;
UITextures textures;
TTFText* textRenderer = NULL;
bool rpxloader_initialized = false;
bool audio_initialized = false;
bool account_initialized = false;

int get_middle_tile_count() {
    return std::max(1, static_cast<int>(apps.size()) + 1);
}

void clamp_middle_selection() {
    if (cur_selected_row != ROW_MIDDLE) return;

    cur_selected_tile = std::clamp(cur_selected_tile, 0, get_middle_tile_count() - 1);
}

SDL_Renderer* create_renderer_with_fallback(SDL_Window* window) {
    const Uint32 renderer_flags[] = {
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC,
        SDL_RENDERER_ACCELERATED,
        SDL_RENDERER_SOFTWARE
    };

    for (Uint32 flags : renderer_flags) {
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, flags);
        if (renderer) {
            printf("Created SDL renderer with flags: 0x%08x\n", static_cast<unsigned int>(flags));
            return renderer;
        }

        printf("SDL_CreateRenderer failed for flags 0x%08x: %s\n", static_cast<unsigned int>(flags), SDL_GetError());
    }

    return nullptr;
}

SDL_Texture* load_texture(const char* path, SDL_Renderer* renderer) {
    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if (!rw) {
        printf("SDL_RWFromFile failed: %s\n", SDL_GetError());
        return NULL;
    }

    SDL_Surface* surface = IMG_Load_RW(rw, 1);
    if (!surface) {
        printf("IMG_Load_RW failed: %s\n", IMG_GetError());
        return NULL;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);

    if (!texture) {
        printf("SDL_CreateTextureFromSurface failed: %s\n", SDL_GetError());
    } else {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }

    return texture;
}

void launch_system_title(uint64_t titleID) {
    MCPTitleListType titleInfo;
    int32_t handle = MCP_Open();
    if (handle < 0) {
        printf("Failed to open MCP for title launch.\n");
        return;
    }

    auto err       = MCP_GetTitleInfo(handle, titleID, &titleInfo);
    MCP_Close(handle);

    if (SYSCheckTitleExists(titleID) && err == ACP_RESULT_SUCCESS) {
        ACPAssignTitlePatch(&titleInfo);
        SYSLaunchTitle(titleID);
    } else {
        printf("Title not found or ACP error.\n");
    }
}

int initialize() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed with error: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    if (RPXLoader_InitLibrary() != RPX_LOADER_RESULT_SUCCESS) {
        printf("RPX_LOADER failed with an error");
    } else {
        rpxloader_initialized = true;
    }

    // Handle window creation
    main_window = SDL_CreateWindow(
        "SwitchU",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        Config::WINDOW_WIDTH,
        Config::WINDOW_HEIGHT,
        0);

    if (!main_window) {
        printf("SDL_CreateWindow failed with error: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        printf("Failed to initialize SDL_image for PNG files: %s\n", IMG_GetError());
    }

    if (TTF_Init() == -1) {
        printf("TTF_Init failed: %s\n", TTF_GetError());
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    // Handle renderer creation
    main_renderer = create_renderer_with_fallback(main_window);
    if (!main_renderer) {
        printf("SDL_CreateRenderer failed with error: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    textRenderer = new TTFText(main_renderer);
    if (!textRenderer->loadFont(SD_CARD_PATH "switchU/fonts/font.ttf", 24, true)) {
        printf("Failed to load font!");
    }

    textures.circle = load_texture(SD_CARD_PATH "switchU/assets/ui_button.png", main_renderer);
    textures.circle_selection = load_texture(SD_CARD_PATH "switchU/assets/ui_button_selected.png", main_renderer);
    textures.circle_big = load_texture(SD_CARD_PATH "switchU/assets/ui_big_circle.png", main_renderer);
    textures.circle_big_selection = load_texture(SD_CARD_PATH "switchU/assets/ui_big_circle_selected.png", main_renderer);
    textures.battery_full = load_texture(SD_CARD_PATH "switchU/assets/battery/battery_full.png", main_renderer);
    textures.battery_three_fourths = load_texture(SD_CARD_PATH "switchU/assets/battery/battery_three_fourths.png", main_renderer);
    textures.battery_half = load_texture(SD_CARD_PATH "switchU/assets/battery/battery_half.png", main_renderer);
    textures.battery_needs_charge = load_texture(SD_CARD_PATH "switchU/assets/battery/battery_needs_charge.png", main_renderer);
    textures.battery_base = load_texture(SD_CARD_PATH "switchU/assets/battery/battery_base.png", main_renderer);
    textures.all_titles = load_texture(SD_CARD_PATH "switchU/assets/all_titles.png", main_renderer);

    textures.miiverse = load_texture(SD_CARD_PATH "switchU/assets/miiverse.png", main_renderer);
    textures.eshop = load_texture(SD_CARD_PATH "switchU/assets/eshop.png", main_renderer);
    textures.screenshots = load_texture(SD_CARD_PATH "switchU/assets/screenshots.png", main_renderer);
    textures.browser = load_texture(SD_CARD_PATH "switchU/assets/browser.png", main_renderer);
    textures.controller = load_texture(SD_CARD_PATH "switchU/assets/controller.png", main_renderer);
    textures.downloads = load_texture(SD_CARD_PATH "switchU/assets/downloads.png", main_renderer);
    textures.settings = load_texture(SD_CARD_PATH "switchU/assets/settings.png", main_renderer);
    textures.power = load_texture(SD_CARD_PATH "switchU/assets/power.png", main_renderer);
    textures.reference = load_texture(SD_CARD_PATH "switchU/assets/reference.png", main_renderer);

    textures.a_button = load_texture(SD_CARD_PATH "switchU/assets/buttons/button_a.png", main_renderer);
    textures.plus_button = load_texture(SD_CARD_PATH "switchU/assets/buttons/button_plus.png", main_renderer);

    get_user_information();
    account_initialized = true;

    return EXIT_SUCCESS;
}

void shutdown() {
    textures.destroyAll();

    unload_all_app_icons();
    apps.clear();

    if (textRenderer) {
        delete textRenderer;
        textRenderer = nullptr;
    }

    if (account_initialized) {
        nn::act::Finalize();
        account_initialized = false;
    }

    if (rpxloader_initialized) {
        RPXLoader_DeInitLibrary();
        rpxloader_initialized = false;
    }

    if (audio_initialized) {
        AXQuit();
        audio_initialized = false;
    }

    TTF_Quit();
    if (main_renderer) {
        SDL_DestroyRenderer(main_renderer);
        main_renderer = nullptr;
    }
    if (main_window) {
        SDL_DestroyWindow(main_window);
        main_window = nullptr;
    }
    SDL_Quit();
}

void input(Input &input) {
    Uint32 now = SDL_GetTicks();

    bool holding_left = (input.data.buttons_h & Input::STICK_L_LEFT || input.data.buttons_h & Input::BUTTON_LEFT);
    bool holding_right = (input.data.buttons_h & Input::STICK_L_RIGHT || input.data.buttons_h & Input::BUTTON_RIGHT);
    bool holding_up = (input.data.buttons_h & Input::STICK_L_UP || input.data.buttons_h & Input::BUTTON_UP);
    bool holding_down = (input.data.buttons_h & Input::STICK_L_DOWN || input.data.buttons_h & Input::BUTTON_DOWN);

    bool pressed_left = (input.data.buttons_d & Input::STICK_L_LEFT || input.data.buttons_d & Input::BUTTON_LEFT);
    bool pressed_right = (input.data.buttons_d & Input::STICK_L_RIGHT || input.data.buttons_d & Input::BUTTON_RIGHT);
    bool pressed_up = (input.data.buttons_d & Input::STICK_L_UP || input.data.buttons_d & Input::BUTTON_UP);
    bool pressed_down = (input.data.buttons_d & Input::STICK_L_DOWN || input.data.buttons_d & Input::BUTTON_DOWN);

    bool is_main_menu = (cur_menu == MENU_MAIN);
    bool is_middle_row = (cur_selected_row == ROW_MIDDLE);
    bool is_bottom_row = (cur_selected_row == ROW_BOTTOM);

    if (is_main_menu && (is_middle_row || is_bottom_row)) {
        int tile_count = is_middle_row ? get_middle_tile_count() : Config::TILE_COUNT_BOTTOM;

        if (pressed_left) {
            if (cur_selected_tile > 0) {
                cur_selected_tile--;
            } else if (is_middle_row) {
                cur_selected_tile = tile_count - 1;
            }
            left_hold_time = now;
            left_scrolling = true;
        } else if (holding_left && left_scrolling) {
            if (now - left_hold_time >= Config::SCROLL_INITIAL_DELAY) {
                if (cur_selected_tile > 0) {
                    cur_selected_tile--;
                    left_hold_time = now - (Config::SCROLL_INITIAL_DELAY - Config::SCROLL_REPEAT_INTERVAL);
                }
            }
        } else {
            left_scrolling = false;
        }

        if (pressed_right) {
            if (cur_selected_tile < tile_count - 1) {
                cur_selected_tile++;
            } else if (is_middle_row) {
                cur_selected_tile = 0;
            }
            right_hold_time = now;
            right_scrolling = true;
        } else if (holding_right && right_scrolling) {
            if (now - right_hold_time >= Config::SCROLL_INITIAL_DELAY) {
                if (cur_selected_tile < tile_count - 1) {
                    cur_selected_tile++;
                    right_hold_time = now - (Config::SCROLL_INITIAL_DELAY - Config::SCROLL_REPEAT_INTERVAL);
                }
            }
        } else {
            right_scrolling = false;
        }
    }

    if (pressed_up) {
        if (cur_menu == MENU_MAIN) {
            if (cur_selected_row > 0) cur_selected_row--;
            if (is_middle_row) cur_selected_tile = 0;
        } else if (cur_menu == MENU_USER) {
            if (cur_selected_subrow > 0) cur_selected_subrow--;
        }
        up_hold_time = now;
        up_scrolling = true;
    } else if (holding_up && up_scrolling && cur_menu != MENU_MAIN) {
        if (now - up_hold_time >= Config::SCROLL_INITIAL_DELAY) {
            if (cur_menu == MENU_USER && cur_selected_subrow > 0) {
                cur_selected_subrow--;
                up_hold_time = now - (Config::SCROLL_INITIAL_DELAY - Config::SCROLL_REPEAT_INTERVAL);
            }
        }
    } else {
        up_scrolling = false;
    }

    if (pressed_down) {
        if (cur_menu == MENU_MAIN) {
            if (cur_selected_row < 2) cur_selected_row++;
            if (cur_selected_tile >= 7) cur_selected_tile = 7;
        } else if (cur_menu == MENU_USER) {
            if (cur_selected_subrow < Config::settings_row_count - 1) cur_selected_subrow++;
        }
        down_hold_time = now;
        down_scrolling = true;
    } else if (holding_down && down_scrolling && cur_menu != MENU_MAIN) {
        if (now - down_hold_time >= Config::SCROLL_INITIAL_DELAY) {
            if (cur_menu == MENU_USER && cur_selected_subrow < Config::settings_row_count - 1) {
                cur_selected_subrow++;
                down_hold_time = now - (Config::SCROLL_INITIAL_DELAY - Config::SCROLL_REPEAT_INTERVAL);
            }
        }
    } else {
        down_scrolling = false;
    }

    if (input.data.buttons_d & Input::BUTTON_A) {
        if ((cur_selected_row == ROW_TOP)) {
            if (cur_menu == MENU_MAIN) {
                cur_menu = MENU_USER;
                cur_selected_row = ROW_MIDDLE;
            }
        } else if (cur_selected_row == ROW_MIDDLE) {
            if (cur_menu == MENU_MAIN) {
                if (static_cast<size_t>(cur_selected_tile) < apps.size()) {
                    if (apps[cur_selected_tile].titleid == 0) {
                        const char* launch_path = get_selected_app_path();
                        if (!launch_path) {
                            printf("No launch path for selected homebrew app.\n");
                            return;
                        }

                        printf("Launching app with path: %s\n", launch_path);
                        RPXLoaderStatus st = RPXLoader_LaunchHomebrew(launch_path);
                        printf("Launch status: %s\n", RPXLoader_GetStatusStr(st));
                    } else {
                        printf("Launching system app with title ID: %llu\n", static_cast<unsigned long long>(apps[cur_selected_tile].titleid));
                        launch_system_title(apps[cur_selected_tile].titleid);
                    }
                } else {
                    cur_menu = MENU_APPS;
                }
            } else if (cur_menu == MENU_USER) {
                if (cur_selected_subrow == 0) {
                    // Insert a profile subsubmenu thing
                }
            }
        } else {
            if (cur_selected_tile == 0) {
                printf("Launching MiiVerse !\n");
                _SYSSwitchTo(SysAppPFID::SYSAPP_PFID_MIIVERSE);
            } else if (cur_selected_tile == 1) {
                const char* launch_path = "wiiu/apps/appstore/appstore.wuhb";

                RPXLoaderStatus st = RPXLoader_LaunchHomebrew(launch_path);
                printf("Launch status: %s\n", RPXLoader_GetStatusStr(st));
            } else if (cur_selected_tile == 3) {
                printf("Launching the Browser !\n");
                SYSSwitchToBrowser(nullptr);
            } else if (cur_selected_tile == 5) {
                printf("Launching Download Manager !\n");
                _SYSSwitchTo(SysAppPFID::SYSAPP_PFID_DOWNLOAD_MANAGEMENT);
            } else if (cur_selected_tile == 6) {
                cur_menu = MENU_SETTINGS;
            }
        }
    }

    if (input.data.buttons_d & Input::BUTTON_B) {
        if (menuOpen) {
            menuOpen = false;
        }
        if (cur_menu != MENU_MAIN) {
            cur_menu = MENU_MAIN;
        }
    }

    if (input.data.buttons_d & Input::BUTTON_PLUS) {
        if ((cur_menu == MENU_MAIN) && (cur_selected_row == ROW_MIDDLE)) {
            menuOpen = true;
        }
    }

    // Developer note: make this an option toggle in the settings menu later
    if (input.data.buttons_d & Input::BUTTON_MINUS) {
        load_homebrew_titles = !load_homebrew_titles;
        printf("Toggled homebrew title loading: %s\n", load_homebrew_titles ? "ON" : "OFF");
        scan_apps(main_renderer);
        clamp_middle_selection();
    }

    if (cur_selected_row == ROW_TOP) {
        cur_selected_tile = 0;
    } else if (cur_selected_row == ROW_MIDDLE) {
        clamp_middle_selection();
    }

    // Only update camera if middle row is selected
    if ((cur_selected_row == ROW_MIDDLE) && (cur_menu == MENU_MAIN)) {
        const int outline_padding = 10;
        int selected_tile_x = cur_selected_tile * seperation_space;

        int tile_left = selected_tile_x - outline_padding;
        int tile_right = selected_tile_x + Config::spawn_box_size + outline_padding;

        if (tile_left < target_camera_offset_x) {
            target_camera_offset_x = tile_left;
        } else if (tile_right > target_camera_offset_x + Config::WINDOW_WIDTH) {
            target_camera_offset_x = tile_right - Config::WINDOW_WIDTH + 220;
        }

        // Clamp camera within bounds
        if (target_camera_offset_x < 0) target_camera_offset_x = 0;
        int max_camera_offset = std::max(0, seperation_space * (get_middle_tile_count() - 1) + Config::spawn_box_size + 220 - Config::WINDOW_WIDTH);
        if (target_camera_offset_x > max_camera_offset) target_camera_offset_x = max_camera_offset;
    }
}

void update() {
    static uint32_t icon_frame = 0;
    icon_frame++;

    render_set_color(main_renderer, COLOR_BACKGROUND);
    SDL_RenderClear(main_renderer);

    // Smooth camera movement
    const float camera_speed = 0.2f;
    camera_offset_x += (int)((target_camera_offset_x - camera_offset_x) * camera_speed);

    // === Middle Row (Camera-dependent) ===
    const int base_x = tiles_x - (Config::spawn_box_size / 2);
    const int base_y = tiles_y - 170;
    const int sub_base_y = tiles_y - 200;

    if (cur_menu == MENU_MAIN) {
        seperation_space = 270;
        const int middle_tile_count = get_middle_tile_count();
        const int all_software_index = middle_tile_count - 1;

        for (int i = 0; i < middle_tile_count; ++i) {
            int x = ((base_x + seperation_space * i) + 24) - camera_offset_x;
            int title_x = (((base_x + seperation_space * i) + 24) + (Config::spawn_box_size/2)) - camera_offset_x;
            bool should_draw = (x + Config::spawn_box_size >= -Config::spawn_box_size) &&
                               (x <= Config::WINDOW_WIDTH + Config::spawn_box_size);

            if (!should_draw && i != cur_selected_tile) {
                continue;
            }

            SDL_Rect icon_rect = { x, base_y, Config::spawn_box_size, Config::spawn_box_size };

            if (i < all_software_index) {
                if (i < (int)apps.size()) {
                    SDL_Texture* icon = ensure_app_icon_loaded(apps[i], main_renderer, icon_frame);
                    if (icon) {
                        render_icon_with_background(main_renderer, icon, apps[i].icon_bg_color, x, base_y, Config::spawn_box_size);
                    } else {
                        render_set_color(main_renderer, COLOR_UI_BOX);
                        SDL_RenderDrawRect(main_renderer, &icon_rect);
                    }
                } else {
                    render_set_color(main_renderer, COLOR_UI_BOX);
                    SDL_RenderDrawRect(main_renderer, &icon_rect);
                }
                if (i < (int)apps.size() && (i == cur_selected_tile && cur_selected_row == ROW_MIDDLE)) {
                    textRenderer->renderTextAt(apps[i].title, {0, 255, 245, 255}, title_x, base_y - 35, TextAlign::Center);
                }
            } else {
                SDL_RenderCopy(main_renderer, textures.circle_big, NULL, &icon_rect);
                SDL_RenderCopy(main_renderer, textures.all_titles, NULL, &icon_rect);
                if (i == cur_selected_tile && cur_selected_row == ROW_MIDDLE) {
                    textRenderer->renderTextAt("All Software", {0, 255, 245, 255}, title_x, base_y, TextAlign::Center);
                }
            }

            if (i == cur_selected_tile && cur_selected_row == ROW_MIDDLE) {
                const int outline_padding = 4;
                const int outline_thickness = 5;

                SDL_Rect outline_rect = {
                    x - outline_padding,
                    base_y - outline_padding,
                    Config::spawn_box_size + 2 * outline_padding,
                    Config::spawn_box_size + 2 * outline_padding
                };

                render_set_color(main_renderer, COLOR_CYAN);

                if (i < all_software_index) {
                    for (int t = 0; t < outline_thickness; ++t) {
                        SDL_Rect thick_rect = {
                            outline_rect.x - t,
                            outline_rect.y - t,
                            outline_rect.w + 2 * t,
                            outline_rect.h + 2 * t
                        };
                        SDL_RenderDrawRect(main_renderer, &thick_rect);
                    }
                } else {
                    SDL_RenderCopy(main_renderer, textures.circle_big_selection, NULL, &icon_rect);
                }
            }
        }

        trim_app_icon_cache(icon_frame, cur_selected_tile);
    } else if (cur_menu == MENU_USER) {
        seperation_space = 80;

        for (int i = 0; i < Config::settings_row_count; ++i) {
            int y = sub_base_y + seperation_space * i;

            render_set_color(main_renderer, COLOR_WHITE);

            if (i == cur_selected_subrow) {
                const int outline_padding = 2;
                const int outline_thickness = 3;

                SDL_Rect setting_outline_rect = {
                    base_x - outline_padding,
                    y - outline_padding,
                    128 * outline_padding,
                    32 * outline_padding
                };

                render_set_color(main_renderer, COLOR_BLUE);
                textRenderer->renderTextAt("None" /*Put the name of the option here later*/, {15, 206, 185, 255}, base_x + 8, y + 16, TextAlign::Left);

                for (int t = 0; t < outline_thickness; ++t) {
                    SDL_Rect thick_setting_rect = {
                        setting_outline_rect.x - t,
                        setting_outline_rect.y - t,
                        setting_outline_rect.w + 2 * t,
                        setting_outline_rect.h + 2 * t
                    };
                    SDL_RenderDrawRect(main_renderer, &thick_setting_rect);
                }
            } else {
                textRenderer->renderTextAt("None" /*Put the name of the option here later*/, {255, 255, 255, 255}, base_x + 8, y + 16, TextAlign::Left);
            }
        }
    }

    // === Bottom Row (Fixed Position, 6 centered circles) ===
    int bottom_y = Config::WINDOW_HEIGHT - 250;
    int total_width = (Config::TILE_COUNT_BOTTOM * Config::circle_diameter) + ((Config::TILE_COUNT_BOTTOM - 1) * 32);
    int start_x = (Config::WINDOW_WIDTH - total_width) / 2;

    if (cur_menu == MENU_MAIN) {
        SDL_Texture* bottom_icons[Config::TILE_COUNT_BOTTOM] = {
            textures.miiverse,
            textures.eshop,
            textures.screenshots,
            textures.browser,
            textures.controller,
            textures.downloads,
            textures.settings,
            textures.power
        };

        for (int i = 0; i < Config::TILE_COUNT_BOTTOM; ++i) {
            int cx = start_x + i * (Config::circle_diameter + 32);
            int cy = bottom_y;

            SDL_Rect dst_rect = { cx, cy, Config::circle_diameter * 2, Config::circle_diameter * 2 };

            SDL_RenderCopy(main_renderer, textures.circle, NULL, &dst_rect);

            if (i == cur_selected_tile && cur_selected_row == ROW_BOTTOM) {
                SDL_RenderCopy(main_renderer, textures.circle_selection, NULL, &dst_rect);
            }

            SDL_RenderCopy(main_renderer, bottom_icons[i], NULL, &dst_rect);
        }
    }

    // === Top Row (Fixed, 1 circle in top-right) ===
    int top_x = 40;
    int top_y = 16;

    SDL_Rect dst_rect_top = { top_x, top_y, 100, 100 };
    if ((cur_menu == MENU_MAIN) || (cur_menu == MENU_USER)) {
        SDL_RenderCopy(main_renderer, textures.circle, NULL, &dst_rect_top);

        if (cur_selected_tile == 0 && cur_selected_row == ROW_TOP) {
            SDL_RenderCopy(main_renderer, textures.circle_selection, NULL, &dst_rect_top);
        }
    }

    if (cur_menu == MENU_SETTINGS) {
        textRenderer->renderTextAt("System Settings", {255, 255, 255, 255}, 128, 32, TextAlign::Left);
    } else if (cur_menu == MENU_USER) {
        std::string title = std::string(ACCOUNT_ID) + "'s Page";
        textRenderer->renderTextAt(title.c_str(), {255, 255, 255, 255}, 128, 32, TextAlign::Left);
    } else {
        std::string battery;
        SDL_Rect battery_rect = { Config::WINDOW_WIDTH - 102, 51, 46, 28 };

        switch (battery_level) {
            case 0:
                SDL_SetTextureColorMod(textures.battery_full, 0, 255, 0);
                SDL_RenderCopy(main_renderer, textures.battery_full, NULL, &battery_rect);
                battery = "";
                break;
            case 1:
                SDL_RenderCopy(main_renderer, textures.battery_needs_charge, NULL, &battery_rect);
                battery = "0%";
                break;
            case 2:
                SDL_RenderCopy(main_renderer, textures.battery_needs_charge, NULL, &battery_rect);
                battery = "20%";
                break;
            case 3:
                SDL_RenderCopy(main_renderer, textures.battery_half, NULL, &battery_rect);
                battery = "30%";
                break;
            case 4:
                SDL_RenderCopy(main_renderer, textures.battery_half, NULL, &battery_rect);
                battery = "50%";
                break;
            case 5:
                SDL_RenderCopy(main_renderer, textures.battery_three_fourths, NULL, &battery_rect);
                battery = "80%";
                break;
            case 6:
                SDL_SetTextureColorMod(textures.battery_full, 255, 255, 255);
                SDL_RenderCopy(main_renderer, textures.battery_full, NULL, &battery_rect);
                battery = "100%";
                break;
            default:
                SDL_SetTextureColorMod(textures.battery_full, 247, 146, 30);
                SDL_RenderCopy(main_renderer, textures.battery_full, NULL, &battery_rect);
                battery = "???%";
                break;
        }

        textRenderer->renderTextAt(battery, {255, 255, 255, 255}, Config::WINDOW_WIDTH - 110, 53, TextAlign::Right);
        SDL_RenderCopy(main_renderer, textures.battery_base, NULL, &battery_rect);
    }

    // === Misc ===
    if (menuOpen) {
        render_set_color(main_renderer, COLOR_UI_BOX);
        render_rectangle(main_renderer, 100, 0, (Config::WINDOW_WIDTH - 200), Config::WINDOW_HEIGHT, true);
    }

    render_set_color(main_renderer, COLOR_WHITE);
    if (menuOpen) {
        SDL_RenderDrawLine(main_renderer, ((Config::WINDOW_WIDTH / 40) + 85), Config::WINDOW_HEIGHT - 70, ((Config::WINDOW_WIDTH / 1.025) - 85), Config::WINDOW_HEIGHT - 70);
    } else {
        SDL_RenderDrawLine(main_renderer, Config::WINDOW_WIDTH / 40, Config::WINDOW_HEIGHT - 70, Config::WINDOW_WIDTH / 1.025, Config::WINDOW_HEIGHT - 70);
    }

    if (cur_menu != MENU_MAIN) {
        SDL_RenderDrawLine(main_renderer, Config::WINDOW_WIDTH / 40, 90, Config::WINDOW_WIDTH / 1.025, 90);
    }
    if (menuOpen) {
        SDL_RenderDrawLine(main_renderer, ((Config::WINDOW_WIDTH / 40) + 85), 90, ((Config::WINDOW_WIDTH / 1.025) - 85), 90);
    }

    SDL_Rect button_a_rect_1 = { Config::WINDOW_WIDTH - 145, Config::WINDOW_HEIGHT - 60, 48, 48 };
    SDL_Rect button_a_rect_2 = { Config::WINDOW_WIDTH - 160, Config::WINDOW_HEIGHT - 60, 48, 48 };
    SDL_Rect button_plus_rect = { Config::WINDOW_WIDTH - 328, Config::WINDOW_HEIGHT - 60, 48, 48 };
    if (cur_menu == MENU_MAIN) {
        if ((cur_selected_row == ROW_TOP) || (cur_selected_row == ROW_BOTTOM)) {
            SDL_RenderCopy(main_renderer, textures.a_button, NULL, &button_a_rect_1);
            textRenderer->renderTextAt("OK", {255, 255, 255, 255}, Config::WINDOW_WIDTH - 96, Config::WINDOW_HEIGHT - 49, TextAlign::Left);
        } else {
            SDL_RenderCopy(main_renderer, textures.a_button, NULL, &button_a_rect_2);
            SDL_RenderCopy(main_renderer, textures.plus_button, NULL, &button_plus_rect);
            textRenderer->renderTextAt("Start", {255, 255, 255, 255}, Config::WINDOW_WIDTH - 115, Config::WINDOW_HEIGHT - 49, TextAlign::Left);
            textRenderer->renderTextAt("Options", {255, 255, 255, 255}, Config::WINDOW_WIDTH - 283, Config::WINDOW_HEIGHT - 49, TextAlign::Left);
        }
    }

    SDL_RenderPresent(main_renderer);
}

int main(int argc, char const *argv[]) {
    (void)argc;
    (void)argv;

    if (initialize() != EXIT_SUCCESS) {
        shutdown();
        return EXIT_FAILURE;
    }

    scan_apps(main_renderer);

    WHBProcInit();

    AXInit();
    audio_initialized = true;

    KPADInit();
    WPADEnableURCC(TRUE);

    CombinedInput baseInput;
    VPadInput vpadInput;
    WPADInput wpadInputs[4] = {
            WPAD_CHAN_0,
            WPAD_CHAN_1,
            WPAD_CHAN_2,
            WPAD_CHAN_3};

    while (WHBProcIsRunning()) {
        baseInput.reset();
        if (vpadInput.update(1280, 720)) {
            baseInput.combine(vpadInput);
        }
        for (auto &wpadInput : wpadInputs) {
            if (wpadInput.update(1280, 720)) {
                baseInput.combine(wpadInput);
            }
        }
        baseInput.process();
        // Read battery from VPAD, fallback to WPAD if invalid (Cemu compatibility)
        battery_level = vpadInput.data.battery;
        if (battery_level > 6) {
            for (auto &wpadInput : wpadInputs) {
                if (wpadInput.data.battery <= 6) {
                    battery_level = wpadInput.data.battery;
                    break;
                }
            }
        }

        input(baseInput);

        update();
    }

    shutdown();

    WHBProcShutdown();
    return EXIT_SUCCESS;
}


