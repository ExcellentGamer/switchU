#include "font.hpp"
#include <iostream>

TTFText::TTFText(SDL_Renderer* renderer) : renderer(renderer), font(nullptr) {
}

TTFText::~TTFText() {
    if (font) TTF_CloseFont(font);
}

bool TTFText::loadFont(const std::string& path, int size, bool bold) {
    if (!TTF_WasInit() && TTF_Init() == -1) {
        std::cerr << "TTF_Init failed: " << TTF_GetError() << std::endl;
        return false;
    }

    font = TTF_OpenFont(path.c_str(), size);
    if (!font) {
        std::cerr << "Failed to load font: " << TTF_GetError() << std::endl;
        return false;
    }

    if (bold) {
        TTF_SetFontStyle(font, TTF_STYLE_BOLD);
    }

    return true;
}

SDL_Texture* TTFText::renderText(const std::string& message, SDL_Color color) {
    if (!font) return nullptr;
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, message.c_str(), color);
    if (!surface) return nullptr;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

void TTFText::renderTextAt(const std::string& message, SDL_Color color, int x, int y, TextAlign align) {
    SDL_Texture* texture = renderText(message, color);
    if (!texture) return;

    int w, h;
    SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);

    SDL_Rect dst = { x, y, w, h };

    // Adjust x based on alignment
    switch (align) {
        case TextAlign::Center:
            dst.x -= w / 2;
            break;
        case TextAlign::Right:
            dst.x -= w;
            break;
        case TextAlign::Left:
        default:
            break;
    }

    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
}
