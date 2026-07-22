/* p3d_raycast - v0.1 - simple raycast render engine - Noah Wagner
 * See end of file for license information.
 *
 * Do this:
 *     #define P3D_RAYCAST_IMPLEMENTATION
 * before you include this file in *one* C file to create the implementation.
 *
 * Optionally #define P3DRC_ASSERT and P3DRC_QSORT before the #include to
 * replace assert and stdlib qsort.
 *
 * The library performs no heap allocation and has no dependencies beyond
 * libc. All buffers (map tiles, atlas pixels, output pixels, z buffer,
 * sprites) are provided by the caller. Output is a software-rendered RGBA32
 * pixel buffer; display it with any graphics library.
 */

/*
 * DOCUMENTATION
 *
 * Coordinates & units
 *
 *     The map is a grid of NxM tiles in the x/y plane; world height runs 0
 *     (floor) to 1 (ceiling). camera pos_z is the eye height (0.5 = centered).
 *     Camera dir must be a unit vector. Pitch shifts the screen vertically.
 *     Horizontal FOV = 2*atan(aspect_ratio / (2*FOV)).
 *
 * Texture atlas
 *
 *     One RGBA32 image holding a grid of subimage_size sized tiles, where
 *     cols = width / subimage_size and rows = height / subimage_size. Texture n
 *     is at column (n % cols), row (n / cols). All texture references (tiles,
 *     sprites, floor, ceiling) index this grid.
 *
 *
 * Draw order
 *
 *     Scenes should be drawn in the following order:
 *     1. Planes: Must be drawn first because they override the canvas and do
 *        not write to the depth buffer.
 *     2. Walls: Must be drawn second since they generate the per-column
 *        z-buffer required by sprite rendering.
 *     3. Sprites: Must be sorted using p3drc_sort_sprites and drawn last.
 *
 *     For convenience, you can use the p3drc_render_sprites or p3drc_render
 *     functions to handle this ordering automatically.
 *
 * Example
 *
 *     p3drc_Scene scene = { map, atlas, light, fog };
 *     p3drc_Camera cam = { .FOV = 0.66, .pos_x = 12, .pos_y = 12, .pos_z = 0.5,
 *                          .dir_x = 1, .dir_y = 0 };
 *     p3drc_Target target = { pixels, width * 4, width, height, z_buffer,
 *                             (double)width / height };
 *     while (running) {
 *         // ... game update ...
 *         // ... build p3drc_Sprite *sprites array from scene entities ...
 *         // ... set sprite_count = |sprites| ...
 *         p3drc_render(&scene, &cam, &target, sprites, sprite_count);
 *         // display the modified target.pixels buffer to the screen
 *     }
 *
 * Multi-threaded example
 *
 *     Give each thread a copy of the target with a disjoint [start, end)
 *     column range; pixels and z_buffer stay shared. Sort once, then render
 *     slices in parallel:
 *
 *     // ... setup scene, cam, target, sprites, and sprite_count ...
 *
 *     // ... game update ...
 *     p3drc_sort_sprites(&cam, sprites, sprite_count);
 *     for (int t = 0; t < num_threads; t++) {
 *         p3drc_Target slice = target;
 *         slice.start = t * width / num_threads;
 *         slice.end = (t + 1) * width / num_threads;
 *         MyThreadArgs args = {&scene, &cam, &slice, sprites, sprite_count};
 *         // ... call p3drc_render_slice with args on a separate thread ...
 *     }
 *     // ... join all render threads ...
 *
 * Limitations
 *
 *     - single-height walls: floor fixed at 0, ceiling at 1
 *     - sprites are one tile in size and can't be moved vertically
 *     - pitch rotation is a horizon shear: large pitch distorts
 *     - software rendered: can be slow at higher resolutions
 */

#ifndef P3D_RAYCAST_H
#define P3D_RAYCAST_H
#include <stdint.h>

#ifndef P3DRC_ASSERT
#include <assert.h>
#define P3DRC_ASSERT assert
#endif

enum p3drc_tile_type {
    P3DRC_TILE_EMPTY,
    P3DRC_TILE_WALL,
    P3DRC_TILE_DOOR_H,
    P3DRC_TILE_DOOR_V
};

