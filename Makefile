# ****************************************************************************
# *
# *  Makefile Builds Comms Handler Process / Executable
# *
# *****************************************************************************

BASE_DIR=../..

include $(BASE_DIR)/toolchain.make

ifeq ($(BUILD_PROFILE),PC)
	INSTALL_DIR=$(BASE_DIR)/pc/paxton_apps
	LIBDIR=/usr
else
	INSTALL_DIR=$(BASE_DIR)/ltib/rootfs/paxton_apps
	LIBDIR=$(BASE_DIR)/ltib/rootfs/usr
endif

# *****************************************************************************
# Add the target name of the output to be produced
# *****************************************************************************

APP_NAME := dualpipeline-imx6

TOPDIR=$(shell pwd)

PROJDIR=$(TOPDIR)

INCDIRS := -I$(SRCDIR) -I$(INCDIR) -I$(BGLIBDIR) -I${LIBINCDIR} -I$(COMMON_LIBDIR)/include -I$(ENCRYPT_LIBDIR)/include -I$(IOCTLINCDIR)/mod

# *****************************************************************************
# Add source directories to be compiled here (these don't need a Makefile)
# If we use multiple sub directories for source code we can compile here
# *****************************************************************************

VPATH:= $(SRCDIR) \
	$(BGLIBDIR)

# *****************************************************************************
# Add source files to be compiled here
# *****************************************************************************


# *****************************************************************************
# Build a release and debug version
# *****************************************************************************

OBJDIR_REL:=Object/Release
DEPDIR_REL:=Object/Release

OBJDIR_DBG:=Object/Debug
DEPDIR_DBG:=Object/Debug

# *****************************************************************************
# setup linker flags
# *****************************************************************************

# LDFLAGS = -L$(COMMON_LIBDIR)/Release -lcommon
#LDFLAGS = -L$(COMMON_LIBDIR)/Release -lcommon -L$(ENCRYPT_LIBDIR)/Release -lencrypt -L$(LIBDIR)/lib -lsqlite3 -lz -lxml2 -L$(BASE_DIR)/drivers/picwatchdog/lib/Release -lpicwatchdogmgr -lpthread -lrt

LDFLAGS = -L$(LIBDIR)/lib/gstreamer-1.0 -lgstvpu -lgstopencv -lgstrtp  -L$(LIBDIR)/lib  -lgstreamer-1.0
# *****************************************************************************
# setup include paths
# *****************************************************************************

INCLUDEFLAGS += -I$(PROJDIR)/. $(INCDIRS)

# *****************************************************************************
# setup compiler flags and code conditional compilations
# *****************************************************************************

OSFLAG = -DLINUX -D_GNU_SOURCE -D_REENTRANT

CFLAGS = $(OSFLAG) $(CDEFS) $(WARNING) $(INCLUDEFLAGS) -D_THREAD_SAFE

CFLAGS_REL = -Os -Wall -Wno-strict-aliasing $(CFLAGS_TOOLCHAIN)
CFLAGS_DBG = -Wall -Wno-strict-aliasing -g2 -DDEBUG -O0 $(CFLAGS_TOOLCHAIN)

# *****************************************************************************
# setup release and debug flags
# *****************************************************************************

REL_OBJS:= $(SRCS:%=$(OBJDIR_REL)/%.o)
REL_DEPS:= $(SRCS:%=$(DEPDIR_REL)/%.d)
DBG_OBJS:= $(SRCS:%=$(OBJDIR_DBG)/%.o)
DBG_DEPS:= $(SRCS:%=$(DEPDIR_DBG)/%.d)
TARGETDIR_REL = Release
TARGETDIR_DBG = Debug

DEPDIR_REL=./
OSFLAG = -DLINUX -D_GNU_SOURCE -D_REENTRANT

INCDIR=$(TOPDIR)/include

INCDIRS := -I./include

INCLUDEFLAGS += $(INCDIR)


