# Windows (MinGW): mingw32-make      -> kollapse.exe (terminal) and kollapsew.exe (window)
# Linux:           make              -> kollapse (terminal) and kollapsew (X11 window, when libx11-dev is there);
#                  make install      -> ~/.local/bin.  Media keys (MPRIS) need libdbus-1-dev; without it they're left out.
CC      = gcc
CFLAGS  = -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-variable
# the release build passes VERSION=1.2.3 (shown by --version and in the F1 help)
VERSION ?= dev
CFLAGS += -DKOLLAPSE_VERSION=\"$(VERSION)\"
XMP     = third_party/libxmp
# file lists from libxmp's own src/*/Makefile (4.7.4)
XMP_SRC = virtual format period player read_event dataio misc mkstemp md5 lfo scan control far_extras flt_extras \
          med_extras filter effects flow mixer mix_all rng load_helpers load hio hmn_extras extras smix path \
          filetype memio tempfile mix_paula miniz_tinfl win32
XMP_LDR = common iff itsex lzw voltable sample vorbis \
          xm_load mod_load s3m_load stm_load 669_load far_load mtm_load ptm_load okt_load ult_load mdl_load it_load \
          stx_load pt3_load sfx_load flt_load st_load emod_load imf_load digi_load fnk_load ice_load liq_load ims_load \
          masi_load masi16_load amf_load stim_load mmd_common mmd1_load mmd3_load rtm_load dt_load no_load arch_load \
          sym_load med2_load med3_load med4_load dbm_load umx_load gdm_load pw_load gal5_load gal4_load mfp_load \
          asylum_load muse_load hmn_load mgt_load chip_load abk_load coco_load xmf_load
XMP_PW  = prowiz ptktable tuning ac1d di eureka fc-m fuchs fuzzac gmc heatseek ksm mp np1 np2 np3 p61a pm10c pm18a \
          pha prun1 prun2 tdd unic unic2 wn zen tp1 tp3 p40 xann theplayer pp10 pp21 starpack titanics skyt \
          novotrade hrt noiserun
XMP_DEP = depacker ppdepack unsqsh mmcmp s404_dec arc arcfs arc_unpack lzx lzx_unpack ice ice_unpack miniz_zip unzip \
          gunzip uncompress bunzip2 unlha unxz xz_dec_lzma2 xz_dec_stream crc32 xfnmatch ptpopen xfd xfd_link
XMP_LHA = ext_header lha_file_header lha_input_stream lha_decoder lha_reader lha_basic_reader lh1_decoder lh5_decoder \
          lh6_decoder lh7_decoder lhx_decoder lk7_decoder lz5_decoder lzs_decoder null_decoder pm1_decoder pm2_decoder macbinary
XMPDEF  = -DLIBXMP_STATIC -D_USE_MATH_DEFINES -I$(XMP)/include
ZDEF    = -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DMINIZ_NO_ARCHIVE_WRITING_APIS -Ithird_party/miniz
XMPLIB := $(XMPDEF) -I$(XMP)/src
XMPDEF += $(ZDEF)
vpath %.c $(XMP)/src $(XMP)/src/loaders $(XMP)/src/loaders/prowizard $(XMP)/src/depackers $(XMP)/src/depackers/lhasa

ifeq ($(OS),Windows_NT)
B       = build
X       = .exe
SYSLIBS = -lwinmm -lwininet -lm
TERM_C  = src/term_win32.c
all: kollapse.exe kollapsew.exe
else
B       = build-linux
X       =
SYSLIBS = -lpthread -lm -ldl
CFLAGS += -Wno-format-truncation -Wno-stringop-truncation -Wno-unused-result   # snprintf cutting long paths is intended
TERM_C  = src/term_posix.c
XMPDEF += -Ithird_party/miniaudio
DBUS_INC := $(wildcard /usr/include/dbus-1.0) $(wildcard /usr/lib/*/dbus-1.0/include /usr/lib/dbus-1.0/include /usr/lib64/dbus-1.0/include)
ifneq ($(wildcard /usr/include/dbus-1.0/dbus/dbus.h),)
XMPDEF  += -DHAVE_DBUS $(addprefix -I,$(DBUS_INC))
SYSLIBS += -ldbus-1
endif
ifneq ($(wildcard /usr/include/X11/Xlib.h),)
WINBIN  = kollapsew
endif
all: kollapse $(WINBIN)
endif

XMPOBJ  = $(addprefix $(B)/,$(addsuffix .o,$(XMP_SRC) $(XMP_LDR) $(XMP_PW) $(XMP_DEP) $(XMP_LHA)))
LIBS    = $(B)/libxmp.a $(B)/miniz.o $(SYSLIBS)
CORE_C  = src/fb.c src/core.c src/xmsave.c src/modland.c src/mpris.c
CORE_H  = src/fb.h src/core.h src/compat.h src/term.h src/mpris.h

$(B)/%.o: %.c
	@mkdir -p $(B)
	$(CC) -std=gnu99 -O2 $(XMPLIB) -c $< -o $@

$(B)/libxmp.a: $(XMPOBJ)
	ar rcs $@ $^

$(B)/miniz.o: third_party/miniz/miniz.c
	@mkdir -p $(B)
	$(CC) -std=gnu99 -O2 $(ZDEF) -c $< -o $@

kollapse$(X): src/tui.c $(TERM_C) $(CORE_C) $(CORE_H) $(B)/libxmp.a $(B)/miniz.o
	$(CC) $(CFLAGS) $(XMPDEF) -o $@ src/tui.c $(TERM_C) $(CORE_C) $(LIBS)

kollapsew.exe: src/gui_win32.c $(CORE_C) $(CORE_H) $(B)/libxmp.a $(B)/miniz.o
	$(CC) $(CFLAGS) $(XMPDEF) -o $@ src/gui_win32.c $(CORE_C) $(LIBS) -mwindows -lgdi32

kollapsew: src/gui_x11.c $(CORE_C) $(CORE_H) $(B)/libxmp.a $(B)/miniz.o
	$(CC) $(CFLAGS) $(XMPDEF) -o $@ src/gui_x11.c $(CORE_C) $(LIBS) -lX11

PREFIX ?= $(HOME)/.local
install: all
	mkdir -p $(PREFIX)/bin
	for f in kollapse $(WINBIN); do cp $$f $(PREFIX)/bin/.$$f.new && mv -f $(PREFIX)/bin/.$$f.new $(PREFIX)/bin/$$f || exit 1; done   # works while it's running
	@echo "installed kollapse $(WINBIN) in $(PREFIX)/bin"

clean:
	rm -rf $(B) kollapse$(X) kollapsew.exe kollapsew
