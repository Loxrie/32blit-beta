/*! \file surface.cpp
*/
#include <algorithm>
#include <cstring>
#include <string>

#include "font.hpp"
#include "sprite.hpp"
#include "surface.hpp"

#include "../engine/file.hpp"

using namespace blit;

#ifdef _MSC_VER
#include <intrin.h>
static int log2i(unsigned int x) {
    unsigned long idx = 0;
    _BitScanReverse(&idx, x);
    return idx;
}
#else
static int log2i(unsigned int x) {
    return 8 * sizeof(unsigned int) - __builtin_clz(x) - 1;
}
#endif

namespace blit {


  Surface::Surface(uint8_t *data, const PixelFormat &format, const Size &bounds) : data(data), bounds(bounds), format(format) {
    init();
  }

  Surface::Surface(uint8_t *data, const PixelFormat &format, const packed_image *image) : data(data), format(format) {
    File f_image;
    f_image.open((const uint8_t *)image, image->byte_count);
    load_from_packed(f_image);
    init();
  }

  Surface::Surface(uint8_t *data, const PixelFormat &format, File &image) : data(data), format(format) {
    load_from_packed(image);
    init();
  }

  /**
   * Loads a packed or raw image asset into a `Surface`
   * 
   * \param image
   * 
   * \return `Surface` containing loaded data or `nullptr` if the image was invalid
   */
  Surface *Surface::load(const packed_image *image) {
    if(memcmp(image->type, "SPRITEPK", 8) != 0 && memcmp(image->type, "SPRITERW", 8) != 0 && memcmp(image->type, "SPRITERL", 8) != 0)
      return nullptr;

    if(image->format > (uint8_t)PixelFormat::M)
      return nullptr;

    uint8_t *buffer = new uint8_t[pixel_format_stride[image->format] * image->width * image->height];
    return new Surface(buffer, (PixelFormat)image->format, image);
  }

  /**
   * \overload
   * 
   * \param data pointer to an image asset
   */
  Surface *Surface::load(const uint8_t *data) {
    return load((const packed_image *)data);
  }

  /**
   * \overload
   * 
   * \param filename string filename
   */
  Surface *Surface::load(std::string &filename) {
    File file;

    if(!file.open(filename, OpenMode::read))
      return nullptr;

    packed_image image;
    file.read(0, sizeof(packed_image), (char *)&image);

    uint8_t *buffer = new uint8_t[pixel_format_stride[image.format] * image.width * image.height];
    return new Surface(buffer, (PixelFormat)image.format, file);
  }

  /**
   * Similar to @ref load, but the resulting `Surface` points directly at the image data instead of copying it.
   * `data` should not be modified after loading, so no drawing can be done to this surface. If the image is paletted, the palette can still be modified.
   * 
   * Only works for non-packed images.
   *
   * \param image
   * 
   * \return `Surface` containing loaded data or `nullptr` if the image was invalid
   */
  Surface *Surface::load_read_only(const packed_image *image) {
    if(memcmp(image->type, "SPRITERW", 8) != 0)
      return nullptr;

    if(image->format > (uint8_t)PixelFormat::M)
      return nullptr;

    return new Surface(nullptr, (PixelFormat)image->format, image);
  }

  /**
   * \overload
   * 
   * \param data pointer to an image asset
   */
  Surface *Surface::load_read_only(const uint8_t *data) {
    return load_read_only((const packed_image *)data);
  }

