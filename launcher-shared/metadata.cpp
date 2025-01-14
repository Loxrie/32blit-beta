#include <cstring>

#include "graphics/sprite.hpp"

#include "metadata.hpp"
#include "executable.hpp"

using namespace blit;

void parse_metadata(char *data, uint16_t metadata_len, BlitGameMetadata &metadata, bool unpack_images) {
  metadata.length = metadata_len;

  auto raw_meta = reinterpret_cast<RawMetadata *>(data);
  metadata.crc32 = raw_meta->crc32;

  metadata.title = raw_meta->title;
  metadata.description = raw_meta->description;
  metadata.version = raw_meta->version;
  metadata.author = raw_meta->author;

  uint16_t offset = sizeof(RawMetadata);

  if(unpack_images && metadata.icon)
    metadata.free_surfaces();

  if(offset != metadata_len && unpack_images) {
    // icon/splash
    auto image = reinterpret_cast<packed_image *>(data + offset);
    metadata.icon = Surface::load(reinterpret_cast<uint8_t *>(data + offset));
    offset += image->byte_count;

    image = reinterpret_cast<packed_image *>(data + offset);
    metadata.splash = Surface::load(reinterpret_cast<uint8_t *>(data + offset));
  }
}