enum p3drc_face {
    P3DRC_FACE_N,
    P3DRC_FACE_E,
    P3DRC_FACE_S,
    P3DRC_FACE_W
};

enum p3drc_side {
    P3DRC_SIDE_V,
    P3DRC_SIDE_H
};

#define P3DRC_OPEN_MAX 255

typedef struct p3drc_tile {
    uint8_t type;    // enum p3drc_tile_type
    uint8_t open;    // doors only: 0 = closed, P3DRC_OPEN_MAX = fully open
    uint16_t tex[4]; // texture per face, indexed by enum p3drc_face
} p3drc_Tile;

#define P3DRC_WALL(n, e, s, w) ((p3drc_Tile){ .type = P3DRC_TILE_WALL, .tex = {(n), (e), (s), (w)} })
#define P3DRC_WALL1(t) P3DRC_WALL(t, t, t, t)
#define P3DRC_DOOR_H(t, open_amt) ((p3drc_Tile){ .type = P3DRC_TILE_DOOR_H, .open = (open_amt), .tex = {(t), (t), (t), (t)} })
#define P3DRC_DOOR_V(t, open_amt) ((p3drc_Tile){ .type = P3DRC_TILE_DOOR_V, .open = (open_amt), .tex = {(t), (t), (t), (t)} })

#define P3DRC_SPRITE_INVISIBLE   (1 << 0)
#define P3DRC_SPRITE_NO_LIGHTING (1 << 1)
#define P3DRC_SPRITE_NO_FOG      (1 << 2)

typedef struct p3drc_hit {
    p3drc_Tile tile;      // zeroed if the ray left the map
    double depth;         // distance to the hit in units of |dir|
    enum p3drc_side side;
    enum p3drc_face face;
} p3drc_Hit;

typedef struct p3drc_map {
    p3drc_Tile *tiles;
    int width, height;
    int floor_tex, ceiling_tex; // negative means "not rendered"
} p3drc_Map;

typedef struct p3drc_sprite {
    double pos_x, pos_y;
    double _dist;        // set by p3drc_sort_sprites
    uint16_t texture;
    uint8_t flags;       // P3DRC_SPRITE_* bits
} p3drc_Sprite;

typedef struct p3drc_atlas {
    uint8_t *pixels;   // RGBA32
    int pitch;         // bytes per row; width * 4 if tightly packed
    int width, height;
    int subimage_size; // texture tile size in pixels
} p3drc_Atlas;

typedef struct p3drc_light {
    double red, green, blue; // light color multipliers, [0, 1]
    double falloff;          // light = falloff / depth
    double ambient;          // lower clamp on light
    double brightness;       // upper clamp on light
    double shade_strength;   // multiplier applied to shade_face walls
    enum p3drc_side shade_face;
} p3drc_Light;

typedef struct p3drc_fog {
    double red, green, blue; // fog color, [0, 1]
    double density;          // fog = depth * density, clamped to 1
} p3drc_Fog;

typedef struct p3drc_scene {
    p3drc_Map map;
    p3drc_Atlas atlas;
    p3drc_Light light;
    p3drc_Fog fog;
} p3drc_Scene;

typedef struct p3drc_camera {
    double FOV;                 // see DOCUMENTATION; 0.66 is a good default
    double pitch;               // vertical rotation, in screen heights
    double pos_x, pos_y, pos_z; // pos_z: 0 = floor, 1 = ceiling
    double dir_x, dir_y;        // must be a unit vector
} p3drc_Camera;

typedef struct p3drc_target {
    uint8_t *pixels;     // RGBA32
    int pitch;           // bytes per row; width * 4 if tightly packed
    int width, height;
    double *z_buffer;    // at least width elements
    double aspect_ratio; // should match the on-screen aspect ratio
    int start, end;      // column slice, see multi-threaded example
} p3drc_Target;

/* Casts one ray through the map (weapon hitscans, line of sight, ...).
 * Pass a unit dir to get depth in world units. Doors count as hit only where
 * the ray crosses the closed part of the door slab.
 *
 * You may also pass the start and end points. Collision check is simple:
 * if (p3drc_cast_ray(scene, start_x, start_y, end_x, end_y) < 1) return true;
 * else return false;
 */
