AOCC ?= /opt/aocc
ifeq ($(origin CXX),default)
CXX := $(AOCC)/bin/clang++
endif
CXX ?= $(AOCC)/bin/clang++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -pthread -Wall -Wextra -Wpedantic
LDFLAGS += -pthread
CPPFLAGS += -Isrc
ifeq ($(CXX),$(AOCC)/bin/clang++)
# Usa os headers e o runtime C++ fornecidos com o AOCC.
CXXFLAGS += -stdlib=libc++
LDFLAGS += -stdlib=libc++ -Wl,-rpath,$(AOCC)/lib
endif
CORE = inflate pdf pdf_image cff fax truetype jpeg raster render
OBJECTS = $(addprefix build/,$(addsuffix .o,$(CORE)))
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
DESTDIR ?=

.PHONY: all test clean install uninstall
all: build/maviles-pdf-reader

build:
	mkdir -p $@

build/%.o: src/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

build/maviles-pdf-reader: $(OBJECTS) build/main.o build/viewer.o build/x11.o
	$(CXX) $(LDFLAGS) $^ -o $@

build/tests: tests/tests.cpp tests/deflate_fixtures.h $(OBJECTS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/tests.cpp $(OBJECTS) -o $@

build/viewer-tests: tests/viewer_tests.cpp build/viewer.o | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

test: build/tests build/viewer-tests
	./build/tests
	./build/viewer-tests

install: build/maviles-pdf-reader
	install -d -- "$(DESTDIR)$(BINDIR)"
	install -m 755 -- build/maviles-pdf-reader "$(DESTDIR)$(BINDIR)/maviles-pdf-reader"
	ln -sfnT -- maviles-pdf-reader "$(DESTDIR)$(BINDIR)/mpr"

uninstall:
	rm -f -- "$(DESTDIR)$(BINDIR)/mpr" "$(DESTDIR)$(BINDIR)/maviles-pdf-reader"

clean:
	rm -rf build

-include $(wildcard build/*.d)
