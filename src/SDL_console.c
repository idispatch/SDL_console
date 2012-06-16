#include <SDL.h>
#include <SDL_thread.h>
#include <console.h>
#include "SDL_console.h"

static SDL_Thread  *g_consoleThread = NULL;
static SDL_Surface *g_screenSurface = NULL;
static console_t g_console = NULL;
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static unsigned char g_mouse_button = 0;
static bool g_ownConsole = false;
static Uint32 g_cursor_color;
static Uint32 g_palette[CONSOLE_NUM_PALETTE_ENTRIES];
static unsigned char * g_fontBitmap = NULL;

static const int SCREEN_BPP = 32;
#if defined(__ARM__) && defined(__QNXNTO__)
static const int SCREEN_WIDTH = 1024;
static const int SCREEN_HEIGHT = 600;
#else
static const int SCREEN_WIDTH = 800;
static const int SCREEN_HEIGHT = 600;
#endif

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
        unsigned char const * char_bitmap = console_get_char_bitmap(console, c);
        unsigned y;
        for (y = 0; y < charHeight; ++y) {
            unsigned cw = charWidth;
            do {
                unsigned x;
                unsigned char mask = 0x80;
                unsigned n = cw <= 8 ? cw : 8;
                unsigned char b = *char_bitmap;
                for (x = 0; x < n; ++x, mask >>= 1)
                    *char_data_ptr++ = (mask & b) ? 255 : 0;
                char_bitmap += 1;
                cw -= n;
            } while (cw > 0);
        }
    }
}

static void render_cursor(console_t console, SDL_Surface * dst, Sint16 x, Sint16 y) {
    if(console_cursor_is_visible(console)) {
        unsigned char_width = console_get_char_width(console);
        unsigned char_height = console_get_char_height(console);
        SDL_Rect d;
        d.x = x * char_width;
        d.y = (y + 1) * char_height - 3;
        d.w = char_width;
        d.h = 2;
        SDL_FillRect(dst, &d, g_cursor_color);
    }
}

