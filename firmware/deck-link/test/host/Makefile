# Host tests for the Deck Link core (Apple clang or gcc).
#   make test       build and run the unit tests
#   make board_sim  build the board simulator
SRC := ../../src
CC ?= cc
CFLAGS ?= -std=c99 -O1 -g -Wall -Wextra -Werror=implicit-function-declaration
CPPFLAGS += -I$(SRC) -D_POSIX_C_SOURCE=200809L
CORE := $(SRC)/deck_link.c $(SRC)/deck_tiles.c $(SRC)/cobs.c $(SRC)/crc16.c \
	$(SRC)/proto/link.pb.c $(SRC)/proto/pb_common.c $(SRC)/proto/pb_encode.c $(SRC)/proto/pb_decode.c
HDRS := $(wildcard $(SRC)/*.h $(SRC)/proto/*.h)
OUT := build

all: $(OUT)/test_link $(OUT)/board_sim

test: $(OUT)/test_link
	./$(OUT)/test_link

board_sim: $(OUT)/board_sim

$(OUT)/test_link: test_link.c $(CORE) $(HDRS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ test_link.c $(CORE)

$(OUT)/board_sim: board_sim.c $(CORE) $(HDRS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ board_sim.c $(CORE)

$(OUT):
	mkdir -p $@

clean:
	rm -rf $(OUT)

.PHONY: all test board_sim clean