CFLAGS =  $(INCLUDEFLAGS) -D_THREAD_SAFE

CFLAGS_REL = -Os -Wall -Wno-strict-aliasing $(CFLAGS_TOOLCHAIN)

TARGETS:= $(TARGETDIR_REL)/$(APP_NAME) $(TARGETDIR_DBG)/$(APP_NAME)
#TARGETS:= $(TARGETDIR_REL)/$(APP_NAME)


.PHONY: all clean release install debug

all: $(TARGETS)

debug: $(TARGETDIR_DBG)/$(APP_NAME)

release: $(TARGETDIR_REL)/$(APP_NAME)

compile:
	$(CC)  $(APP_NAME).c -o $(APP_NAME) `pkg-config --cflags --libs gstreamer-1.0`			

install:
	mkdir -p $(INSTALL_DIR)
	cp -f $(TARGETDIR_REL)/$(APP_NAME) $(INSTALL_DIR)/.

clean:
	@echo clean
	@rm -rf Object $(TARGETDIR_REL) $(TARGETDIR_DBG)

$(TARGETDIR_REL)/$(APP_NAME): $(REL_OBJS) Makefile
	@echo linking  $@
	@[ -d $(TARGETDIR_REL) ] || mkdir $(TARGETDIR_REL)
	$(CC)  $(APP_NAME).c -o $@ `pkg-config --cflags --libs gstreamer-1.0`			
	@$(STRIP) --strip-unneeded  -R=.comment -R=.note $@

$(TARGETDIR_DBG)/$(APP_NAME): $(DBG_OBJS) Makefile
	@echo linking  $@
	@[ -d $(TARGETDIR_DBG) ] || mkdir $(TARGETDIR_DBG)
	$(CC) $(APP_NAME).c -o $(APP_NAME) `pkg-config --cflags --libs gstreamer-1.0`			

$(OBJDIR_REL)/%.o: %.c Makefile
	@[ -d $(DEPDIR_REL) ] || mkdir -p $(DEPDIR_REL)
	@[ -d $(OBJDIR_REL) ] || mkdir -p $(OBJDIR_REL)
	@echo compiling $<
	$(CC)  $(APP_NAME).c -o $(APP_NAME) `pkg-config --cflags --libs gstreamer-1.0`			

$(OBJDIR_DBG)/%.o: %.c Makefile
	@[ -d $(DEPDIR_DBG) ] || mkdir -p $(DEPDIR_DBG)
	@[ -d $(OBJDIR_DBG) ] || mkdir -p $(OBJDIR_DBG)
	@echo compiling $<
	$(CC)  $(APP_NAME).c -o $(APP_NAME) `pkg-config --cflags --libs gstreamer-1.0`			

$(OBJDIR_REL)/%.o: %.cpp Makefile
	@[ -d $(DEPDIR_REL) ] || mkdir -p $(DEPDIR_REL)
	@[ -d $(OBJDIR_REL) ] || mkdir -p $(OBJDIR_REL)
	@echo compiling $<
	$(CPP) -MMD -MF $(DEPDIR_REL)/$(*F).d -MT $@ $(CFLAGS) $(CFLAGS_REL) -c $< -o $@
	@[ -s $(DEPDIR_REL)/$(*F).d ] || rm -f $(DEPDIR_REL)/$(*F).d

$(OBJDIR_DBG)/%.o: %.cpp Makefile
	@[ -d $(DEPDIR_DBG) ] || mkdir -p $(DEPDIR_DBG)
	@[ -d $(OBJDIR_DBG) ] || mkdir -p $(OBJDIR_DBG)
	@echo compiling $<
	$(CPP) -MMD -MF $(DEPDIR_DBG)/$(*F).d -MT $@ $(CFLAGS) $(CFLAGS_DBG) -c $< -o $@
	@[ -s $(DEPDIR_DBG)/$(*F).d ] || rm -f $(DEPDIR_DBG)/$(*F).d

-include $(REL_DEPS) $(DBG_DEPS)
