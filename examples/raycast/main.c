#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <SDL3/SDL.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#define P3D_RAYCAST_IMPLEMENTATION
#include "p3d_raycast.h"

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

#define RENDER_WIDTH 480
#define RENDER_HEIGHT 270

#define MAP_WIDTH 24
#define MAP_HEIGHT 24

#define SPRITE_COUNT 100

char map_str[MAP_WIDTH * MAP_HEIGHT] =
    "AAAAAAAAAAAAAAAAAAAAAAAA"
    "A......................A"
    "A......................A"
    "A......................A"
    "A.....BBBBB....C.C.C...A"
    "A.....B...G............A"
    "A.....B...v....C.H.C...A"
    "A.....B...G............A"
    "A.....BFhFB....C.C.C...A"
    "A......................A"
    "A......................A"
    "A......................A"
    "A......................A"
    "A......................A"
    "A......................A"
    "A......................A"
    "ADIDDDDID...BBBBBFhFBBBA"
    "AD.D....I...B.........BA"
    "AD....E.D...G..B.B.B..GA"
    "AI.D....I...v.........vJ"
    "AD.DDDIDD...G..B.B.B..GA"
    "AD..........B.........BA"
    "ADDIDDDID...BBBBBBBBBBBA"
    "AAAAAAAAAAAAAAAAAAAAAAAA";

p3drc_Tile legend[128] = {
    ['.'] = {0},
    ['A'] = P3DRC_WALL1(10),
    ['B'] = P3DRC_WALL1(2),
    ['C'] = P3DRC_WALL1(9),
    ['D'] = P3DRC_WALL1(0),
    ['E'] = P3DRC_WALL(2, 2, 2, 3),
    ['h'] = P3DRC_DOOR_H(4, 102), // 204 -> 0.8 open
    ['v'] = P3DRC_DOOR_V(4, 204),
    ['F'] = P3DRC_WALL(2, 5, 2, 5),
    ['G'] = P3DRC_WALL(5, 2, 5, 2),
    ['H'] = P3DRC_WALL1(8),
    ['I'] = P3DRC_WALL1(1),
    ['J'] = P3DRC_WALL1(6)
};

p3drc_Tile tiles[MAP_WIDTH * MAP_HEIGHT];
double z_buffer[RENDER_WIDTH];
SDL_Surface *atlas_img;

p3drc_Scene scene = {
    .map = {
        .tiles = tiles,
        .width = MAP_WIDTH,
        .height = MAP_HEIGHT,
        .floor_tex = 11,
        .ceiling_tex = 7
    },
    .atlas = { 0 },
    .light = {
        .red = 1.0, .green = 1.0, .blue = 1.0,
        .falloff = 1.0,
        .ambient = 1.0,
        .brightness = 1.0,
        .shade_strength = 0.5,
        .shade_face = P3DRC_SIDE_H
    },
    .fog = {
        .red = 1.0, .green = 1.0, .blue = 1.0,
        .density = 0.0
    }
};

p3drc_Camera camera = {
    .FOV = 0.66,
    .pos_x = 22, .pos_y = 12, .pos_z = 0.5,
    .dir_x = -1, .dir_y = 0
};

p3drc_Target target = {
    .pixels = NULL,
    .width = RENDER_WIDTH, .height = RENDER_HEIGHT,
    .z_buffer = z_buffer,
    .aspect_ratio = (double)RENDER_WIDTH / RENDER_HEIGHT
};

p3drc_Sprite sprites[SPRITE_COUNT];

struct app_state {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
};

