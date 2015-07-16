SUBDIRS = data rply

bin_PROGRAMS =

if IS_EMSCRIPTEN
bin_PROGRAMS += finvenkisto.html
else
bin_PROGRAMS += finvenkisto
endif

AM_CFLAGS = \
	$(GL_CFLAGS) \
	$(SDL_CFLAGS) \
	$(OPUS_CFLAGS) \
	$(FINVENKISTO_EXTRA_CFLAGS) \
	-I$(srcdir)/../common \
	$(NULL)

if !IS_EMSCRIPTEN
AM_CFLAGS += $(PULSE_SIMPLE_CFLAGS)
endif

sources = \
	fv-audio-buffer.c \
	fv-audio-buffer.h \
	fv-array-object.c \
	fv-array-object.h \
	fv-data.c \
	fv-data.h \
	fv-error-message.h \
	fv-game.c \
	fv-game.h \
	fv-gl.c \
	fv-gl.h \
	fv-gl-funcs.h \
	fv-hud.c \
	fv-hud.h \
	fv-image-data.h \
	fv-logic.c \
	fv-logic.h \
	fv-main.c \
	fv-map.c \
	fv-map.h \
	fv-map-buffer.c \
	fv-map-buffer.h \
	fv-map-painter.c \
	fv-map-painter.h \
	fv-matrix.c \
	fv-matrix.h \
	fv-microphone.h \
	fv-model.c \
	fv-model.h \
	fv-mutex.h \
	fv-network.h \
	fv-network-common.h \
	fv-paint-state.h \
	fv-person.h \
	fv-person-painter.c \
	fv-person-painter.h \
	fv-recorder.c \
	fv-recorder.h \
	fv-shader-data.c \
	fv-shader-data.h \
	fv-transform.c \
	fv-transform.h \
	stb_image.h \
	$(NULL)

BUILT_SOURCES = \
	fv-map.c \
	$(NULL)

fv-map.c : fv-map.png make-map.py
	$(AM_V_GEN)python3 $(srcdir)/make-map.py $(srcdir)/fv-map.png > $@

ldadd = \
	$(SDL_LIBS) \
	$(OPUS_LIBS) \
	$(FINVENKISTO_EXTRA_LIBS) \
	rply/librply.a \
	$(builddir)/../common/libcommon.a \
	$(NULL)

finvenkisto_SOURCES = \
	$(sources) \
	fv-error-message-native.c \
	fv-network-native.c \
	fv-image-data-native.c \
	fv-microphone-pulse.c \
	$(NULL)
finvenkisto_LDADD = \
	$(ldadd) \
	$(PULSE_SIMPLE_LIBS) \
	$(NULL)

finvenkisto_html_SOURCES = \
	$(sources) \
	fv-error-message-emscripten.c \
	fv-network-emscripten.c \
	fv-image-data-emscripten.c \
	fv-microphone-emscripten.c \
	$(NULL)
finvenkisto_html_LDFLAGS = \
	--pre-js $(srcdir)/configure-emscripten.js \
	--pre-js $(builddir)/data/finvenkisto-data.js \
	--shell-file $(srcdir)/finvenkisto-shell.html \
	$(NULL)
finvenkisto_html_LDADD = \
	$(ldadd) \
	$(NULL)

EXTRA_DIST = \
	configure-emscripten.js \
	fv-map.png \
	make-map.py \
	$(NULL)
