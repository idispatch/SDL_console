#ifndef SDL_CONSOLE_H_
#define SDL_CONSOLE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <console.h>

void SDL_console_init(console_t console);
console_t SDL_console_get();
void SDL_console_run();
int SDL_console_run_frames(unsigned frameCount);
void SDL_console_done();

#ifdef __cplusplus
}
#endif

#endif /* SDL_CONSOLE_H_ */
