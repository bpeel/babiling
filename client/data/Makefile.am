HUD_SVGS = \
	hud/title.svg \
	$(NULL)

HUD_PNGS = \
	$(HUD_SVGS:.svg=.png) \
	$(NULL)

OTHER_SVGS = \
	$(NULL)

OTHER_PNGS = $(OTHER_SVGS:.svg=.png)

SHADERS = \
	fv-hud-vertex.glsl \
	fv-color-fragment.glsl \
	fv-lighting.glsl \
	fv-lighting-texture-fragment.glsl \
	fv-special-color-vertex.glsl \
	fv-special-texture-vertex.glsl \
	fv-hud-fragment.glsl \
	fv-map-vertex.glsl \
	fv-person-fragment.glsl \
	fv-person-vertex.glsl \
	$(NULL)

MODELS = \
	person.ply \
	table.ply \
	chair.ply \
	barrel.ply \
	bar.ply \
	flag-post-1-2-m.ply \
	flag-post-2-3-m.ply \
	flag-post-7-10-n.ply \
	flag-post-3-5-m.ply \
	$(NULL)

# The order here must match the order for the image numbers used in
# the map
BLOCK_IMAGES = \
	walltop.png \
	brick-flooring.png \
	grass.png \
	brick-wall.png \
	inner-wall.png \
	welcome-poster1.png \
	welcome-poster2.png \
	$(NULL)

map-texture.png : make-map-texture.py $(BLOCK_IMAGES)
	$(AM_V_GEN)python3 $(srcdir)/make-map-texture.py $@ \
	$(BLOCK_IMAGES:%=$(srcdir)/%)

IMAGES = \
	$(OTHER_PNGS) \
	bambo1.png \
	bambo2.png \
	bambo3.png \
	gufujestro.png \
	map-texture.png \
	pyjamas.png \
	hud.png \
	toiletguy.png \
	bar-texture.png \
	union-jack.png \
	usa-flag.png \
	spanish-flag.png \
	italian-flag.png \
	tricolour.png \
	portuguese-flag.png \
	brazilian-flag.png \
	japanese-flag.png \
	chinese-flag.png \
	russian-flag.png \
	dutch-flag.png \
	south-korean-flag.png \
	german-flag.png \
	$(NULL)

NON_IMAGES = \
	ARTCREDITS.txt \
	$(SHADERS) \
	$(MODELS) \
	$(NULL)

imagesdir = $(datadir)/babiling
dist_images_DATA = \
	$(IMAGES) \
	$(NON_IMAGES) \
	$(NULL)

EXTRA_DIST = \
	$(HUD_SVGS) \
	$(HUD_PNGS) \
	$(OTHER_SVGS) \
	$(OTHER_PNGS) \
	fv-image-data-enum.h \
	fv-image-data-files.h \
	fv-image-data-stamp \
	hud.png \
	hud-layout.h \
	make-atlas.py \
	make-map-texture.py \
	hud-stamp \
	$(MODELS:.ply=.blend) \
	$(BLOCK_IMAGES) \
	make-flag.py \
	flag-notes.txt \
	get-flags.py \
	$(NULL)

.svg.png :
	$(AM_V_GEN)mkdir -p `dirname $@`
	$(AM_V_at)inkscape -e $@ $<

hud.png hud-layout.h : hud-stamp

hud-stamp : $(HUD_PNGS) make-atlas.py
	$(AM_V_GEN)mkdir -p $(builddir)/hud
	$(AM_V_at)python3 $(srcdir)/make-atlas.py \
	hud.png hud-layout.h $(filter %.png,$^)
	$(AM_V_at)touch $@

fv-image-data-stamp : $(IMAGES)
	$(AM_V_GEN)for x in $(IMAGES); do \
	echo "$$x" | \
	sed -e 's/\..*$$/,/' -e 's/^/fv_image_data_/' -e 's/-/_/g' \
	| tr '[a-z]' '[A-Z]'; \
	done > fv-image-data-enum.h
	$(AM_V_at)for x in $(IMAGES); \
	do echo "\"$$x\"," ; \
	done > fv-image-data-files.h
	$(AM_V_at)touch $@

DISTCLEANFILES = \
	$(HUD_PNGS) \
	hud-layout.h \
	fv-image-data-enum.h \
	fv-image-data-files.h \
	fv-image-data-stamp \
	hud.png \
	hud-stamp \
	$(NULL)

EXTRA_TARGETS = fv-image-data-stamp

if IS_EMSCRIPTEN
EXTRA_TARGETS += babiling-data.js

babiling-data.js : $(dist_images_DATA)
	$(AM_V_GEN)python $$EMSCRIPTEN/tools/file_packager.py \
	babiling.data --js-output=$@ --preload $^
endif

all-local : $(EXTRA_TARGETS)