SDL_AppResult raycast_init(struct app_state *app) {
    // initalize target texture
    app->texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, RENDER_WIDTH, RENDER_HEIGHT);
    if (app->texture == NULL) {
        SDL_Log("SDL_CreateTexture() failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetTextureScaleMode(app->texture, SDL_SCALEMODE_NEAREST);

    // load atlas image
    atlas_img = SDL_LoadPNG("atlas.png");
    if (atlas_img == NULL) {
        SDL_Log("SDL_LoadPNG() failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (atlas_img->format != SDL_PIXELFORMAT_RGBA32) {
        SDL_Log("Texture atlas does not match expected pixel format RGBA32");
        return SDL_APP_FAILURE;
    }

    scene.atlas = (p3drc_Atlas) {
        .pixels = atlas_img->pixels,
        .pitch = atlas_img->pitch,
        .width = atlas_img->w,
        .height = atlas_img->h,
        .subimage_size = 64,
        ._cols = atlas_img->w / 64,
        ._rows = atlas_img->h / 64
    };

    // build tile map from int codes
    for (int i = 0; i < MAP_WIDTH * MAP_HEIGHT; i++)
        tiles[i] = legend[(unsigned char)map_str[i]];

    // spawn random sprites
    for (int i = 0; i < SPRITE_COUNT; i++) {
        sprites[i].pos_x = SDL_randf() * MAP_WIDTH;
        sprites[i].pos_y = SDL_randf() * MAP_HEIGHT;
        sprites[i].texture = i % 4 + 12;
        sprites[i].flags = sprites[i].texture == 12 ? P3DRC_SPRITE_NO_LIGHTING : 0;
    }
    return SDL_APP_CONTINUE;
}

void raycast_render(struct app_state *app) {
    int pitch;
    SDL_LockTexture(app->texture, NULL, (void **)&target.pixels, &pitch);
    target.pitch = pitch;
    p3drc_render(&scene, &camera, &target, sprites, SPRITE_COUNT);
    SDL_UnlockTexture(app->texture);
    SDL_RenderTexture(app->renderer, app->texture, NULL, NULL);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    (void)argc; (void)argv;
    *appstate = malloc(sizeof(struct app_state));
    struct app_state *app = *appstate;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("Pseudo3D", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &app->window, &app->renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetRenderVSync(app->renderer, 1);

    return raycast_init(app);
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    struct app_state *app = appstate;

    const bool *key_states = SDL_GetKeyboardState(NULL);
    float rot_speed = 0.04;
    float move_speed = 0.06;

    if (key_states[SDL_SCANCODE_W]) {
        camera.pos_x += camera.dir_x * move_speed;
        camera.pos_y += camera.dir_y * move_speed;
    }

    if (key_states[SDL_SCANCODE_S]) {
        camera.pos_x -= camera.dir_x * move_speed;
        camera.pos_y -= camera.dir_y * move_speed;
    }

    if (key_states[SDL_SCANCODE_Q]) {
        camera.pos_z += move_speed / 3;
    }

    if (key_states[SDL_SCANCODE_E]) {
        camera.pos_z -= move_speed / 3;
    }

    if (key_states[SDL_SCANCODE_Z]) {
        camera.pitch += 0.01;
    }

    if (key_states[SDL_SCANCODE_C]) {
        camera.pitch -= 0.01;
    }

    if (key_states[SDL_SCANCODE_D]) {
        double old_dir_x = camera.dir_x;
        camera.dir_x = camera.dir_x * cos(rot_speed) - camera.dir_y * sin(rot_speed);
        camera.dir_y = old_dir_x * sin(rot_speed) + camera.dir_y * cos(rot_speed);
    }

    if (key_states[SDL_SCANCODE_A]) {
        double old_dir_x = camera.dir_x;
        camera.dir_x = camera.dir_x * cos(-rot_speed) - camera.dir_y * sin(-rot_speed);
        camera.dir_y = old_dir_x * sin(-rot_speed) + camera.dir_y * cos(-rot_speed);
    }

    static double acc_time = 0;
    static int acc_frames = 0;
    static Uint64 last_update = 0;
    static char fps_text[32] = "";

    Uint64 before = SDL_GetPerformanceCounter();
    raycast_render(app);
    Uint64 after = SDL_GetPerformanceCounter();

    acc_time += (double)(after - before) / SDL_GetPerformanceFrequency();
    acc_frames++;

    if (after - last_update >= SDL_GetPerformanceFrequency() / 6) {
        SDL_snprintf(fps_text, sizeof(fps_text), "%.0f fps", acc_frames / acc_time);
        acc_time = 0;
        acc_frames = 0;
        last_update = after;
    }

    SDL_SetRenderScale(app->renderer, 2.0f, 2.0f);
    SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
    SDL_RenderDebugText(app->renderer, 2, 2, fps_text);
    SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
    SDL_RenderPresent(app->renderer);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_KEY_DOWN:
        if (event->key.key == SDLK_ESCAPE)
            return SDL_APP_SUCCESS;
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        target.aspect_ratio = (double)event->window.data1 / event->window.data2;
        break;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)result;
    struct app_state *app = appstate;
    SDL_DestroyRenderer(app->renderer);
    SDL_DestroyWindow(app->window);
    SDL_DestroyTexture(app->texture);
    SDL_DestroySurface(atlas_img);
    SDL_Quit();
}
