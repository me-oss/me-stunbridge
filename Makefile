# This is a simple makefile that compiles multiple C++ source files

# set the names here to be the names of your source files with the
# .cxx or .cpp replaced by .o
# Be *** SURE *** to put the .o files here rather than the source files


#BUILD_MODE	= pc
BUILD_MODE	= arm

include debug.mk


Objects =  stunbridge.o client_main.o symmetricNATTest.o relay.o DDNS_API.o debuglog.o
TargetName = stunbridge
#------------ no need to change between these lines -------------------

ifeq ($(BUILD_MODE),arm)
	CROSS_COMPILE = arm-linux-
	CFLAGS = -DDEBUG -Iinclude
	LIB_FOLDER = libs
else
	CFLAGS = -DDEBUG -DSIMULATION -Iinclude_pc
	LIB_FOLDER=libs_pc
	CROSS_COMPILE = 
	
endif
Objects += minIni.o

#CFLAGS += -DDEBUG --exceptions 
LFLAGS = $(LIB_FOLDER)/libcurl.a $(LIB_FOLDER)/libudt.a $(LIB_FOLDER)/libpjnath.a $(LIB_FOLDER)/libpjlib-util.a $(LIB_FOLDER)/libpj.a
#CC = gcc



CC = $(CROSS_COMPILE)gcc
C++ = $(CROSS_COMPILE)g++
STRIP = $(CROSS_COMPILE)strip

ifeq ($(BUILD_MODE),arm)
LFLAGS += /opt/crosstool/arm9-eabi-uclibc-0.9.30/lib/libstdc++.a
LINKER = $(CC)
else
#LFLAGS += -lstdc++
LINKER = $(C++)
endif

ifeq ($(DEBUG_HOST),pcd)
CFLAGS +=  -g -fno-omit-frame-pointer -fno-optimize-sibling-calls -fno-strict-aliasing -fno-crossjumping -falign-functions -DDEBUG_PCD -I./pcd/include
LFLAGS += pcd/libpcd.a pcd/libipc.a 
else
endif


CFLAGS += -DDMALLOC -DDMALLOC_FUNC_CHECK


LFLAGS+=-lpthread -lm

LFLAGS += pcd/libdmallocth.a
.SUFFIXES: .c .cpp

.c.o:
	$(CC) $(CFLAGS) -c $<

.cpp.o:
	$(C++) $(CFLAGS) -c $<
	
#------------ no need to change between these lines -------------------


#------------ targets --------------------------------------------
# describe how to create the target

$(TargetName): $(Objects)
	$(LINKER) --static $(CFLAGS) $(Objects) $(LFLAGS) -o $(TargetName) 
	$(STRIP) $(TargetName)
	cp $(TargetName) /tftpboot
clean:
	rm -f $(Objects) $(TargetName)




#------------ dependencies --------------------------------------------
# put the .o that depends on a .h, then colon, then TAB, then the .h