  bool Surface::save(const std::string &filename) {
    File file;

    if(!file.open(filename, OpenMode::write))
      return false;

    unsigned int data_size = row_stride * bounds.h;
    unsigned int palette_size = format == PixelFormat::P ? 256 : 0;

#pragma pack(push, 2)
    struct BMPHeader {
      char header[2]{'B', 'M'};
      uint32_t file_size;
      uint16_t reserved[2]{};
      uint32_t data_offset;

      uint32_t info_size = 40; // BITMAPINFOHEADER size
      int32_t w;
      int32_t h;
      uint16_t planes = 1;
      uint16_t bpp;
      uint32_t compression = 0;
      uint32_t image_size;
      int32_t res_x = 0;
      int32_t res_y = 0;
      uint32_t palette_cols = 0; // default
      uint32_t important_cols = 0;
    };
#pragma pack(pop)

    BMPHeader head;
    head.file_size = sizeof(head) + palette_size * 4 + data_size;
    head.data_offset = sizeof(head) + palette_size * 4;

    head.w = bounds.w;
    head.h = bounds.h;
    head.bpp = pixel_stride * 8;
    head.image_size = data_size;

    file.write(0, sizeof(head), reinterpret_cast<char *>(&head));

    uint32_t offset = sizeof(head);

    if(format == PixelFormat::P) {
      for(auto i = 0u; i < palette_size; i++) {
        uint8_t col[4]{palette[i].b, palette[i].g, palette[i].r, palette[i].a};
        file.write(offset, 4, reinterpret_cast<char *>(col));
        offset += 4;
      }
    }

    for(int y = 0; y < bounds.h; y++) {
        // flip y
        auto in_offset = (bounds.h - 1 - y) * row_stride;

        if(pixel_stride == 1)
          file.write(offset, row_stride, reinterpret_cast<char *>(data + in_offset));
        else {
          // r/b swap
          char pixel[4];

          for(int x = 0; x < bounds.w; x++) {
            pixel[0] = data[in_offset + x * pixel_stride + 2];
            pixel[1] = data[in_offset + x * pixel_stride + 1];
            pixel[2] = data[in_offset + x * pixel_stride + 0];

            if(pixel_stride == 4)
              pixel[3] = data[in_offset + x * pixel_stride + 3];

            file.write(offset + x * pixel_stride, pixel_stride, pixel);
          }
        }
        offset += row_stride;
    }

    return true;
  }

  void Surface::init() {
    clip = Rect(0, 0, bounds.w, bounds.h);

    pixel_stride = pixel_format_stride[static_cast<uint8_t>(format)];
    row_stride = pixel_stride * bounds.w;
    
    switch (format) {
    case PixelFormat::RGBA: {
      pbf = RGBA_RGBA;
      bbf = RGBA_RGBA;
    }break;
    case PixelFormat::RGB: {
      pbf = RGBA_RGB;
      bbf = RGBA_RGB;
    }break;
    case PixelFormat::P: {
      pbf = P_P;
      bbf = P_P;
    }break;
    case PixelFormat::M: {
      pbf = M_M;
      bbf = M_M;
    }break;
    }
  }

  /**
   * Generate mipmaps for surface
   *
   * \param depth `uint8_t`
   */
  void Surface::generate_mipmaps(uint8_t depth) {
    uint16_t w = bounds.w;
    uint16_t h = bounds.h;

    mipmaps.reserve(depth + 1);
    
    Surface *src = this;
    mipmaps.push_back(src);
    
    // offset the data pointer to the end
    uint8_t *mipmap_data = data + (row_stride * bounds.h);

    do {
      w /= 2;
      h /= 2;      
      Surface *dest = new Surface(mipmap_data, PixelFormat::RGBA, Size(w, h));
      mipmaps.push_back(dest);
      
      for (int y = 0; y < h; y++) {      
        for (int x = 0; x < w; x++) {
          // sample the average ARGB values
          Pen *c1, *c2, *c3, *c4;
          if (src->format == PixelFormat::P) {
            c1 = &src->palette[*src->ptr(Point(x * 2, y * 2))];
            c2 = &src->palette[*src->ptr(Point(x * 2 + 1, y * 2))];
            c3 = &src->palette[*src->ptr(Point(x * 2 + 1, y * 2 + 1))];
            c4 = &src->palette[*src->ptr(Point(x * 2, y * 2 + 1))];
          }
          else {
            c1 = (Pen *)(src->ptr(Point(x * 2, y * 2)));
            c2 = (Pen *)(src->ptr(Point(x * 2 + 1, y * 2)));
            c3 = (Pen *)(src->ptr(Point(x * 2 + 1, y * 2 + 1)));
            c4 = (Pen *)(src->ptr(Point(x * 2, y * 2 + 1)));
          }

          uint8_t a = (c1->a + c2->a + c3->a + c4->a) / 4;
          uint8_t r = (c1->r + c2->r + c3->r + c4->r) / 4;
          uint8_t g = (c1->g + c2->g + c3->g + c4->g) / 4;
          uint8_t b = (c1->b + c2->b + c3->b + c4->b) / 4;          
          a = 255;

          dest->pen = Pen(r, g, b, a);
          dest->pixel(Point(x, y));
        }
      }

      src = dest;    
      mipmap_data += (src->row_stride * src->bounds.h);
    } while (--depth);
  }

