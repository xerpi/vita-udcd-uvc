TARGET	= udcd_uvc
OBJS	= main.o log.o draw.o font_data.o

LIBS	= -ltaihenForKernel_stub -lSceSysclibForDriver_stub -lSceSysmemForDriver_stub \
	-lSceSysmemForKernel_stub -lSceThreadmgrForDriver_stub -lSceCpuForKernel_stub \
	-lSceCpuForDriver_stub -lSceUartForKernel_stub -lScePervasiveForDriver_stub \
	-lScePowerForDriver_stub -lSceIofilemgrForDriver_stub -lSceUdcdForDriver_stub \
	-lSceDisplayForDriver_stub

PREFIX	= arm-vita-eabi
CC	= $(PREFIX)-gcc
AS	= $(PREFIX)-as
OBJCOPY	= $(PREFIX)-objcopy
CFLAGS	= -Wl,-q -Wall -O0 -nostartfiles -mcpu=cortex-a9 -mthumb-interwork
ASFLAGS	=

all: $(TARGET).skprx

%.skprx: %.velf
	vita-make-fself $< $@

%.velf: %.elf
	vita-elf-create -e $(TARGET).yml $< $@

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

.PHONY: clean send

clean:
	@rm -rf $(TARGET).skprx $(TARGET).velf $(TARGET).elf $(OBJS)

send: $(TARGET).skprx
	curl -T $(TARGET).skprx ftp://$(PSVITAIP):1337/ux0:/data/tai/kplugin.skprx
	@echo "Sent."
