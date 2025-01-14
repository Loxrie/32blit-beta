
#include <map>
#include <string>
#include <vector>
#include <cstdlib>

#include "platformer.hpp"

using namespace blit;
using std::floor;

Size screen_size(160, 120);

/* define storage for the framebuffer, spritesheet, and mask */
Pen    __ss[128 * 128];
uint8_t __m[320 * 240]; 

/* create surfaces */
Surface m((uint8_t *)__m, PixelFormat::M, screen_size);

const int max_light_radius = 60;
uint8_t __mshad[(max_light_radius * 2 + 1) * (max_light_radius * 2 + 1)] __SECTION__(".m");
Surface mshad((uint8_t *)__mshad, PixelFormat::M, Size(max_light_radius * 2 + 1, max_light_radius * 2 + 1));

Point world_to_screen(const Vec2 &p);
Point world_to_screen(const Point &p);
Point screen_to_world(const Point &p);
void highlight_tile(Point p, Pen c);
Point tile(const Point &p);
Point player_origin();
void draw_layer(MapLayer &layer);
void draw_flags();
void render_light(Point pt, float radius, bool shadows);
void blur(uint8_t passes);
void bloom(uint8_t passes);
void load_assets();


/* create map */
enum TileFlags { SOLID = 1, WATER = 2, LADDER = 4 };
Map map(Rect(0, 0, 48, 24));

uint8_t player_animation[5] = { 208, 209, 210, 211, 212 };
float player_animation_frame = 0;

float clamp(float v, float min, float max) {
  return v > min ? (v < max ? v : max) : min;
}

struct Player {
  Vec2 vel;
  Vec2 pos;
  Size size;
  bool flip = false;

  enum State {
    STILL,
    WALKING,
    JUMPING,
    SWIMMING,
    CLIMBING
  };

  State state = STILL;

  std::map<uint8_t, std::vector<uint8_t>> animations;
  float animation_frame = 0.0f;

  Player() {
    animations[STILL] = { 208, 208, 208, 208, 208, 208, 209, 208, 208, 208, 208, 208, 208, 208 };
    animations[WALKING] = { 208, 209, 210, 211, 212 };
    animations[JUMPING] = { 217 };
    animations[SWIMMING] = { 217 };
    animations[CLIMBING] = { 217 };

    vel = Vec2(0, 0);
    pos = Vec2(100, 32);
    size = Size(6, 14);
  }

  Rect aabb() {
    return Rect(pos.x, pos.y - size.h, size.w, size.h);
  }

  Rect feet() {
    return Rect(pos.x, pos.y - size.h / 2, size.w, size.h / 2);
  }

  uint8_t animation_sprite_index(uint8_t animation) {
    auto animation_length = animations[animation].size();
    return animations[animation][uint32_t(animation_frame) % animation_length];
  }

  Point current_tile() {
    return Point((pos.x + (size.w / 2)) / 8, (pos.y - 8) / 8);
  }

  bool tile_under_solid() {
    Point p = current_tile();
    return map.has_flag(p, TileFlags::SOLID);
  }

  bool tile_under_ladder() {
    Point p = current_tile();
    return map.has_flag(p, TileFlags::LADDER);
  }

  bool tile_under_water() {
    Point p = current_tile();
    return map.has_flag(p, TileFlags::WATER);
  }

  bool on_ground() {
    return state == STILL || state == WALKING || state == SWIMMING || state == CLIMBING;
    /*if (vel.y < 0) return false;

    Point p = current_tile();
    return map.has_flag(Point(p.x, p.y + 1), TileFlags::SOLID) && ((int32_t(pos.y) % 8) == 0);*/
  }

  bool in_water() {
    return false;
    Point p = current_tile();
    return map.has_flag(Point(p.x, p.y + 1), TileFlags::WATER);
  }

  /*
    return a clipped camera point that doesn't allow the viewport to
    leave the world bounds
  */
  Point camera() {      
    static Rect b(screen_size.w / 2, screen_size.h / 2, map.bounds.w * 8 - screen.bounds.w, map.bounds.h * 8 - screen.bounds.h);
    return b.clamp(Point(floor(pos.x), floor(pos.y)));
  }

