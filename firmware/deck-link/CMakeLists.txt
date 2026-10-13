# ESP-IDF component: the C core only (the Arduino wrapper, DeckLink.cpp, is
# for Arduino builds). Put this folder in your project's components/ (or
# point EXTRA_COMPONENT_DIRS at it) and include "deck_link.h".
idf_component_register(
  SRCS
    "src/deck_link.c"
    "src/deck_tiles.c"
    "src/cobs.c"
    "src/crc16.c"
    "src/proto/link.pb.c"
    "src/proto/pb_common.c"
    "src/proto/pb_encode.c"
    "src/proto/pb_decode.c"
  INCLUDE_DIRS "src" "src/proto"
)