p3drc_Hit p3drc_cast_ray(const p3drc_Scene *scene, double pos_x, double pos_y, double dir_x, double dir_y);

/* Sorts sprites in place by distance. Call once per frame before rendering
 * sprites; p3drc_render does it for you.
 */
void p3drc_sort_sprites(const p3drc_Camera *camera, p3drc_Sprite *sprites, int sprite_count);

/* Renders one horizontal plane at the given world height (0 = floor,
 * 1 = ceiling). Called by p3drc_render_slice using scene floor_tex/ceiling_tex.
 * This function must run before render_walls.
 */
void p3drc_render_plane(const p3drc_Scene *scene, const p3drc_Camera *camera, p3drc_Target *target, int tex_num, double height);

/* Renders walls and fills target->z_buffer for every column in the slice,
 * including misses. Must run before p3drc_render_sprites.
 */
void p3drc_render_walls(const p3drc_Scene *scene, const p3drc_Camera *camera, p3drc_Target *target);

/* Renders sprites back to front against the z buffer. Sprites must already
 * be sorted.
 */
void p3drc_render_sprites(const p3drc_Scene *scene, const p3drc_Camera *camera, p3drc_Target *target, const p3drc_Sprite *sprites, int sprite_count);

/* Renders the target's [start, end) column slice: planes, walls, sprites.
 * Does not sort sprites, so slices can run on separate threads after a
 * single sort. sprites may be NULL with count 0.
 */
void p3drc_render_slice(const p3drc_Scene *scene, const p3drc_Camera *camera, p3drc_Target *target, const p3drc_Sprite *sprites, int sprite_count);

// Convenience: sorts sprites and renders the full target.
void p3drc_render(const p3drc_Scene *scene, const p3drc_Camera *camera, p3drc_Target *target, p3drc_Sprite *sprites, int sprite_count);

#endif // P3D_RAYCAST_H

#ifdef P3D_RAYCAST_IMPLEMENTATION
#include <math.h>

#ifndef P3DRC_QSORT
#include <stdlib.h>
#define P3DRC_QSORT qsort
#endif

static double p3drc__get_light(const p3drc_Scene *scene, double depth, int side) {
    double light = scene->light.falloff / depth;
    if (light < scene->light.ambient) light = scene->light.ambient;
    else if (light > scene->light.brightness) light = scene->light.brightness;
    return side == (int)scene->light.shade_face ? light * scene->light.shade_strength : light;
}

static double p3drc__get_fog(const p3drc_Scene *scene, double depth) {
    double fog = depth * scene->fog.density;
    return fog > 1.0 ? 1.0 : fog;
}

static int p3drc__sprite_compare(const void *a, const void *b) {
    const p3drc_Sprite *A = (const p3drc_Sprite *)a;
    const p3drc_Sprite *B = (const p3drc_Sprite *)b;
    if (A->_dist < B->_dist) return -1;
    else if (B->_dist < A->_dist) return 1;
    else return 0;
}

void p3drc_sort_sprites(const p3drc_Camera *camera, p3drc_Sprite *sprites, int sprite_count) {
    P3DRC_ASSERT(camera != NULL);
    P3DRC_ASSERT(sprites != NULL || sprite_count == 0);
    for (int i = 0; i < sprite_count; i++) {
        p3drc_Sprite *s = &sprites[i];
        double dx = s->pos_x - camera->pos_x;
        double dy = s->pos_y - camera->pos_y;
        s->_dist = dx * dx + dy * dy;
    }
    P3DRC_QSORT(sprites, sprite_count, sizeof(p3drc_Sprite), p3drc__sprite_compare);
}