  /**
   * Blit a sprite to the surface
   *
   * \param sprite
   * \param p
   * \param t
   */
  void Surface::blit_sprite(const Rect &sprite, const Point &p, uint8_t t) {
    Rect dr = clip.intersection(Rect(p.x, p.y, sprite.w, sprite.h));  // clipped destination rect

    if (dr.empty())
      return; // after clipping there is nothing to draw

    uint8_t left = dr.x - p.x;
    uint8_t top = dr.y - p.y;
    uint8_t right = sprite.w - (sprite.w - dr.w) + left - 1;
    uint8_t bottom = sprite.h - (sprite.h - dr.h) + top - 1;    
    
    if (t & SpriteTransform::VERTICAL) {
      top    = sprite.h - 1 - top;
      bottom = sprite.h - 1 - bottom;
    }

    if (t & SpriteTransform::HORIZONTAL) {
      left  = sprite.w - 1 - left;
      right = sprite.w - 1 - right;
    }

    int y_step = top < bottom ? 1 : -1;
    int x_step = left < right ? 1 : -1;

    if(t & SpriteTransform::XYSWAP)
      x_step *= sprites->bounds.w;
      
    uint32_t dest_offset = offset(dr);
    uint32_t src_offset;    
    
    uint8_t y_count = dr.h;
    uint8_t y = top;
    do {    
      uint8_t x_count = dr.w;
      uint8_t x = left;

      if (t & SpriteTransform::XYSWAP)
        src_offset = sprites->offset(sprite.x + y, sprite.y + x);
      else
        src_offset = sprites->offset(sprite.x + x, sprite.y + y);

      bbf(sprites, src_offset, this, dest_offset, x_count, x_step);

      dest_offset += bounds.w;
      y += y_step;
    } while (--y_count);
  }

  /**
   * Blit a stretched sprite to the surface
   *
   * \param sprite
   * \param p
   * \param t
   */
  void Surface::stretch_blit_sprite(const Rect &sprite, const Rect &r, uint8_t t) {
    Rect dr = clip.intersection(r);  // clipped destination rect

    if (dr.empty())
      return; // after clipping there is nothing to draw

    float scale_x = float(sprite.w) / r.w;
    float scale_y = float(sprite.h) / r.h;

    float left = (dr.x - r.x) * scale_x;
    float top = (dr.y - r.y) * scale_y;
    float right = sprite.w - (sprite.w - (dr.w * scale_x)) + left - 1;
    float bottom = sprite.h - (sprite.h - (dr.h * scale_y)) + top - 1;

    if (t & SpriteTransform::VERTICAL) {
      top = sprite.h - 1 - top;
      bottom = sprite.h - 1 - bottom;
    }

    if (t & SpriteTransform::HORIZONTAL) {
      left = sprite.w - 1 - left;
      right = sprite.w - 1 - right;
    }

    float y_step = top < bottom ? scale_y : -scale_y;
    float x_step = left < right ? scale_x : -scale_x;
    
    uint32_t dest_offset = offset(dr);
    uint32_t src_offset;

    uint8_t y_count = dr.h;    
    float y = top;
    do {
      uint8_t x_count = dr.w;
      float x = left;
      do {
        if (t & SpriteTransform::XYSWAP)
          src_offset = sprites->offset(sprite.x + y, sprite.y + x);
        else
          src_offset = sprites->offset(sprite.x + x, sprite.y + y);

        bbf(sprites, src_offset, this, dest_offset, 1, 1);
        dest_offset++;

        x += x_step;
      } while (--x_count);

      dest_offset += bounds.w - dr.w;
      y += y_step;
    } while (--y_count);
  }

  /**
   * Blit another surface to the surface
   *
   * \param src
   * \param r
   * \param p
   * \param hflip `true` to flip the source surface horizontally
   */
  void Surface::blit(Surface *src, Rect r, Point p, bool hflip) {    
    Rect dr = clip.intersection(Rect(p.x, p.y, r.w, r.h));  // clipped destination rect

    if (dr.empty()) 
      return; // after clipping there is nothing to draw

    // offset source rect to accomodate for clipped destination rect    
    uint8_t l = dr.x - p.x; // top left corner
    uint8_t t = dr.y - p.y; 
    r.x += l; r.w -= l; r.y += t; r.h -= t;    
    r.w = dr.w; // clamp width/height
    r.h = dr.h;

    uint32_t src_offset = src->offset(r.x, r.y);

    uint8_t src_offset_flip = 0;
    int8_t src_direction = 1;
    if (hflip) {
      src_offset_flip = r.w - 1;
      src_direction = -1;
    }

    
    
    int32_t dest_offset = offset(dr);
    for (int32_t y = p.y; y < p.y + r.h; y++) {
      bbf(src, src_offset + src_offset_flip, this, dest_offset, r.w, src_direction);

      src_offset += src->bounds.w;
      dest_offset += bounds.w;      
    }
  }

