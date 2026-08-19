# SDL3 desktop build from upstream PR #1353, with post-PR dev fixes layered on top.
include Makefile.sdl3

# Preserve the newer dev fix that makes the Discord SDK discoverable from the
# macOS app bundle. Extra legacy rpaths from the upstream SDL3 snapshot are
# harmless; @executable_path is the authoritative bundle-local lookup path.
ifeq ($(OSX_BUILD),1)
  ifeq ($(DISCORD_SDK),1)
    LDFLAGS += -Wl,-rpath,@executable_path
  endif
endif