p3drc_Hit p3drc_cast_ray(const p3drc_Scene *scene, double pos_x, double pos_y, double dir_x, double dir_y) {
    P3DRC_ASSERT(scene != NULL && scene->map.tiles != NULL);

    int map_x = floor(pos_x);
    int map_y = floor(pos_y);

    if (map_x >= scene->map.width || map_x < 0 || map_y >= scene->map.height || map_y < 0)
        return (p3drc_Hit){0};

    double dx = (dir_x == 0.0) ? 1e30 : fabs(1.0 / dir_x);
    double dy = (dir_y == 0.0) ? 1e30 : fabs(1.0 / dir_y);

    int step_x = copysign(1.0, dir_x);
    int step_y = copysign(1.0, dir_y);

    double sx = (-step_x * (pos_x - map_x) + (step_x > 0)) * dx;
    double sy = (-step_y * (pos_y - map_y) + (step_y > 0)) * dy;

    p3drc_Tile tile = {0};
    int type = P3DRC_TILE_EMPTY;
    enum p3drc_side side = sx < sy ? P3DRC_SIDE_V : P3DRC_SIDE_H;
    double entry = 0;

    p3drc_Tile start_tile = scene->map.tiles[map_x + map_y * scene->map.width];
    if (start_tile.type == P3DRC_TILE_WALL) {
        enum p3drc_face face;
        if (side == P3DRC_SIDE_V) face = step_x < 0 ? P3DRC_FACE_W : P3DRC_FACE_E;
        else face = step_y < 0 ? P3DRC_FACE_N : P3DRC_FACE_S;
        return (p3drc_Hit){
            .tile = start_tile,
            .depth = side == P3DRC_SIDE_V ? sx : sy,
            .side = side,
            .face = face
        };
    }

    while (1) {
        if (map_x >= scene->map.width || map_x < 0 || map_y >= scene->map.height || map_y < 0) {
            tile = (p3drc_Tile){0};
            break;
        }

        tile = scene->map.tiles[map_x + map_y * scene->map.width];
        type = tile.type;
        double open_amount = (double)tile.open / P3DRC_OPEN_MAX;

        if (type == P3DRC_TILE_DOOR_H) {
            double mid = sy - dy / 2;
            double wall_x = pos_x + mid * dir_x;
            wall_x -= floor(wall_x);
            if (entry < mid && mid < sx && open_amount < wall_x) {
                sy += dy / 2;
                side = P3DRC_SIDE_H;
            } else type = P3DRC_TILE_EMPTY;
        } else if (type == P3DRC_TILE_DOOR_V) {
            double mid = sx - dx / 2;
            double wall_x = pos_y + mid * dir_y;
            wall_x -= floor(wall_x);
            if (entry < mid && mid < sy && open_amount < 1 - wall_x) {
                sx += dx / 2;
                side = P3DRC_SIDE_V;
            } else type = P3DRC_TILE_EMPTY;
        }

        if (type != P3DRC_TILE_EMPTY) break;

        if (sx < sy) {
            entry = sx;
            sx += dx;
            map_x += step_x;
            side = P3DRC_SIDE_V;
        } else {
            entry = sy;
            sy += dy;
            map_y += step_y;
            side = P3DRC_SIDE_H;
        }
    }

    enum p3drc_face face;
    if (side == P3DRC_SIDE_V) face = step_x < 0 ? P3DRC_FACE_E : P3DRC_FACE_W;
    else face = step_y < 0 ? P3DRC_FACE_S : P3DRC_FACE_N;
    return (p3drc_Hit){
        .tile = tile,
        .depth = side == P3DRC_SIDE_V ? sx - dx : sy - dy,
        .side = side,
        .face = face
    };
}

