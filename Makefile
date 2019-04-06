TARGET	= udcd_uvc
OBJS	= main.o
LIBS	= -lSceSysmemForDriver_stub -lSceThreadmgrForDriver_stub \
	-lSceCpuForDriver_stub -lSceUdcdForDriver_stub \
	-lSceDisplayForDriver_stub -lSceIftuForDriver_stub \
	-lSceSysrootForKernel_stub -ltaihenForKernel_stub

DEBUG ?= 0
ifeq ($(DEBUG), 1)
	OBJS	+= debug/log.o debug/draw.o debug/console.o debug/font_data.o
	CFLAGS	+= -DDEBUG -Idebug
	LIBS	+= -lSceSysclibForDriver_stub -lSceIofilemgrForDriver_stub
endif

PREFIX	= arm-vita-eabi
CC	= $(PREFIX)-gcc
CFLAGS	+= -Wl,-q -Wall -O0 -nostartfiles -mcpu=cortex-a9 -mthumb-interwork -I.
DEPS	= $(OBJS:.o=.d)

all: $(TARGET).skprx

%.skprx: %.velf
	vita-make-fself -c $< $@

%.velf: %.elf
	vita-elf-create -e $(TARGET).yml $< $@

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

.PHONY: clean send

clean:
	@rm -rf $(TARGET).skprx $(TARGET).velf $(TARGET).elf $(OBJS) $(DEPS)

send: $(TARGET).skprx
	curl -T $(TARGET).skprx ftp://$(PSVITAIP):1337/ux0:/data/tai/kplugin.skprx
	@echo "Sent."

taisend: $(TARGET).skprx
	curl -T $(TARGET).skprx ftp://$(PSVITAIP):1337/ux0:/tai/$(TARGET).skprx
	@echo "Sent."

-include $(DEPS)