  Rect viewport() {
    Point c = camera();
    return Rect(
      c.x - screen.bounds.w / 2,
      c.y - screen.bounds.h / 2,
      screen.bounds.w,
      screen.bounds.h
    );
  }

  void update() {
    static float ground_acceleration_x = 0.5f;
    static float air_acceleration_x = 0.2f;
    static float ground_drag_x = 0.70f;
    static float air_drag_x = 0.8f;
    static float duration = 0.01f;
    static float jump_velocity = 4.0f;
    static Vec2 gravity(0, 0.98f / 10.0f);

    bool is_on_ground = on_ground();

    vel.x *= is_on_ground ? ground_drag_x : air_drag_x;
    if(!tile_under_ladder()) vel += gravity;
    if(tile_under_water()) {
      vel.y *= 0.80f;
    }
    else {
      vel.y *= tile_under_ladder() ? 0.80f : 0.95f;
    }
  

    // Handle Left/Right collission
    pos.x += vel.x;
    Rect bounds_lr = feet();

    map.tiles_in_rect(bounds_lr, [this, bounds_lr](Point tile_pt) -> void {
      if (map.has_flag(tile_pt, TileFlags::SOLID)) {
        Rect rb(tile_pt.x * 8, tile_pt.y * 8, 8, 8);
        uint32_t player_bottom = bounds_lr.y + bounds_lr.h;
        uint32_t player_top = bounds_lr.y;
        uint32_t player_left = bounds_lr.x;
        uint32_t player_right = bounds_lr.x + bounds_lr.w;
        uint32_t tile_bottom = rb.y + rb.h;
        uint32_t tile_top = rb.y;
        uint32_t tile_left = rb.x;
        uint32_t tile_right = rb.x + rb.w;
        if(((player_bottom > tile_top) && (player_bottom < tile_bottom))
        || ((player_top > tile_top) && player_top < tile_bottom)){
            // Collide the left-hand side of the tile right of player
            if(player_right > tile_left && (player_left < tile_left)){
                pos.x = float(tile_left - bounds_lr.w);
                vel.x = 0.0f;
            }
            // Collide the right-hand side of the tile left of player
            if((player_left < tile_right) && (player_right > tile_right)) {
                pos.x = float(tile_right);
                vel.x = 0.0f;
            }
        }
      }
    });

    // Handle Up/Down collission
    pos.y += vel.y;
    Rect bounds_ud = feet();

    map.tiles_in_rect(bounds_ud, [this, bounds_ud](Point tile_pt) -> void {
      if (map.has_flag(tile_pt, TileFlags::SOLID)) {
        Rect rb(tile_pt.x * 8, tile_pt.y * 8, 8, 8);
        uint32_t player_bottom = bounds_ud.y + bounds_ud.h;
        uint32_t player_top = bounds_ud.y;
        uint32_t player_left = bounds_ud.x;
        uint32_t player_right = bounds_ud.x + bounds_ud.w;
        uint32_t tile_bottom = rb.y + rb.h;
        uint32_t tile_top = rb.y;
        uint32_t tile_left = rb.x;
        uint32_t tile_right = rb.x + rb.w;
        if((player_right > tile_left) && (player_left < tile_right)){
          // Collide bottom side of tile above player
          if(player_top < tile_bottom && player_bottom > tile_bottom){
              pos.y = float(tile_bottom + bounds_ud.h);
              vel.y = 0;
          }
          // Collide the top side of the tile below player
          if((player_bottom > tile_top) && (player_top < tile_top)){
              pos.y = tile_top;
              vel.y = 0;
              state = STILL;
          }
        }
      }
    });

    if (buttons.state & Button::DPAD_LEFT) {
      vel.x -= is_on_ground ? ground_acceleration_x : air_acceleration_x;
      flip = true;
      if(is_on_ground) {
        state = WALKING;
      }
    }

    if (buttons.state & Button::DPAD_RIGHT) {
      vel.x += is_on_ground ? ground_acceleration_x : air_acceleration_x;
      flip = false;
      if(is_on_ground) {
        state = WALKING;
      }
    }

    if (is_on_ground && buttons.pressed & Button::A) {
      vel.y += -jump_velocity;
      state = JUMPING;
    }

    if(tile_under_ladder() || tile_under_water()) {
      if (buttons.state & Button::DPAD_UP) {
        vel.y -= 0.2f;
      }
      if (buttons.state & Button::DPAD_DOWN) {
        vel.y += 0.2f;
      }
      state = tile_under_water() ? SWIMMING : CLIMBING;
    }


  }