void p3drc_render_plane(const p3drc_Scene *scene, const p3drc_Camera *camera, p3drc_Target *target, int tex_num, double height) {
    P3DRC_ASSERT(scene != NULL && camera != NULL && target != NULL);
    P3DRC_ASSERT(scene->atlas.pixels != NULL && target->pixels != NULL);
    int cols = scene->atlas.width / scene->atlas.subimage_size;
    int rows = scene->atlas.height / scene->atlas.subimage_size;
    // suppress unused variable warning if user disables asserts
    (void)rows;
    P3DRC_ASSERT(tex_num >= 0 && tex_num < rows * cols);
    P3DRC_ASSERT(target->start >= 0 && target->end <= target->width && target->start < target->end);

    double horizon = target->height / 2.0 + camera->pitch * target->height;
    double plane_x = -camera->dir_y * target->aspect_ratio / 2.0;
    double plane_y = camera->dir_x * target->aspect_ratio / 2.0;
    int base_tex_x = tex_num % cols * scene->atlas.subimage_size;
    int base_tex_y = tex_num / cols * scene->atlas.subimage_size;

    double ray_dir_xl = camera->dir_x * camera->FOV - plane_x;
    double ray_dir_yl = camera->dir_y * camera->FOV - plane_y;
    double ray_dir_xr = camera->dir_x * camera->FOV + plane_x;
    double ray_dir_yr = camera->dir_y * camera->FOV + plane_y;

    int is_below = height < camera->pos_z;
    int draw_start = is_below ? (int)ceil(horizon - 0.5) : 0;
    int draw_end = is_below ? target->height : (int)ceil(horizon - 0.5);

    if (draw_start < 0) draw_start = 0;
    if (draw_end > target->height) draw_end = target->height;

    double lr = scene->light.red, lg = scene->light.green, lb = scene->light.blue;
    double fr = scene->fog.red, fg = scene->fog.green, fb = scene->fog.blue;

    for (int y = draw_start; y < draw_end; y++) {
        double p = y + 0.5 - horizon;
        double v_dist = (height - camera->pos_z) * target->height;

        // cap dist at some arbitrary large distance to avoid inf
        double row_dist = fmin(fabs(v_dist / p), 16384.0);
        double step_x = (ray_dir_xr - ray_dir_xl) * row_dist / target->width;
        double step_y = (ray_dir_yr - ray_dir_yl) * row_dist / target->width;
        double world_x = camera->pos_x + ray_dir_xl * row_dist + step_x * target->start;
        double world_y = camera->pos_y + ray_dir_yl * row_dist + step_y * target->start;
        double light = p3drc__get_light(scene, row_dist * camera->FOV, -1);
        double fog = p3drc__get_fog(scene, row_dist * camera->FOV);

        // 32.32 fixed-point world coordinates
        int64_t wx = (int64_t)(world_x * 4294967296.0);
        int64_t wy = (int64_t)(world_y * 4294967296.0);
        int64_t sx = (int64_t)(step_x * 4294967296.0);
        int64_t sy = (int64_t)(step_y * 4294967296.0);

        // per-row integer shading: out = (texel * A + B) >> 16, A/B in 16.16
        int32_t ar = (int32_t)((1.0 - fog) * light * lr * 65536.0);
        int32_t ag = (int32_t)((1.0 - fog) * light * lg * 65536.0);
        int32_t ab = (int32_t)((1.0 - fog) * light * lb * 65536.0);
        int32_t br = (int32_t)(fog * fr * 255.0 * 65536.0);
        int32_t bg = (int32_t)(fog * fg * 255.0 * 65536.0);
        int32_t bb = (int32_t)(fog * fb * 255.0 * 65536.0);

        int sub = scene->atlas.subimage_size;
        uint8_t *out = target->pixels + target->start * 4 + y * target->pitch;
        for (int x = target->start; x < target->end; x++, wx += sx, wy += sy, out += 4) {
            int tex_x = base_tex_x + (int)(((wx & 0xFFFFFFFF) * sub) >> 32);
            int tex_y = base_tex_y + (int)(((wy & 0xFFFFFFFF) * sub) >> 32);
            uint8_t *c = scene->atlas.pixels + tex_x * 4 + tex_y * scene->atlas.pitch;
            int32_t r = (c[0] * ar + br) >> 16;
            int32_t g = (c[1] * ag + bg) >> 16;
            int32_t b = (c[2] * ab + bb) >> 16;
            out[0] = r > 255 ? 255 : r;
            out[1] = g > 255 ? 255 : g;
            out[2] = b > 255 ? 255 : b;
            out[3] = 0xFF;
        }
    }
}