static void render_char(console_t console, SDL_Surface * dst, Sint16 x, Sint16 y, unsigned char c, unsigned char a) {
    if (SDL_LockSurface(dst) != 0)
        return;
    const Uint32 foreColor = g_palette[a & 0xf];
    const Uint32 backColor = g_palette[a >> 4];
#ifdef _DEBUG
#if 0
    fprintf(stdout,
            "render_char: [%d,%d]=%u[attr:0x%02x, fore:0x%06X, back:0x%06X]\n",
            (unsigned)x,
            (unsigned)y,
            (unsigned)c,
            (unsigned)a,
            foreColor,
            backColor);
    fflush(stdout);
#endif
#endif
    const unsigned bpp = dst->format->BytesPerPixel;
    unsigned cx;
    unsigned cy;
    const unsigned charWidth = console_get_char_width(console);
    const unsigned charHeight = console_get_char_height(console);
    Uint8 * pixels = (Uint8 *)dst->pixels + y * charHeight * dst->pitch + x * charWidth * bpp;
    const Uint16 pitch = dst->pitch;
    const unsigned char * bitmap_data = g_fontBitmap + ((unsigned)c) * charWidth * charHeight * sizeof(unsigned char);
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
#if 1
static void render_font_surface(console_t console, SDL_Surface * dst) {
    unsigned y;
    unsigned x;
    for(y = 0; y < 16; ++y) {
        for(x = 0; x < 16; ++x) {
            render_char(console, dst, x, y, y * 16 + x, 7);
        }
    }
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
#if 0
        fprintf(stdout,
                "UPDATE_CHAR: [x=%d,y=%d,c=%u,a=0x%02x]\n",
                u->data.u_char.x,
                u->data.u_char.y,
                (unsigned)(u->data.u_char.c),
                (unsigned)(u->data.u_char.a));
        fflush(stdout);
#endif
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
#if 0
        fprintf(stdout, "CURSOR_POSITION: [x=%d,y=%d->x=%d,y=%d] v=%d\n",
                u->data.u_cursor.x,
                u->data.u_cursor.y,
                console_get_cursor_x(console),
                console_get_cursor_y(console),
                (int)u->data.u_cursor.cursor_visible);
        fflush(stdout);
#endif
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

static int console_render_loop(void * data) {
    while (SDL_console_run_frames(1) >= 0);
    return 0;
}

int SDL_console_get_mouse_x() {
    return g_mouse_x;
}

int SDL_console_get_mouse_y() {
    return g_mouse_y;
}

unsigned char SDL_console_get_mouse_button() {
    return g_mouse_button;
}

int SDL_console_run_frames(unsigned frameCount) {
    for(;frameCount > 0; --frameCount) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch(event.type) {
            case SDL_QUIT:
#ifdef _DEBUG
                fprintf(stdout, "%s: SDL_QUIT\n", __FUNCTION__);
                fflush(stdout);
#endif
                return -1;
            case SDL_MOUSEBUTTONDOWN:
#ifdef _DEBUG
                fprintf(stdout,
                        "%s: SDL_MOUSEBUTTONDOWN, which=%d, [x=%d,y=%d,b=%d]\n",
                        __FUNCTION__,
                        event.button.which,
                        event.button.x / console_get_char_width(g_console),
                        event.button.y / console_get_char_height(g_console),
                        event.button.button);
                fflush(stdout);
#endif
                if(event.button.which == 0) {
                    g_mouse_x = event.button.x / console_get_char_width(g_console);
                    g_mouse_y = event.button.y / console_get_char_height(g_console);
                    g_mouse_button |= event.button.button;
                }
                break;
            case SDL_MOUSEBUTTONUP:
#ifdef _DEBUG
                fprintf(stdout,
                        "%s: SDL_MOUSEBUTTONUP, which=%d, [x=%d,y=%d,b=%d]\n",
                        __FUNCTION__,
                        event.button.which,
                        event.button.x / console_get_char_width(g_console),
                        event.button.y / console_get_char_height(g_console),
                        event.button.button);
                fflush(stdout);
#endif
                if(event.button.which == 0) {
                    g_mouse_x = event.button.x / console_get_char_width(g_console);
                    g_mouse_y = event.button.y / console_get_char_height(g_console);
                    g_mouse_button &= ~event.button.button;
                }
                break;
            case SDL_MOUSEMOTION:
#ifdef _DEBUG
                fprintf(stdout,
                        "%s: SDL_MOUSEMOTION, which=%d, [x=%d,y=%d]\n",
                        __FUNCTION__,
                        event.motion.which,
                        event.motion.x / console_get_char_width(g_console),
                        event.motion.y / console_get_char_height(g_console));
                fflush(stdout);
#endif
                if(event.motion.which == 0) {
                    g_mouse_x = event.motion.x / console_get_char_width(g_console);
                    g_mouse_y = event.motion.y / console_get_char_height(g_console);
                    g_mouse_button = event.motion.state;
                }
                break;
            case SDL_KEYDOWN:
#ifdef _DEBUG
                fprintf(stdout,
                        "%s: %s, %s, which=%d [scancode=0x%02x, sym=%d, mode=%d, unicode=0x%X]\n",
                        __FUNCTION__,
                        (event.key.type == SDL_KEYDOWN ? "SDL_KEYDOWN" : "SDL_KEYUP"),
                        (event.key.state == SDL_PRESSED ? "SDL_PRESSED" : "SDL_RELEASED"),
                        event.key.which,
                        event.key.keysym.scancode,
                        event.key.keysym.sym,
                        event.key.keysym.mod,
                        event.key.keysym.unicode);
                fflush(stdout);
#endif
#if 0
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
#endif
                break;
            case SDL_KEYUP:
#ifdef _DEBUG
                fprintf(stdout,
                        "%s: %s, %s, which=%d [scancode=0x%02x, sym=%d, mode=%d, unicode=0x%X]\n",
                        __FUNCTION__,
                        (event.key.type == SDL_KEYDOWN ? "SDL_KEYDOWN" : "SDL_KEYUP"),
                        (event.key.state == SDL_PRESSED ? "SDL_PRESSED" : "SDL_RELEASED"),
                        event.key.which,
                        event.key.keysym.scancode,
                        event.key.keysym.sym,
                        event.key.keysym.mod,
                        event.key.keysym.unicode);
                fflush(stdout);
#endif
                break;
            default:
#ifdef _DEBUG
                fprintf(stdout, "%s: event type %d\n", __FUNCTION__, (int)event.type);
                fflush(stdout);
#endif
                break;
            }
        }
#ifdef _DEBUG
#if 0
        render_font_surface(g_console, g_screenSurface);
#endif
#endif
        console_blink_cursor(g_console);
        SDL_Flip(g_screenSurface);
    }
    return 0;
}

console_t SDL_console_init(console_t console, font_id_t font){
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
#ifdef _DEBUG
        printf("%s: Unable to initialize SDL: %s\n", __FUNCTION__, SDL_GetError());
#endif
        exit(1);
    }

    Uint32 flags = SDL_HWSURFACE | SDL_FULLSCREEN | SDL_NOFRAME | SDL_DOUBLEBUF;
    g_screenSurface = SDL_SetVideoMode(SCREEN_WIDTH,
                                       SCREEN_HEIGHT,
                                       SCREEN_BPP,
                                       flags);

    if (g_screenSurface == NULL) {
#ifdef _DEBUG
        printf("%s: Unable to set %dx%dx%d video mode: %s\n",
                __FUNCTION__,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                SCREEN_BPP,
                SDL_GetError());
#endif
        exit(1);
    }

    if(console == NULL) {
        g_console = console_alloc(SCREEN_WIDTH, SCREEN_HEIGHT, font);
        g_ownConsole = true;
    } else {
        g_console = console;
        g_ownConsole = false;
    }
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
    return g_console;
}

console_t SDL_console_get() {
    return g_console;
}

void SDL_console_run() {
    console_render_loop(NULL);
}

void SDL_console_done() {
    if(g_consoleThread) {
        SDL_WaitThread(g_consoleThread, NULL);
        g_consoleThread = NULL;
    }

    if(g_console) {
        console_set_callback(g_console, NULL, NULL);
        if(g_ownConsole) {
            console_free(g_console);
            g_ownConsole = false;
        }
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
