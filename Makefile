#DEBUG = FALSE
#What's a debug build?

EXE = nvid2

GCC = nspire-gcc
AS  = nspire-as
GXX = nspire-g++
LD  = nspire-ld
GENZEHN = genzehn
OBJDUMP = arm-none-eabi-objdump

ZEHNFLAGS = --name "nvid2" --author "giraf-fe" --notice "mpeg4 video player" --240x320-support true \
			--ndless-min 53 --ndless-max 62 --color-support true --uses-lcd-blit true
# -Werror removed for now due to xvid warnings
SHAREDFLAGS =  -Wall -Wextra -Wpedantic -marm -finline-functions -march=armv5te -mtune=arm926ej-s -mfpu=auto -Ofast -flto -ffast-math -ffunction-sections -fdata-sections -mno-unaligned-access \
			   -fno-math-errno -fomit-frame-pointer -fgcse-sm -fgcse-las -funsafe-loop-optimizations -fno-fat-lto-objects -frename-registers -fprefetch-loop-arrays \
			  -Ixvid -DARCH_IS_32BIT -DARCH_IS_ARM
GCCFLAGS = $(SHAREDFLAGS) -Wno-incompatible-pointer-types -std=gnu2x
GXXFLAGS = $(SHAREDFLAGS) -std=c++20
LDFLAGS = -Wall -lnspireio

DISTDIR = build
OBJDIR = obj
ASMDIR = asm

OBJS = $(patsubst %.c, %.o, $(shell find . -name \*.c))
OBJS += $(patsubst %.cpp, %.o, $(shell find . -name \*.cpp))
OBJS += $(patsubst %.S, %.o, $(shell find . -name \*.S))

vpath %.tns $(DISTDIR)
vpath %.elf $(DISTDIR)

all: $(EXE).tns

%.o: %.c
	mkdir -p $(dir $(OBJDIR)/$@)
	mkdir -p $(dir $(ASMDIR)/$@)
	# Compile
	$(GCC) $(GCCFLAGS) -c $< -o $(OBJDIR)/$@
	# Disassemble the generated object file
	$(OBJDUMP) -D -C $(OBJDIR)/$@ > $(ASMDIR)/$(patsubst %.o,%.s,$@)

%.o: %.cpp
	mkdir -p $(dir $(OBJDIR)/$@)
	mkdir -p $(dir $(ASMDIR)/$@)
	# Compile
	$(GXX) $(GXXFLAGS) -c $< -o $(OBJDIR)/$@
	# Disassemble the generated object file
	$(OBJDUMP) -D -C $(OBJDIR)/$@ > $(ASMDIR)/$(patsubst %.o,%.s,$@)
	
%.o: %.S
	mkdir -p $(dir $(OBJDIR)/$@)
	mkdir -p $(dir $(ASMDIR)/$@)
	# Assemble
	$(AS) -c $< -o $(OBJDIR)/$@
	# Disassemble
	$(OBJDUMP) -D $(OBJDIR)/$@ > $(ASMDIR)/$(patsubst %.o,%.s,$@)

$(EXE).elf: $(OBJS)
	mkdir -p $(DISTDIR)
	$(LD) $(addprefix $(OBJDIR)/,$^) -o $(DISTDIR)/$@ $(LDFLAGS)

$(EXE).tns: $(EXE).elf
	$(GENZEHN) --input $(DISTDIR)/$^ --output $(DISTDIR)/$@.zehn $(ZEHNFLAGS)
	make-prg $(DISTDIR)/$@.zehn $(DISTDIR)/$@
	rm $(DISTDIR)/$@.zehn

clean:
	rm -f $(addprefix $(OBJDIR)/,$(OBJS)) $(DISTDIR)/$(EXE).tns $(DISTDIR)/$(EXE).elf $(DISTDIR)/$(EXE).zehn
	rm -rf $(ASMDIR)