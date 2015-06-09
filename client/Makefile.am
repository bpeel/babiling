SUBDIRS = data rply

bin_PROGRAMS = finvenkisto

AM_CFLAGS = \
	$(GL_CFLAGS) \
	$(SDL_CFLAGS) \
	$(FINVENKISTO_EXTRA_CFLAGS) \
	-I$(srcdir)/../common \
	$(NULL)

finvenkisto_SOURCES = \
	fv-array-object.c \
	fv-array-object.h \
	fv-data.c \
	fv-data.h \
	fv-error-message.c \
	fv-error-message.h \
	fv-game.c \
	fv-game.h \
	fv-gl.c \
	fv-gl.h \
	fv-gl-funcs.h \
	fv-hud.c \
	fv-hud.h \
	fv-image.c \
	fv-image.h \
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
	fv-model.c \
	fv-model.h \
	fv-network.c \
	fv-network.h \
	fv-paint-state.h \
	fv-person.h \
	fv-person-painter.c \
	fv-person-painter.h \
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

finvenkisto_LDADD = \
	$(SDL_LIBS) \
	$(FINVENKISTO_EXTRA_LIBS) \
	rply/librply.a \
	$(builddir)/../common/libcommon.a \
	$(NULL)

EXTRA_DIST = \
	fv-map.png \
	make-map.py \
	$(NULL)
