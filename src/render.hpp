#include <SDL_render.h>
#include <SDL_ttf.h>

typedef struct {
    int rr;
    int gg;
    int bb;
    int aa;
} RenderColor;

const RenderColor COLOR_BACKGROUND = {45, 45, 45, SDL_ALPHA_OPAQUE};
const RenderColor COLOR_UI_BOX = {75, 75, 75, SDL_ALPHA_OPAQUE};
const RenderColor COLOR_WHITE = {255, 255, 255, SDL_ALPHA_OPAQUE};
const RenderColor COLOR_CYAN = {0, 255, 245, SDL_ALPHA_OPAQUE};
const RenderColor COLOR_BLUE = {60, 170, 230, SDL_ALPHA_OPAQUE};

void render_set_color(SDL_Renderer *renderer, RenderColor color);
void render_rectangle(SDL_Renderer *renderer, int xx, int yy, int ww, int hh, bool filled);
void render_circle(SDL_Renderer *renderer, int32_t centreX, int32_t centreY, int32_t radius, bool fill);
void render_icon_with_background(SDL_Renderer* renderer, SDL_Texture* icon, SDL_Color background, int x, int y, int box_size);