void p3drc_render_walls(const p3drc_Scene *scene, const p3drc_Camera *camera, p3drc_Target *target) {
    P3DRC_ASSERT(scene != NULL && camera != NULL && target != NULL);
    P3DRC_ASSERT(scene->map.tiles != NULL && scene->atlas.pixels != NULL);
    P3DRC_ASSERT(target->pixels != NULL && target->z_buffer != NULL);
    P3DRC_ASSERT(target->start >= 0 && target->end <= target->width && target->start < target->end);

    int sub = scene->atlas.subimage_size;
    int cols = scene->atlas.width / sub;
    double plane_x = -camera->dir_y * target->aspect_ratio / 2.0;
    double plane_y = camera->dir_x * target->aspect_ratio / 2.0;
    double lr = scene->light.red, lg = scene->light.green, lb = scene->light.blue;
    double fr = scene->fog.red, fg = scene->fog.green, fb = scene->fog.blue;

    for (int x = target->start; x < target->end; x++) {
        double camera_x = 2.0 * (double)x / target->width - 1.0;
        double ray_dir_x = camera->dir_x * camera->FOV + plane_x * camera_x;
        double ray_dir_y = camera->dir_y * camera->FOV + plane_y * camera_x;

        p3drc_Hit hit = p3drc_cast_ray(scene, camera->pos_x, camera->pos_y, ray_dir_x, ray_dir_y);
        int type = hit.tile.type;
        if (type == P3DRC_TILE_EMPTY) {
            target->z_buffer[x] = 1e30;
            continue;
        }
        target->z_buffer[x] = hit.depth;

        double horizon = (0.5 + camera->pitch) * target->height;
        double inv_depth = 1.0 / hit.depth;

        double top = horizon + (camera->pos_z - 1.0) * inv_depth * target->height;
        double bottom = horizon + camera->pos_z * inv_depth * target->height;

        double wall_x;
        if (hit.side == P3DRC_SIDE_V) wall_x = camera->pos_y + hit.depth * ray_dir_y;
        else wall_x = camera->pos_x + hit.depth * ray_dir_x;
        wall_x -= floor(wall_x);

        double open_amount = (double)hit.tile.open / P3DRC_OPEN_MAX;
        if (type == P3DRC_TILE_DOOR_H) wall_x -= open_amount;
        else if (type == P3DRC_TILE_DOOR_V) wall_x += open_amount;

        enum p3drc_face face = hit.face;
        int tex_x = wall_x * sub;
        if (type == P3DRC_TILE_DOOR_H || type == P3DRC_TILE_DOOR_V) face = P3DRC_FACE_N;
        if (face == P3DRC_FACE_W || face == P3DRC_FACE_S) tex_x = sub - tex_x - 1;

        int tex_num = hit.tile.tex[face];
        tex_x += tex_num % cols * sub;

        int y_offset = tex_num / cols * sub;
        int draw_start = (int)ceil(top - 0.5);
        int draw_end = (int)ceil(bottom - 0.5);
        if (draw_start < 0) draw_start = 0;
        if (draw_end > target->height) draw_end = target->height;

        double step = sub / (bottom - top);
        // 16.16 fixed-point for texture sampling
        uint32_t step_fp = (uint32_t)(step * 65536.0);
        uint32_t tex_y_fp = (uint32_t)((draw_start + 0.5 - top) * step * 65536.0);

        double light = p3drc__get_light(scene, hit.depth * camera->FOV, hit.side);
        double fog = p3drc__get_fog(scene, hit.depth * camera->FOV);

        // per-column integer shading: out = (texel * A + B) >> 16, A/B in 16.16
        int32_t ar = (int32_t)((1.0 - fog) * light * lr * 65536.0);
        int32_t ag = (int32_t)((1.0 - fog) * light * lg * 65536.0);
        int32_t ab = (int32_t)((1.0 - fog) * light * lb * 65536.0);
        int32_t br = (int32_t)(fog * fr * 255.0 * 65536.0);
        int32_t bg = (int32_t)(fog * fg * 255.0 * 65536.0);
        int32_t bb = (int32_t)(fog * fb * 255.0 * 65536.0);

        uint8_t *out = target->pixels + x * 4 + draw_start * target->pitch;
        for (int y = draw_start; y < draw_end; y++, tex_y_fp += step_fp, out += target->pitch) {
            uint8_t *c = scene->atlas.pixels + tex_x * 4 + ((tex_y_fp >> 16) + y_offset) * scene->atlas.pitch;
            int32_t r = (c[0] * ar + br) >> 16;
            int32_t g = (c[1] * ag + bg) >> 16;
            int32_t b = (c[2] * ab + bb) >> 16;
            out[0] = r > 255 ? 255 : r;
            out[1] = g > 255 ? 255 : g;
            out[2] = b > 255 ? 255 : b;
            out[3] = 0xFF;
        }
    }
}

