#ifndef SDL_CONSOLE_H_
#define SDL_CONSOLE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <console.h>

console_t SDL_console_init(console_t console, font_id_t font);
console_t SDL_console_get();
void SDL_console_run();
int SDL_console_run_frames(unsigned frameCount);
int SDL_console_get_mouse_x();
int SDL_console_get_mouse_y();
unsigned char SDL_console_get_mouse_button();
void SDL_console_done();

#ifdef __cplusplus
}
#endif

#endif /* SDL_CONSOLE_H_ */
