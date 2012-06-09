#include <SDL.h>
#include <SDL_thread.h>
#include <console.h>
#include "SDL_console.h"

static SDL_Thread  *g_consoleThread = NULL;
static SDL_Surface *g_screenSurface = NULL;
static console_t g_console = NULL;

static Uint32 g_cursor_color;
static Uint32 g_palette[CONSOLE_NUM_PALETTE_ENTRIES];
static unsigned char * g_fontBitmap = NULL;

static const int SCREEN_WIDTH = 1024;
static const int SCREEN_HEIGHT = 600;
static const int SCREEN_BPP = 32;

static Uint32 render_get_background_color(console_t console) {
    return g_palette[console_get_background_color(console)];
}

static void render_init_font(console_t console, SDL_Surface * dst) {
    if(g_fontBitmap) {
        free(g_fontBitmap);
        g_fontBitmap = NULL;
    }
    unsigned c;
    unsigned charWidth = console_get_char_width(console);
    unsigned charHeight = console_get_char_height(console);
    size_t bytes = 256 * charWidth * charHeight * sizeof(unsigned char);
    g_fontBitmap = malloc(bytes);
    unsigned char * char_data_ptr = g_fontBitmap;
    for(c = 0; c < 256; c++) {
        unsigned char const * char_bitmap = console_get_char_bitmap(console, (unsigned char)c);
        unsigned y;
        for (y = 0; y < charHeight; ++y) {
            unsigned char mask = 0x80;
            unsigned char b = *char_bitmap;
            unsigned x;
            for (x = 0; x < charWidth; ++x, mask >>= 1)
                *char_data_ptr++ = (mask & b) ? 255 : 0;
            char_bitmap += 1;
        }
    }
}

static void render_cursor(console_t console, SDL_Surface * dst, Sint16 x, Sint16 y) {
    unsigned char_width = console_get_char_width(console);
    unsigned char_height = console_get_char_height(console);
    SDL_Rect d;
    d.x = x * char_width;
    d.y = (y + 1) * char_height - 3;
    d.w = char_width;
    d.h = 2;
    SDL_FillRect(dst, &d, g_cursor_color);
}

static void render_char(console_t console, SDL_Surface * dst, Sint16 x, Sint16 y, char c, unsigned char a) {
    Uint32 foreColor = g_palette[a & 0xf];
    Uint32 backColor = g_palette[a >> 4];
    if (SDL_LockSurface(dst) != 0)
        return;
    unsigned bpp = dst->format->BytesPerPixel;
    unsigned cx;
    unsigned cy;
    unsigned charWidth = console_get_char_width(console);
    unsigned charHeight = console_get_char_height(console);
    Uint8 * pixels = (Uint8 *)dst->pixels + y * charHeight * dst->pitch + x * charWidth * bpp;
    Uint16 pitch = dst->pitch;
    unsigned char * bitmap_data = g_fontBitmap + c * charWidth * charHeight * sizeof(unsigned char);
    for (cy = 0; cy < charHeight; ++cy) {
        Uint32 * dst_ptr = (Uint32*)pixels;
        for (cx = 0; cx < charWidth; ++cx) {
            *dst_ptr++ = (*bitmap_data++) ? foreColor : backColor;
        }
        pixels += pitch;
    }
    SDL_UnlockSurface(dst);
}

#ifdef _DEBUG
#if 0
static void render_font_surface(console_t console, SDL_Surface * dst) {
    SDL_Rect srect;
    SDL_Rect drect;
    srect.x = 0;
    srect.y = 0;
    srect.w = console_get_char_width(console) * 16;
    srect.h = console_get_char_height(console) * 16;
    drect.x = srect.x;
    drect.y = srect.y;
    drect.w = srect.w;
    drect.h = srect.h;
    SDL_BlitSurface(g_fontSurface, &srect, dst, &drect);
}
#endif
#endif

static void console_render_callback(console_t console, console_update_t * u, void * data) {
    SDL_Surface * screen = (SDL_Surface *)data;
    switch(u->type) {
    case CONSOLE_UPDATE_CHAR:
        render_char(console,
                    screen,
                    u->data.u_char.x,
                    u->data.u_char.y,
                    u->data.u_char.c,
                    u->data.u_char.a);
#ifdef _DEBUG
        fprintf(stdout, "CONSOLE_UPDATE_CHAR: [%d,%d]=%c\n", u->data.u_char.x, u->data.u_char.y, u->data.u_char.c);
        fflush(stdout);
#endif
        break;
    case CONSOLE_UPDATE_ROWS:
        break;
    case CONSOLE_UPDATE_SCROLL: {
            SDL_Rect src;
            SDL_Rect dst;
            unsigned console_width = console_get_width(console);
            unsigned console_height = console_get_height(console);
            unsigned char_width = console_get_char_width(console);
            unsigned char_height = console_get_char_height(console);
            src.x = dst.x = 0;
            src.w = dst.w = char_width * console_width;
            src.h = dst.h = u->data.u_scroll.n * char_height;
            src.y = u->data.u_scroll.y2 * char_height;
            dst.y = u->data.u_scroll.y1 * char_height;
            SDL_BlitSurface(screen, &src, screen, &dst);
            dst.y = (console_height * char_height) - (console_get_height(console) - u->data.u_scroll.n) * char_height;
            dst.h = (u->data.u_scroll.y2 - u->data.u_scroll.y1) * char_height;
            SDL_FillRect(screen, &dst, render_get_background_color(console));
            SDL_UpdateRect(screen, 0, 0, 0, 0);
        }
        break;
    case CONSOLE_UPDATE_CURSOR_VISIBILITY:
        if(u->data.u_cursor.cursor_visible) {
            render_cursor(console,
                          screen,
                          console_get_cursor_x(console),
                          console_get_cursor_y(console));
        } else {
            render_char(console,
                        screen,
                        console_get_cursor_x(console),
                        console_get_cursor_y(console),
                        console_get_character_at(console,
                                                 console_get_cursor_x(console),
                                                 console_get_cursor_y(console)),
                        console_get_attribute_at(console,
                                                 console_get_cursor_x(console),
                                                 console_get_cursor_y(console)));
        }
        break;
    case CONSOLE_UPDATE_CURSOR_POSITION:
#ifdef _DEBUG
        fprintf(stdout, "CONSOLE_UPDATE_CURSOR_POSITION: [%d,%d->%d,%d]=%d\n",
                u->data.u_cursor.x, u->data.u_cursor.y,
                console_get_cursor_x(console),
                console_get_cursor_y(console),
                (int)u->data.u_cursor.cursor_visible);
        fflush(stdout);
#endif
        render_char(console,
                    screen,
                    u->data.u_cursor.x,
                    u->data.u_cursor.y,
                    console_get_character_at(console,
                                             u->data.u_cursor.x,
                                             u->data.u_cursor.y),
                    console_get_attribute_at(console,
                                             u->data.u_cursor.x,
                                             u->data.u_cursor.y));
        if(u->data.u_cursor.cursor_visible) {
            render_cursor(console,
                          screen,
                          console_get_cursor_x(console),
                          console_get_cursor_y(console));
        }
        break;
    }
}