void p3drc_render_sprites(const p3drc_Scene *scene, const p3drc_Camera *camera, p3drc_Target *target, const p3drc_Sprite *sprites, int sprite_count) {
    P3DRC_ASSERT(scene != NULL && camera != NULL && target != NULL);
    P3DRC_ASSERT(sprites != NULL || sprite_count == 0);
    P3DRC_ASSERT(scene->atlas.pixels != NULL);
    P3DRC_ASSERT(target->pixels != NULL && target->z_buffer != NULL);
    P3DRC_ASSERT(target->start >= 0 && target->end <= target->width && target->start < target->end);

    int sub = scene->atlas.subimage_size;
    int cols = scene->atlas.width / sub;
    double sdir_x = camera->dir_x * camera->FOV;
    double sdir_y = camera->dir_y * camera->FOV;
    double plane_x = -camera->dir_y * target->aspect_ratio / 2.0;
    double plane_y = camera->dir_x * target->aspect_ratio / 2.0;
    double lr = scene->light.red, lg = scene->light.green, lb = scene->light.blue;
    double fr = scene->fog.red, fg = scene->fog.green, fb = scene->fog.blue;

    for (int s = sprite_count - 1; s >= 0; s--) {
        double rel_x = sprites[s].pos_x - camera->pos_x;
        double rel_y = sprites[s].pos_y - camera->pos_y;
        int tex_num = sprites[s].texture;
        uint8_t flags = sprites[s].flags;
        if (flags & P3DRC_SPRITE_INVISIBLE) continue;

        double inv_det = 1.0 / (plane_x * sdir_y - sdir_x * plane_y);
        double local_x = inv_det * (sdir_y * rel_x - sdir_x * rel_y);
        double local_y = inv_det * (-plane_y * rel_x + plane_x * rel_y);

        if (local_y <= 0) continue;

        double px = (local_x / local_y + 1.0) / 2.0 * target->width;
        double py = ((camera->pos_z - 0.5) / local_y + (camera->pitch + 0.5)) * target->height;
        double pw = 1.0 / local_y * target->width / target->aspect_ratio;
        double ph = 1.0 / local_y * target->height;

        double sprite_left = px - pw / 2.0;
        double sprite_top = py - ph / 2.0;

        int draw_start_x = (int)ceil(sprite_left - 0.5);
        int draw_end_x = (int)ceil(px + pw / 2.0 - 0.5);
        int draw_start_y = (int)ceil(sprite_top - 0.5);
        int draw_end_y = (int)ceil(py + ph / 2.0 - 0.5);

        if (draw_start_x < target->start) draw_start_x = target->start;
        if (draw_end_x > target->end) draw_end_x = target->end;
        if (draw_start_y < 0) draw_start_y = 0;
        if (draw_end_y > target->height) draw_end_y = target->height;

        double step_x = (double)sub / pw;
        double step_y = (double)sub / ph;
        int base_tex_x = tex_num % cols * sub;
        int base_tex_y = tex_num / cols * sub;

        double light = flags & P3DRC_SPRITE_NO_LIGHTING ? 1.0 : p3drc__get_light(scene, local_y * camera->FOV, -1);
        double fog = flags & P3DRC_SPRITE_NO_FOG ? 0.0 : p3drc__get_fog(scene, local_y * camera->FOV);

        // per-sprite integer shading: out = (texel * A + B) >> 16, A/B in 16.16
        int32_t ar = (int32_t)((1.0 - fog) * light * lr * 65536.0);
        int32_t ag = (int32_t)((1.0 - fog) * light * lg * 65536.0);
        int32_t ab = (int32_t)((1.0 - fog) * light * lb * 65536.0);
        int32_t br = (int32_t)(fog * fr * 255.0 * 65536.0);
        int32_t bg = (int32_t)(fog * fg * 255.0 * 65536.0);
        int32_t bb = (int32_t)(fog * fb * 255.0 * 65536.0);

        // 16.16 fixed-point for texture sampling
        int32_t step_x_fp = (int32_t)(step_x * 65536.0);
        int32_t step_y_fp = (int32_t)(step_y * 65536.0);
        int32_t tex_x_fp = (int32_t)((draw_start_x + 0.5 - sprite_left) * step_x * 65536.0);
        int32_t tex_y_fp0 = (int32_t)((draw_start_y + 0.5 - sprite_top) * step_y * 65536.0);

        for (int x = draw_start_x; x < draw_end_x; x++, tex_x_fp += step_x_fp) {
            if (local_y > target->z_buffer[x]) continue;
            int tex_x = base_tex_x + (tex_x_fp >> 16);

            uint32_t tex_y_fp = tex_y_fp0;
            uint8_t *out = target->pixels + x * 4 + draw_start_y * target->pitch;
            for (int y = draw_start_y; y < draw_end_y; y++, tex_y_fp += step_y_fp, out += target->pitch) {
                uint8_t *c = scene->atlas.pixels + tex_x * 4 + (base_tex_y + (tex_y_fp >> 16)) * scene->atlas.pitch;
                if (c[3] == 0) continue;
                // 16.16 fixed point alpha
                int32_t alpha = c[3] * 65536 / 255;
                uint8_t r_mix = out[0] + (((c[0] - out[0]) * alpha) >> 16);
                uint8_t g_mix = out[1] + (((c[1] - out[1]) * alpha) >> 16);
                uint8_t b_mix = out[2] + (((c[2] - out[2]) * alpha) >> 16);
                int32_t r = (r_mix * ar + br) >> 16;
                int32_t g = (g_mix * ag + bg) >> 16;
                int32_t b = (b_mix * ab + bb) >> 16;
                out[0] = r > 255 ? 255 : r;
                out[1] = g > 255 ? 255 : g;
                out[2] = b > 255 ? 255 : b;
                out[3] = 0xFF;
            }
        }
    }
}

