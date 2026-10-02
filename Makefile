# SPDX-License-Identifier: GPL-2.0-or-later

CC      ?= clang
CXX     ?= clang++
ARCHS   ?= -arch arm64
CFLAGS  ?= -O2 -Wall -Wextra

FILTER  = build/rastertobrhl1210

all: $(FILTER)

$(FILTER): src/rastertobrhl1210.c
	@mkdir -p build
	$(CC) $(ARCHS) $(CFLAGS) -o $@ $< -lcups

build/rasterdump: test/rasterdump.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Wno-deprecated-declarations -o $@ $< -lcups

build/mkraster: test/mkraster.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Wno-deprecated-declarations -o $@ $< -lcups

build/brdecode: test/brdecode.cc
	@mkdir -p build
	$(CXX) -std=c++11 -O2 -o $@ $<

test: $(FILTER) build/rasterdump build/mkraster build/brdecode
	./test/run-tests.sh

install: $(FILTER)
	sudo ./install.sh

uninstall:
	sudo ./uninstall.sh

clean:
	rm -rf build

.PHONY: all test install uninstall clean