static int console_render_thread(void * data) {
    bool bDone = false;
    while (!bDone) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch(event.type) {
            case SDL_QUIT:
                bDone = true;
                break;
            case SDL_KEYDOWN:
                if(isascii(event.key.keysym.unicode) &&
                   isprint(event.key.keysym.unicode & 0xff)) {
                    static unsigned char c = 0;
                    console_print_char(g_console, (char)(event.key.keysym.unicode & 0xff));
                    console_set_attribute(g_console, c++);
                } else {
                    switch(event.key.keysym.sym) {
                    case SDLK_LEFT: {
                            unsigned x = console_get_cursor_x(g_console);
                            if(x > 0)
                                --x;
                            console_cursor_goto_xy(g_console, x, console_get_cursor_y(g_console));
                        }
                        break;
                    case SDLK_DOWN:{
                            unsigned y = console_get_cursor_y(g_console);
                            if(y < console_get_height(g_console))
                                ++y;
                            console_cursor_goto_xy(g_console, console_get_cursor_x(g_console), y);
                        }
                        break;
                    case SDLK_UP:{
                            unsigned y = console_get_cursor_y(g_console);
                            if(y > 0)
                                --y;
                            console_cursor_goto_xy(g_console, console_get_cursor_x(g_console), y);
                        }
                        break;
                    case SDLK_RIGHT: {
                            unsigned x = console_get_cursor_x(g_console);
                            if(x < console_get_width(g_console))
                                ++x;
                            console_cursor_goto_xy(g_console, x, console_get_cursor_y(g_console));
                        }
                        break;
                    case SDLK_RETURN:
                        console_print_char(g_console, '\n');
                        break;
                    }
                    break;
                }
                break;
            }
        }

        console_blink_cursor(g_console);
        SDL_Flip(g_screenSurface);
    }
    return 0;
}

void SDL_console_init(){
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("Unable to initialize SDL: %s\n", SDL_GetError());
        exit(1);
    }

    Uint32 flags = SDL_HWSURFACE | SDL_FULLSCREEN | SDL_NOFRAME | SDL_DOUBLEBUF;
    g_screenSurface = SDL_SetVideoMode(SCREEN_WIDTH,
                                       SCREEN_HEIGHT,
                                       SCREEN_BPP,
                                       flags);

    if (g_screenSurface == NULL) {
        printf("Unable to set %dx%dx%d video: %s\n",
                SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_BPP, SDL_GetError());
        exit(1);
    }

    SDL_ShowCursor(0);

    g_console = console_alloc(SCREEN_WIDTH, SCREEN_HEIGHT);
    console_set_font(g_console, FONT_8x16);
    console_set_callback(g_console, console_render_callback, g_screenSurface);
    console_clear(g_console);
    console_set_attribute(g_console, 1);

    int i;
    console_rgb_t rgb[CONSOLE_NUM_PALETTE_ENTRIES];
    console_get_palette(g_console, &rgb[0]);
    for(i=0; i<CONSOLE_NUM_PALETTE_ENTRIES; i++) {
        g_palette[i] = SDL_MapRGB(g_screenSurface->format, rgb[i].r, rgb[i].g, rgb[i].b);
    }

    g_cursor_color = SDL_MapRGB(g_screenSurface->format, 255, 255, 255);
    render_init_font(g_console, g_screenSurface);
#if 0
    g_consoleThread = SDL_CreateThread(console_render_thread, NULL);
    if (g_consoleThread == NULL ) {
        fprintf(stderr, "Unable to create thread: %s\n", SDL_GetError());
        fflush(stderr);
        return;
    }
#endif
}

void SDL_console_run() {
    console_render_thread(NULL);
}

void SDL_console_done() {
    if(g_consoleThread) {
        SDL_WaitThread(g_consoleThread, NULL);
        g_consoleThread = NULL;
    }

    if(g_console) {
        console_set_callback(g_console, NULL, NULL);
        console_free(g_console);
        g_console = NULL;
    }
    if(g_screenSurface) {
        SDL_FreeSurface(g_screenSurface);
        g_screenSurface = NULL;
    }

    if(g_fontBitmap) {
        free(g_fontBitmap);
        g_fontBitmap = NULL;
    }
    memset(g_palette, 0, sizeof(g_palette));

    SDL_Quit();
}