void p3drc_render_slice(const p3drc_Scene *scene, const p3drc_Camera *camera, p3drc_Target *target, const p3drc_Sprite *sprites, int sprite_count) {
    P3DRC_ASSERT(scene != NULL && camera != NULL && target != NULL);

    if (scene->map.floor_tex >= 0)
        p3drc_render_plane(scene, camera, target, scene->map.floor_tex, 0);
    if (scene->map.ceiling_tex >= 0)
        p3drc_render_plane(scene, camera, target, scene->map.ceiling_tex, 1);

    if (scene->map.tiles != NULL)
        p3drc_render_walls(scene, camera, target);

    if (sprites != NULL && sprite_count > 0)
        p3drc_render_sprites(scene, camera, target, sprites, sprite_count);
}

void p3drc_render(const p3drc_Scene *scene, const p3drc_Camera *camera, p3drc_Target *target, p3drc_Sprite *sprites, int sprite_count) {
    P3DRC_ASSERT(target != NULL);

    if (sprites != NULL && sprite_count > 0)
        p3drc_sort_sprites(camera, sprites, sprite_count);

    int old_start = target->start;
    int old_end = target->end;
    target->start = 0;
    target->end = target->width;
    p3drc_render_slice(scene, camera, target, sprites, sprite_count);
    target->start = old_start;
    target->end = old_end;
}

#endif // P3D_RAYCAST_IMPLEMENTATION
/*
--------------------------------------------------------------------------------
LICENSE
--------------------------------------------------------------------------------
MIT License

Copyright (c) 2026 Noah Wagner

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

--------------------------------------------------------------------------------
THIRD PARTY NOTICES
--------------------------------------------------------------------------------
Portions of the raycaster are derived from Lode Vandevenne's raycasting
tutorial series (https://lodev.org/cgtutor/raycasting.html), licensed as
follows:

BSD 2-Clause License

Copyright (c) 2004-2021, Lode Vandevenne

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/