  /**
   * Blit from another surface
   *
   * \param src
   * \param sr `rect` soruce
   * \param dr `rect` destination
   */
  void Surface::stretch_blit(Surface *src, Rect sr, Rect dr) {
    Rect cdr = clip.intersection(dr);  // clipped destination rect

    if (cdr.empty())
      return; // after clipping there is nothing to draw

    float sx = (sr.w) / float(dr.w);
    float sy = (sr.h) / float(dr.h);

    // offset source rect to accomodate for clipped destination rect    
    uint8_t l = cdr.x - dr.x; // top left corner
    uint8_t t = cdr.y - dr.y;

    sr.x += (sx * l);
    sr.y += (sy * t);

    sr.w = cdr.w * sx;
    sr.h = cdr.h * sy;

    float src_y = sr.y;
    for (int32_t y = cdr.y; y < cdr.y + cdr.h; y++) {
      float src_x = sr.x;
      for (int32_t x = cdr.x; x < cdr.x + cdr.w; x++) {
        bbf(src, src->offset(src_x, src_y), this, offset(x, y), 1, 1);

        src_x += sx;
      }      
      src_y += sy;
    }
  }

  /**
   * Blit a vertical span
   *
   * \param src
   * \param uv
   * \param sc
   * \param p
   * \param dc
   */
  void Surface::stretch_blit_vspan(Surface *src, Point uv, uint16_t sc, Point p, int16_t dc) {
    float v = uv.y;
    float vs = float(sc) / float(dc);

    if (p.y < 0) {
      dc += p.y;
      v += (vs * float(-p.y));
      p.y = 0;
    }

    if (dc <= 0) {
      return;
    }

    int16_t max_y = std::min(p.y + dc, bounds.h);
    for (; p.y < max_y; p.y++) {
      bbf(src, src->offset(Point(uv.x, v)), this, offset(p), 1, 1);

      v += vs;
    }
  }

  /**
   * TODO: Document this function
   *
   * \param src
   * \param r
   * \param p
   * \param f
   */
  void Surface::custom_blend(Surface *src, Rect r, Point p, std::function<void(uint8_t *psrc, uint8_t *pdest, int16_t c)> f) {
    Rect dr = clip.intersection(Rect(p.x, p.y, r.w, r.h));  // clipped destination rect

    if (dr.empty())
      return; // after clipping there is nothing to draw 

    // offset source rect to accomodate for clipped destination rect    
    uint8_t l = dr.x - p.x; // top left corner
    uint8_t t = dr.y - p.y;
    r.x += l; r.w -= l; r.y += t; r.h -= t;
    r.w = dr.w; // clamp width/height
    r.h = dr.h;

    uint8_t *psrc = src->ptr(r.x, r.y);
    uint8_t *pdest = ptr(dr.x, dr.y);
    
    for (int32_t y = 0; y < dr.h; y++) {
      f(psrc, pdest, dr.w);      

      psrc += src->bounds.w;
      pdest += bounds.w;
    }
  }

  /**
   * TODO: Document this function
   *
   * \param r
   * \param f
   */
  void Surface::custom_modify(Rect r, std::function<void(uint8_t *p, int16_t c)> f) {
    Rect dr = clip.intersection(r);  // clipped destination rect

    if (dr.empty())
      return; // after clipping there is nothing to draw

    uint8_t *p = ptr(dr.x, dr.y);

    for (int32_t y = 0; y < dr.h; y++) {
      f(p, dr.w);

      p += bounds.w;
    }
  }