  void render() {
    uint8_t animation = Player::STILL;

    if (std::abs(vel.x) > 1) {
      animation = Player::WALKING;
    }
    if (!on_ground()) {
      animation = Player::JUMPING;
    }

    uint8_t si = animation_sprite_index(animation);

    Point sp = world_to_screen(Point(pos.x, pos.y - 8));
    screen.sprite(si, sp, flip);
    sp.y -= 8;
    screen.sprite(si - 16, sp, flip);


    /*
    Rect bb = aabb();
    screen.pen = Pen(0, 255, 0);
    screen.line(world_to_screen(bb.tl()), world_to_screen(bb.tr()));
    screen.line(world_to_screen(bb.bl()), world_to_screen(bb.br()));

    // Collission Debug
    map.tiles_in_rect(bb, [&bb](Point tile_pt) -> void {
      Point sp = world_to_screen(tile_pt * 8);
      Rect rb(sp.x, sp.y, 8, 8);

      screen.pen = Pen(0, 255, 0, 150);
      if (map.has_flag(tile_pt, TileFlags::SOLID)) {
        screen.pen = Pen(255, 0, 0, 150);
      }

      screen.rectangle(rb);
    });
    */

    //map.tiles_in_rect(bb)
  }
} player;


struct bat {
  Vec2 pos;
  Vec2 vel = Vec2(-2, 0);
  uint8_t current_frame = 0;
  std::array<uint8_t, 6> frames = {{ 96, 97, 98, 99, 98, 97 }};

  void update() {
    current_frame++;
    current_frame %= frames.size();

    Vec2 test_pos = pos + (Vec2::normalize(vel) * 8.0f);

    if (map.has_flag(tile(Point(test_pos)), TileFlags::SOLID)) {
      vel.x *= -1;
    }

    pos += vel;
  }
};

bat bat1;

struct slime {
  Vec2 pos;
  Vec2 vel = Vec2(1, 0);
  uint8_t current_frame = 0;
  std::array<uint8_t, 4> frames = {{ 112, 113, 114, 113 }};

  void update() {
    current_frame++;
    current_frame %= frames.size();

    Vec2 test_pos = pos + (Vec2::normalize(vel) * 8.0f);

    if (map.has_flag(tile(Point(test_pos)), TileFlags::SOLID)) {
      vel.x *= -1;
    }

    pos += vel;
  }
};

slime slime1;

void animation_timer_callback(Timer &timer) {
  bat1.update();
  slime1.update();
}

Timer t;






/* setup */
void init() {
  load_assets();

  bat1.pos = Vec2(200, 22);
  slime1.pos = Vec2(50, 112);

  t.init(animation_timer_callback, 50, -1);
  t.start();

  screen_size.w = 160;
  screen_size.h = 120;
  //engine::set_screen_mode(screen_mode::hires);
}


