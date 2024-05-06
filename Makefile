prefix := /usr/local
install_runner := sudo

CXXFLAGS := $(CXXFLAGS) -MMD -MP -Wall -Wextra -Werror -Wtype-limits -Wpedantic -pedantic-errors \
	   -std=c++23 -D_GNU_SOURCE -march=native -pipe -Isrc
CXXFLAGS_release := -DNDEBUG -O3 -flto
CXXFLAGS_debug := -ggdb3 -fsanitize=address -fsanitize=undefined -D_GLIBCXX_DEBUG

LDFLAGS := $(LDFLAGS) -pipe
LDFLAGS_release := -flto -s
LDFLAGS_debug := -fsanitize=address -fsanitize=undefined
LDLIBS := -lstdc++

MAKEFLAGS := -j $(shell nproc)
.DEFAULT_GOAL := debug

prog := ath10k-fixer
modes := debug release
cppfiles := $(wildcard src/*.cpp)
objects := $(notdir $(cppfiles:.cpp=.o))
build_dirs := $(addprefix build_,$(modes))

$(build_dirs):
	mkdir -p $@

define template
objects_$(1) := $$(addprefix build_$(1)/,$$(objects))
$$(objects_$(1)): CXXFLAGS += $$(CXXFLAGS_$(1))
$$(objects_$(1)): | build_$(1)
build_$(1)/%.o: src/%.cpp
	$$(COMPILE.cc) -o $$@ $$<
-include $$(objects_$(1):.o=.d)
build_$(1)/$$(prog): LDFLAGS += $$(LDFLAGS_$(1))
build_$(1)/$$(prog): $$(objects_$(1))
	   $$(LINK.o) $$^ $$(LDLIBS) -o $$@
$(1): build_$(1)/$$(prog)
endef

$(foreach mode,$(modes),$(eval $(call template,$(mode))))

all: $(modes)

clean:
	$(RM) -r $(build_dirs) compile_commands.json

compile_commands.json: Makefile $(cppfiles)
	bear -- $(MAKE) -B debug

install: build_release/$(prog) $(prog).service
	$(install_runner) install -D -t $(DESTDIR)$(prefix)/bin $<
	sed 's:{PREFIX}:$(prefix):g' $(prog).service | \
		$(install_runner) install -D -m644 -T /dev/stdin \
		$(DESTDIR)$(prefix)/lib/systemd/system/$(prog).service

uninstall:
	$(install_runner) $(RM) \
		$(DESTDIR)$(prefix)/bin/$(prog) \
		$(DESTDIR)$(prefix)/lib/systemd/system/$(prog).service

.PHONY: all clean compile_commands.json install uninstall $(modes)
.DELETE_ON_ERROR:
