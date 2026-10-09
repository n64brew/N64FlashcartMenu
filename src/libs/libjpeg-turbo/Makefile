PROJECT_DIR = libjpeg-turbo-src
BUILD_DIR = build

include $(N64_INST)/include/n64.mk

N64_CFLAGS += -I. -I$(PROJECT_DIR)/src -DHIDDEN=
CFLAGS += $(N64_CFLAGS) -fno-lto

SRCS = \
	$(PROJECT_DIR)/src/jcapimin.c \
	$(PROJECT_DIR)/src/wrapper/jcapistd-8.c \
	$(PROJECT_DIR)/src/wrapper/jcapistd-12.c \
	$(PROJECT_DIR)/src/wrapper/jcapistd-16.c \
	$(PROJECT_DIR)/src/wrapper/jccoefct-8.c \
	$(PROJECT_DIR)/src/wrapper/jccoefct-12.c \
	$(PROJECT_DIR)/src/wrapper/jccolor-8.c \
	$(PROJECT_DIR)/src/wrapper/jccolor-12.c \
	$(PROJECT_DIR)/src/wrapper/jccolor-16.c \
	$(PROJECT_DIR)/src/wrapper/jcdctmgr-8.c \
	$(PROJECT_DIR)/src/wrapper/jcdctmgr-12.c \
	$(PROJECT_DIR)/src/wrapper/jcdiffct-8.c \
	$(PROJECT_DIR)/src/wrapper/jcdiffct-12.c \
	$(PROJECT_DIR)/src/wrapper/jcdiffct-16.c \
	$(PROJECT_DIR)/src/jchuff.c \
	$(PROJECT_DIR)/src/jcicc.c \
	$(PROJECT_DIR)/src/jcinit.c \
	$(PROJECT_DIR)/src/jclhuff.c \
	$(PROJECT_DIR)/src/wrapper/jclossls-8.c \
	$(PROJECT_DIR)/src/wrapper/jclossls-12.c \
	$(PROJECT_DIR)/src/wrapper/jclossls-16.c \
	$(PROJECT_DIR)/src/wrapper/jcmainct-8.c \
	$(PROJECT_DIR)/src/wrapper/jcmainct-12.c \
	$(PROJECT_DIR)/src/wrapper/jcmainct-16.c \
	$(PROJECT_DIR)/src/jcmarker.c \
	$(PROJECT_DIR)/src/jcmaster.c \
	$(PROJECT_DIR)/src/jcomapi.c \
	$(PROJECT_DIR)/src/jcparam.c \
	$(PROJECT_DIR)/src/jcphuff.c \
	$(PROJECT_DIR)/src/wrapper/jcprepct-8.c \
	$(PROJECT_DIR)/src/wrapper/jcprepct-12.c \
	$(PROJECT_DIR)/src/wrapper/jcprepct-16.c \
	$(PROJECT_DIR)/src/wrapper/jcsample-8.c \
	$(PROJECT_DIR)/src/wrapper/jcsample-12.c \
	$(PROJECT_DIR)/src/wrapper/jcsample-16.c \
	$(PROJECT_DIR)/src/jctrans.c \
	$(PROJECT_DIR)/src/jdapimin.c \
	$(PROJECT_DIR)/src/wrapper/jdapistd-8.c \
	$(PROJECT_DIR)/src/wrapper/jdapistd-12.c \
	$(PROJECT_DIR)/src/wrapper/jdapistd-16.c \
	$(PROJECT_DIR)/src/jdatadst.c \
	$(PROJECT_DIR)/src/jdatasrc.c \
	$(PROJECT_DIR)/src/wrapper/jdcoefct-8.c \
	$(PROJECT_DIR)/src/wrapper/jdcoefct-12.c \
	$(PROJECT_DIR)/src/wrapper/jdcolor-8.c \
	$(PROJECT_DIR)/src/wrapper/jdcolor-12.c \
	$(PROJECT_DIR)/src/wrapper/jdcolor-16.c \
	$(PROJECT_DIR)/src/wrapper/jddctmgr-8.c \
	$(PROJECT_DIR)/src/wrapper/jddctmgr-12.c \
	$(PROJECT_DIR)/src/wrapper/jddiffct-8.c \
	$(PROJECT_DIR)/src/wrapper/jddiffct-12.c \
	$(PROJECT_DIR)/src/wrapper/jddiffct-16.c \
	$(PROJECT_DIR)/src/jdhuff.c \
	$(PROJECT_DIR)/src/jdicc.c \
	$(PROJECT_DIR)/src/jdinput.c \
	$(PROJECT_DIR)/src/jdlhuff.c \
	$(PROJECT_DIR)/src/wrapper/jdlossls-8.c \
	$(PROJECT_DIR)/src/wrapper/jdlossls-12.c \
	$(PROJECT_DIR)/src/wrapper/jdlossls-16.c \
	$(PROJECT_DIR)/src/wrapper/jdmainct-8.c \
	$(PROJECT_DIR)/src/wrapper/jdmainct-12.c \
	$(PROJECT_DIR)/src/wrapper/jdmainct-16.c \
	$(PROJECT_DIR)/src/jdmarker.c \
	$(PROJECT_DIR)/src/jdmaster.c \
	$(PROJECT_DIR)/src/wrapper/jdmerge-8.c \
	$(PROJECT_DIR)/src/wrapper/jdmerge-12.c \
	$(PROJECT_DIR)/src/jdphuff.c \
	$(PROJECT_DIR)/src/wrapper/jdpostct-8.c \
	$(PROJECT_DIR)/src/wrapper/jdpostct-12.c \
	$(PROJECT_DIR)/src/wrapper/jdpostct-16.c \
	$(PROJECT_DIR)/src/wrapper/jdsample-8.c \
	$(PROJECT_DIR)/src/wrapper/jdsample-12.c \
	$(PROJECT_DIR)/src/wrapper/jdsample-16.c \
	$(PROJECT_DIR)/src/jdtrans.c \
	$(PROJECT_DIR)/src/jerror.c \
	$(PROJECT_DIR)/src/jfdctflt.c \
	$(PROJECT_DIR)/src/wrapper/jfdctfst-8.c \
	$(PROJECT_DIR)/src/wrapper/jfdctfst-12.c \
	$(PROJECT_DIR)/src/wrapper/jfdctint-8.c \
	$(PROJECT_DIR)/src/wrapper/jfdctint-12.c \
	$(PROJECT_DIR)/src/wrapper/jidctflt-8.c \
	$(PROJECT_DIR)/src/wrapper/jidctflt-12.c \
	$(PROJECT_DIR)/src/wrapper/jidctfst-8.c \
	$(PROJECT_DIR)/src/wrapper/jidctfst-12.c \
	$(PROJECT_DIR)/src/wrapper/jidctint-8.c \
	$(PROJECT_DIR)/src/wrapper/jidctint-12.c \
	$(PROJECT_DIR)/src/wrapper/jidctred-8.c \
	$(PROJECT_DIR)/src/wrapper/jidctred-12.c \
	$(PROJECT_DIR)/src/jmemmgr.c \
	$(PROJECT_DIR)/src/jmemnobs.c \
	$(PROJECT_DIR)/src/jpeg_nbits.c \
	$(PROJECT_DIR)/src/wrapper/jquant1-8.c \
	$(PROJECT_DIR)/src/wrapper/jquant1-12.c \
	$(PROJECT_DIR)/src/wrapper/jquant2-8.c \
	$(PROJECT_DIR)/src/wrapper/jquant2-12.c \
	$(PROJECT_DIR)/src/wrapper/jutils-8.c \
	$(PROJECT_DIR)/src/wrapper/jutils-12.c \
	$(PROJECT_DIR)/src/wrapper/jutils-16.c \
	$(PROJECT_DIR)/src/jaricom.c \
	$(PROJECT_DIR)/src/jdarith.c

OBJS = $(SRCS:%.c=$(BUILD_DIR)/%.o)
LIB = $(BUILD_DIR)/libjpeg.a

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "    [CC] $<"
	$(N64_CC) $(CFLAGS) -c -o $@ $<

$(LIB): $(OBJS)
	@mkdir -p $(dir $@)
	@echo "    [AR] $@"
	$(N64_AR) rcs $@ $^

all: $(LIB)
.PHONY: all

clean:
	rm -rf $(BUILD_DIR)
.PHONY: clean
