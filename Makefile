TARGET	= udcd_uvc
OBJS	= main.o conversion.o log.o draw.o font_data.o

LIBS	= -lSceSysclibForDriver_stub -lSceSysmemForDriver_stub \
	-lSceSysmemForKernel_stub -lSceThreadmgrForDriver_stub -lSceCpuForKernel_stub \
	-lSceCpuForDriver_stub -lSceUdcdForDriver_stub -lSceDisplayForDriver_stub \
	-lSceIofilemgrForDriver_stub -lSceAvcodecForDriver_stub -ltaihenForKernel_stub \
	-lSceAppMgrForDriver_stub

PREFIX	= arm-vita-eabi
CC	= $(PREFIX)-gcc
CFLAGS	= -Wl,-q -Wall -O0 -nostartfiles -mcpu=cortex-a9 -mthumb-interwork
DEPS	= $(OBJS:.o=.d)

all: $(TARGET).skprx

release: CFLAGS += -DRELEASE
release: $(TARGET).skprx

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