  /**
   * TODO: Document this function
   *
   * \param image
   */
  void Surface::load_from_packed(File &file) {
    packed_image image;
    file.read(0, sizeof(packed_image), (char *)&image);

    int palette_entry_count = image.palette_entry_count;
    if(palette_entry_count == 0)
      palette_entry_count = 256;

    bool is_raw = image.type[6] == 'R' && image.type[7] == 'W'; // SPRITE[RW]
    bool is_rle = image.type[6] == 'R' && image.type[7] == 'L';

    bounds = Size(image.width, image.height);

    uint8_t bit_depth = log2i(std::max(1, palette_entry_count - 1)) + 1;

    uint8_t col = 0;
    uint8_t bit = 0;

    // Skip over image header to palette entries
    uint32_t offset = sizeof(packed_image);

    if (format == PixelFormat::P || !is_raw) {
      // load palette
      palette = new Pen[256];
      file.read(offset, palette_entry_count * 4, (char *)palette);
      offset += palette_entry_count * 4;
    }

    if (is_raw) {
      if(data) // just read/copy the data
        file.read(offset, image.width * image.height * pixel_format_stride[image.format], (char *)data);
      else // no data pointer, assume load_read_only is being used
        data = (uint8_t *)file.get_ptr() + offset;

      return;
    }

    // avoid allocating if in flash
    const uint8_t *image_data, *end;

    if(file.get_ptr())
      image_data = file.get_ptr() + offset;
    else {
      image_data = new uint8_t[image.width * image.height * pixel_format_stride[image.format]];
      file.read(offset, image.width * image.height * pixel_format_stride[image.format], (char *)image_data);
    }

    end = image_data + image.byte_count - offset;

    if (format == PixelFormat::P) {
      // load paletted
      uint8_t *pdest = (uint8_t *)data;

      if(is_rle) {
        auto bytes = image_data;
        int parse_state = 0;
        uint8_t count = 0;

        for (auto bytes = image_data; bytes < end; ++bytes) {
          uint8_t b = *bytes;

          for (auto j = 0; j < 8; j++) {
            switch (parse_state) {
              case 0: // flag
                if(b & (0b10000000 >> j))
                  parse_state = 1;
                else
                  parse_state = 2;
                break;
              case 1: // repeat count
                count <<= 1;
                count |= ((0b10000000 >> j) & b) ? 1 : 0;
                if (++bit == 8) {
                  parse_state = 2;
                  bit = 0;
                }
                break;

              case 2: // value
                col <<= 1;
                col |= ((0b10000000 >> j) & b) ? 1 : 0;

                if (++bit == bit_depth) {
                  for (int c = 0; c <= count; c++)
                    *pdest++ = col;

                  bit = 0; col = 0;
                  parse_state = 0;
                  count = 0;
                }
                break;
            }

          }
        }

      } else {
        for (auto bytes = image_data; bytes < end; ++bytes) {
          uint8_t b = *bytes;
          for (auto j = 0; j < 8; j++) {
            col <<= 1;
            col |= ((0b10000000 >> j) & b) ? 1 : 0;

            bit++;
            if (bit == bit_depth) {
              *pdest++ = col;
              bit = 0; col = 0;
            }
          }
        }
      }
    } else {
      // packed RGBA
      Pen *pdest = (Pen *)data;

      for (auto bytes = image_data; bytes < end; ++bytes) {
        uint8_t b = *bytes;
        for (auto j = 0; j < 8; j++) {
          col <<= 1;
          col |= ((0b10000000 >> j) & b) ? 1 : 0;

          bit++;
          if (bit == bit_depth) {
            *pdest++ = palette[col];

            bit = 0; col = 0;
          }
        }
      }

      // unpacked, no longer needed
      delete[] palette;
      palette = nullptr;
    }

    if (!file.get_ptr()) {
      delete[] image_data;
    }
  }

  /**
   * TODO: Document this function
   */
  void Surface::watermark() {
    static uint8_t logo[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 2, 0,
        0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 2, 0,
        0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 2, 0,
        0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0,
        0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 0, 0, 0, 0, 1, 0, 2, 0, 1, 0, 0,
        0, 1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0,
        0, 2, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0,
        0, 2, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0,
        0, 2, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    static Pen pens[] = { Pen(39, 39, 56), Pen(255, 255, 255), Pen(0, 255, 0) };

    uint8_t scale = bounds.w / 160;
    for (uint8_t y = 0; y < 13; y++) {
      for (uint8_t x = 0; x < 13; x++) {
        Pen &p = pens[logo[x + y * 13]];
        int o = offset(bounds.w - (15 * scale) + (x * scale), bounds.h - (15 * scale) + (y * scale));
        pbf(&p, this, o, scale);
        if (scale == 2) {
          pbf(&p, this, o + bounds.w, scale);
        }
      }
    }
  }
}