void render(uint32_t time) {
  static int32_t x = 0; x++;
      
  uint32_t ms_start = now();

  screen.mask = nullptr;
  screen.alpha = 255;
  screen.pen = Pen(0, 0, 0);
  screen.clear();

  // mask out for lighting    
  mshad.alpha = 255;
  mshad.pen = Pen(0);
  mshad.clear();

  m.alpha = 255;
  m.pen = Pen(64);
  m.clear();

  // render lights
  
  for (uint8_t y = 0; y < 24; y++) {
    for (uint8_t x = 0; x < 48; x++) {
      uint32_t ti = map.layers["effects"].tile_at(Point(x, y));
      Point lp = Point(x * 8 + 4, y * 8 + 3);
      if (ti == 37 || ti == 38) {
        render_light(lp, 15.0f, false);
      }
    }
  }

  render_light(Point(player.pos.x, player.pos.y - 7), 60.0f, true);

  // light up the "outside" this should be done with map flags
  Rect r; m.pen = Pen(255);
  r = Rect(world_to_screen(Point(0, 0)), world_to_screen(Point(112, 40))); m.rectangle(r);
  r = Rect(world_to_screen(Point(0, 40)), world_to_screen(Point(80, 48))); m.rectangle(r);
  r = Rect(world_to_screen(Point(0, 48)), world_to_screen(Point(72, 56))); m.rectangle(r);
  r = Rect(world_to_screen(Point(0, 56)), world_to_screen(Point(48, 64))); m.rectangle(r);
  r = Rect(world_to_screen(Point(0, 64)), world_to_screen(Point(32, 72))); m.rectangle(r);

  bloom(3);
  blur(1);


  screen.alpha = 255;
  screen.pen = Pen(39, 39, 54);
  screen.clear();



  // draw world
  // layers: background, environment, effects, characters, objects
  draw_layer(map.layers["background"]);
  draw_layer(map.layers["environment"]);
  draw_layer(map.layers["effects"]);
  draw_layer(map.layers["objects"]);


  // draw player
  player.render();


  // bat
  Point sp = world_to_screen(Point(bat1.pos.x - 4, bat1.pos.y));
  screen.sprite(bat1.frames[bat1.current_frame], sp, bat1.vel.x < 0 ? false : true);

  // slime
  sp = world_to_screen(Point(slime1.pos.x - 4, slime1.pos.y));
  screen.sprite(slime1.frames[slime1.current_frame], sp, slime1.vel.x < 0 ? false : true);


  // overlay water
  screen.pen = Pen(56, 136, 205, 125);
  for (uint8_t y = 0; y < 24; y++) {
    for (uint8_t x = 0; x < 48; x++) {
      Point pt = world_to_screen(Point(x *   8, y * 8));
      uint32_t ti = map.tile_index(Point(x, y));

      if (map.has_flag(Point(x, y), TileFlags::WATER)) {
        screen.rectangle(Rect(pt.x, pt.y, 8, 8));
      }
    }
  }

  // invert the lighting mask
  m.custom_modify(m.clip, [](uint8_t *p, int16_t c) -> void {
    while (c--) {
      *p = 255 - *p;
      p++;
    }
  });
  
  // blend over lighting
  screen.mask = &m;
  screen.pen = Pen(39 / 2, 39 / 2, 54 / 2);
  screen.clear();

  static int tick = 0;
  tick++;
  screen.mask = nullptr;
  screen.alpha = 255;
  screen.sprite(139, Point(2, 2));
  screen.sprite(139, Point(12, 2));
  screen.sprite(139, Point(22, 2));

  
  // draw FPS meter
  uint32_t ms_end = now();
  screen.mask = nullptr;
  screen.pen = Pen(255, 0, 0);
  for (uint32_t i = 0; i < (ms_end - ms_start); i++) {
    screen.pen = Pen(i * 5, 255 - (i * 5), 0);
    screen.rectangle(Rect(i * 3 + 1, 117, 2, 2));
  }
  

  // highlight current player tile
  // point pt2 = player.current_tile();
  // highlight_tile(pt2, rgba(0, 255, 0, 100));

  // draw map flags
  // draw_flags();
}


/*
  update() is called every 10ms, all effects should be
  scaled to that duration

  player velocity is in tiles per second, so if the players
  'x' velocity is 1 then they move sideways by one tile per
  second

  one tile is considered to be 1 metre
*/
void update(uint32_t time) {
  player.update();

  /*
  if (tick_seed % 3 == 0) {
    for (uint8_t y = 0; y < 16; y++) {
      for (uint8_t x = 0; x < 32; x++) {
        uint8_t ti = tile_index(point(x, y));
        if (ti >= 5 && ti <= 7) {
          set_tile_index(point(x, y), 5 + rand() % 3);
        }
      }
    }
  }
  */

}




float orient2d(Vec2 p1, Vec2 p2, Vec2 p3) {
  return (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
}

std::vector<std::pair<Vec2, Vec2>> get_occluders(Point pt, float radius) {
  std::vector<std::pair<Vec2, Vec2>> occluders;

  Rect light_bounds(pt, pt);
  light_bounds.inflate(max_light_radius);

  map.tiles_in_rect(light_bounds, [&occluders, &pt](Point tile_pt) -> void {
    if (map.has_flag(tile_pt, TileFlags::SOLID)) {
      Rect rb(tile_pt.x * 8, tile_pt.y * 8, 8, 8);
      rb.x -= pt.x;
      rb.y -= pt.y;

      Vec2 o(0, 0);
      Vec2 fpt(pt.x, pt.y);
      /*vec2 tl(rb.x - 0.5f, rb.y + 0.5f);// = rb.tl();
      vec2 tr(rb.x + rb.w - 0.5f, rb.y + 0.5f);// = rb.tr();
      vec2 bl(rb.x - 0.5f, rb.y + rb.h + 0.5f);// = rb.bl();
      vec2 br(rb.x + rb.w - 0.5f, rb.y + rb.h + 0.5f);// = rb.br();
      */
      Vec2 tl = Vec2(rb.tl().x - 1, rb.tl().y - 1);
      Vec2 tr = Vec2(rb.tr().x - 1, rb.tr().y - 1);
      Vec2 bl = Vec2(rb.bl().x - 1, rb.bl().y - 1);
      Vec2 br = Vec2(rb.br().x - 1, rb.br().y - 1);

      if (!map.has_flag(Point(tile_pt.x, tile_pt.y + 1), TileFlags::SOLID) && orient2d(bl, br, o) > 0) {
        occluders.push_back(std::make_pair(bl, br));
      }

      if (!map.has_flag(Point(tile_pt.x - 1, tile_pt.y), TileFlags::SOLID) && orient2d(tl, bl, o) > 0) {
        occluders.push_back(std::make_pair(tl, bl));
      }

      if (!map.has_flag(Point(tile_pt.x, tile_pt.y - 1), TileFlags::SOLID) && orient2d(tr, tl, o) > 0) {
        occluders.push_back(std::make_pair(tr, tl));
      }

      if (!map.has_flag(Point(tile_pt.x + 1, tile_pt.y), TileFlags::SOLID) && orient2d(br, tr, o) > 0) {
        occluders.push_back(std::make_pair(br, tr));
      }
    }
  });

  return occluders;
}

void render_light(Point pt, float radius, bool shadows = false) {
  Point lpt(max_light_radius, max_light_radius);

  mshad.alpha = 255;
  mshad.pen = Pen(0);
  mshad.clear();

  // draw the light aura
  mshad.alpha = (rand() % 10) + 40;
  int steps = 20;
  for (int j = steps; j > 0; j--) {
    mshad.pen = Pen(255);
    mshad.circle(lpt, (j * radius / steps));
  }

  if (shadows)
  {
    // cut out the shadows
    mshad.alpha = 255;
    mshad.pen = Pen(0);

    float rs = radius * radius;
    std::vector<std::pair<Vec2, Vec2>> occluders = get_occluders(pt, radius);
    for (auto occluder : occluders) {
      Vec2 p1 = occluder.first;
      Vec2 p2 = occluder.second;
      Vec2 fpt(pt.x, pt.y);

      Vec2 rv1 = p1;
      Vec2 rv2 = p2;

      if ((std::abs(rv1.x) * std::abs(rv1.y)) < rs && (std::abs(rv2.x) * std::abs(rv2.y)) < rs) {
        // (max_light_radius * 2) = cludge to ensure shadows are projected far enough
        // actually we should project shadows to the bounds of the light bounding box
        // there is no need to "guess" but that requires working out the intersection
        // with the edge of the bounding box and optionally inserting points at the corners
        // if required. a task for another day....
        float c1 = (max_light_radius * 2) / float(std::max(std::abs(rv1.x), std::abs(rv1.y)));
        float c2 = (max_light_radius * 2) / float(std::max(std::abs(rv2.x), std::abs(rv2.y)));

        Vec2 p3 = rv1 * c1;
        Vec2 p4 = rv2 * c2;

        Point wp1 = p1 + lpt;
        Point wp2 = p2 + lpt;
        Point wp3 = p3 + lpt;
        Point wp4 = p4 + lpt;

        std::vector<Point> poly = {
          wp1, wp3, wp4, wp2
        };

        mshad.triangle(wp1, wp2, wp3);
        mshad.triangle(wp2, wp4, wp3);
        //mshad.polygon(poly);
      }
    }
  }

  Point light_corner = world_to_screen(pt - Point(max_light_radius, max_light_radius));
  m.custom_blend(&mshad, mshad.clip, light_corner, [](uint8_t *psrc, uint8_t *pdest, int16_t c) -> void {
    while (c--) {
      *pdest = *pdest ^ ((*pdest ^ *psrc) & -(*pdest < *psrc)); // integer `max` without branching

      pdest++;
      psrc++;
    }
  });
}

void blur(uint8_t passes) {
  uint8_t last;

  for (uint8_t pass = 0; pass < passes; pass++) {
    uint8_t *p = (uint8_t *)m.data;
    for (uint16_t y = 0; y < m.bounds.h; y++) {
      last = *p;
      p++;

      for (uint16_t x = 1; x < m.bounds.w - 1; x++) {
        *p = (*(p + 1) + last + *p + *p) >> 2;
        last = *p;
        p++;
      }

      p++;
    }
  }

  // vertical      
  for (uint8_t pass = 0; pass < passes; pass++) {
    for (uint16_t x = 0; x < m.bounds.w; x++) {
      uint8_t *p = (uint8_t *)m.data + x;

      last = *p;
      p += m.bounds.w;

      for (uint16_t y = 1; y < m.bounds.h - 1; y++) {
        *p = (*(p + m.bounds.w) + last + *p + *p) >> 2;
        last = *p;
        p += m.bounds.w;
      }
    }
  }
}


  void bloom(uint8_t passes) {
  for (uint8_t pass = 0; pass < passes; pass++) {
    uint8_t *p = (uint8_t *)m.data + m.bounds.w;
    for (uint16_t y = 1; y < m.bounds.h - 1; y++) {
      p++;

      for (uint16_t x = 1; x < m.bounds.w - 1; x++) {
        uint8_t v1 = *p;
        uint8_t v2 = *(p + 1);
        uint8_t v3 = *(p + m.bounds.w);
        *p++ = v1 > v2 ? (v1 > v3 ? v1 : v3) : (v2 > v3 ? v2 : v3);
      }

      p++;
    }
    
    p = (uint8_t *)m.data + (m.bounds.h * m.bounds.w) - 1 - m.bounds.w;
    for (uint16_t y = 1; y < m.bounds.h - 1; y++) {
      p--;

      for (uint16_t x = 1; x < m.bounds.w - 1; x++) {
        uint8_t v1 = *p;
        uint8_t v2 = *(p - 1);
        uint8_t v3 = *(p - m.bounds.w);
        *p-- = v1 > v2 ? (v1 > v3 ? v1 : v3) : (v2 > v3 ? v2 : v3);
      }

      p--;
    }
  }
}

void load_assets() {
  std::vector<uint8_t> layer_background = { 17,17,17,17,17,17,17,17,17,17,17,17,17,17,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,47,17,17,17,17,17,17,17,17,17,17,17,17,17,17,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,47,17,17,17,17,17,17,17,17,17,17,17,17,17,17,0,0,0,0,0,0,0,0,0,0,0,0,0,41,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,41,0,0,0,47,1,2,3,4,1,2,3,1,2,3,4,5,2,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,47,0,0,0,0,0,0,0,51,0,0,0,13,14,0,41,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,41,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,31,68,47,0,0,0,30,0,0,0,0,15,0,0,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,0,31,84,47,0,0,0,0,0,0,0,15,0,0,0,0,0,0,0,0,0,0,0,0,31,67,47,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,22,0,0,0,0,0,0,0,0,0,23,0,0,0,0,0,0,0,0,0,0,0,0,0,0,31,83,47,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,223,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,41,0,0,0,0,0,0,41,0,0,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,13,0,0,0,0,0,0,0,0,15,0,78,0,0,0,0,0,0,0,0,15,0,0,78,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,41,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,15,15,15,15,15,41,15,15,15,41,15,41,15,15,15,41,0,0,0,15,15,15,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,41,15,41,15,15,15,15,41,15,15,15,15,15,60,15,15,0,0,0,15,41,41,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,15,60,15,15,15,15,15,41,15,15,41,15,15,15,41,15,41,41,15,15,15,15,41,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,41,15,15,15,41,15,13,15,15,41,15,15,41,15,41,15,15,15,15,41,15,15,15,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,15,41,15,15,41,15,15,15,41,41,15,15,15,15,15,30,15,15,15,41,15,60,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,15,15,15,15,15,41,15,15,15,60,15,15,41,15,15,15,15,41,15,41,15,15,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };;
  map.add_layer("background", layer_background);

  std::vector<uint8_t> layer_environment = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,72,0,74,28,29,60,0,0,0,0,0,0,60,28,29,0,0,15,0,0,0,0,0,0,0,0,0,0,28,29,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,72,0,74,44,45,0,0,0,0,0,0,0,0,44,45,0,0,0,0,15,0,0,0,0,15,0,0,0,44,45,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,72,0,74,0,0,0,0,0,0,0,0,0,0,60,89,89,89,89,71,0,0,0,0,0,0,0,0,0,74,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,88,89,90,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,60,28,29,0,0,0,0,0,15,0,74,0,0,0,0,0,0,0,0,0,0,0,0,0,0,190,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,44,45,0,0,15,0,0,0,0,74,0,0,0,0,0,0,0,0,0,0,0,0,0,0,121,28,29,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,60,71,0,0,0,0,0,0,74,0,0,0,0,0,0,0,0,0,0,0,0,0,0,60,44,45,58,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,88,89,89,89,89,89,71,74,0,0,0,0,0,0,0,0,0,0,0,60,57,57,87,0,0,74,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,60,0,0,0,0,0,0,0,0,60,74,0,0,0,56,57,0,0,0,0,56,57,87,0,0,0,0,0,86,57,57,57,57,28,29,57,60,57,57,57,50,55,55,55,55,48,57,57,57,60,0,0,0,0,0,0,0,28,29,55,55,55,64,0,58,7,57,60,72,0,0,0,0,75,94,94,94,94,94,94,76,44,45,0,0,0,0,0,66,16,16,48,49,127,0,0,28,29,60,0,0,0,0,0,0,44,45,16,16,16,64,0,74,7,0,0,72,0,0,0,0,79,0,0,0,0,0,0,77,0,75,94,94,94,94,76,126,49,49,127,0,0,0,0,44,45,66,55,55,55,55,55,55,60,16,16,16,16,64,0,74,7,0,0,93,94,94,94,94,95,0,0,0,0,0,0,93,94,95,0,0,0,0,47,0,0,0,0,0,0,0,0,0,0,66,16,16,16,16,16,16,16,16,16,16,16,64,0,74,7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,47,0,0,0,0,0,0,0,0,0,0,126,49,49,49,50,16,16,16,16,16,16,48,127,0,31,7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,47,0,0,0,0,0,0,0,0,0,0,0,0,0,0,66,16,16,16,16,16,48,127,0,0,31,7,0,0,0,0,0,0,0,0,40,61,62,62,63,0,0,0,0,0,0,0,0,47,0,0,0,0,0,0,0,0,0,0,0,0,0,0,126,49,49,49,49,49,127,0,0,0,91,62,62,62,62,62,62,62,62,62,62,92,0,0,91,62,62,63,0,61,62,62,62,92,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,79,0,77,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,79,59,77,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
  map.add_layer("environment", layer_environment);
  map.layers["environment"].add_flags({ 8, 59, 31, 47, 28, 29, 44, 45, 60, 48, 49, 50, 64, 66, 80, 81, 82, 56, 57, 58, 72, 74, 88, 89, 90, 61, 62, 63, 77, 79, 93, 94, 95 }, TileFlags::SOLID);
  map.layers["environment"].add_flags(7, TileFlags::LADDER);
  map.layers["environment"].add_flags({ 16, 55, 223 }, TileFlags::WATER);

  std::vector<uint8_t> layer_effects = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,32,33,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,32,33,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,53,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,37,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,37,0,0,37,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,52,52,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
  map.add_layer("effects", layer_effects);

  std::vector<uint8_t> layer_characters = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,96,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,192,0,0,0,107,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,208,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,96,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,144,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,166,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,122,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,182,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,112,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
  map.add_layer("characters", layer_characters);

  std::vector<uint8_t> layer_objects = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,51,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,68,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,11,0,0,0,0,0,0,0,0,0,0,0,0,0,0,84,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,25,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,12,0,39,0,0,0,0,0,0,0,0,68,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,39,0,0,0,0,0,0,0,0,0,0,85,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,84,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
  map.add_layer("objects", layer_objects);

  screen.sprites = SpriteSheet::load(packed_data);
}


Point world_to_screen(const Vec2 &p) {
  return Point(
    p.x - player.camera().x + m.bounds.w / 2,
    p.y - player.camera().y + m.bounds.h / 2
  );
}

Point world_to_screen(const Point &p) {
  return Point(
    p.x - player.camera().x + m.bounds.w / 2,
    p.y - player.camera().y + m.bounds.h / 2
  );
}

Point screen_to_world(const Point &p) {
  return Point(
    p.x + player.camera().x - m.bounds.w / 2,
    p.y + player.camera().y - m.bounds.h / 2
  );
}


void highlight_tile(Point p, Pen c) {
  screen.pen = c;
  p.x *= 8;
  p.y *= 8;
  p = world_to_screen(p);
  screen.rectangle(Rect(p.x, p.y, 8, 8));
}

Point player_origin() {
  return Point(player.pos.x, player.pos.y);
}

Point tile(const Point &p) {
  return Point(p.x / 8, p.y / 8);
}


void draw_layer(MapLayer &layer) {
  Point tl = screen_to_world(Point(0, 0));
  Point br = screen_to_world(Point(screen.bounds.w, screen.bounds.h));

  Point tlt = tile(tl);
  Point brt = tile(br);

  for (uint8_t y = tlt.y; y <= brt.y; y++) {
    for (uint8_t x = tlt.x; x <= brt.x; x++) {
      Point pt = world_to_screen(Point(x * 8, y * 8));
      int32_t ti = layer.map->tile_index(Point(x, y));
      if (ti != -1) {
        uint8_t si = layer.tiles[ti];
        if (si != 0) {
          screen.sprite(si, pt);
        }
      }
    }
  }
}

Pen flag_colours[] = {
  Pen(255, 0, 0, 100),
  Pen(0, 255, 0, 100),
  Pen(0, 0, 255, 100)
};

void draw_flags() {
  for (uint8_t y = 0; y < 24; y++) {
    for (uint8_t x = 0; x < 48; x++) {
      Point pt = world_to_screen(Point(x * 8, y * 8));
      uint32_t ti = map.tile_index(Point(x, y));
      uint8_t f = map.get_flags(Point(x, y));

      for (uint8_t i = 0; i < 3; i++) {
        if (f & (1 << i)) {
          screen.pen = flag_colours[i];
          screen.rectangle(Rect(pt.x, pt.y, 8, 8));
        }
      }
    }
  }
}
