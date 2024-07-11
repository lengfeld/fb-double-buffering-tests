
#SYSROOTS = /home/stc/yocto/BSP-Yocto-i.MX6-PD16.1.0/build/tmp/sysroots

#CC = $(SYSROOTS)/x86_64-linux/usr/bin/arm-phytec-linux-gnueabi/arm-phytec-linux-gnueabi-gcc -march=armv7-a -mfpu=neon  -mfloat-abi=hard -mcpu=cortex-a9 --sysroot=$(SYSROOTS)/phyboard-mira-imx6-4

TARGETS = fb-tests

# see https://stackoverflow.com/questions/154630/recommended-gcc-warning-options-for-c
CFLAGS += -Wall -std=c99 -g -O2 -pedantic -Wall -Wshadow -Wpointer-arith
CFLAGS += -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes

fb-tests: fb-tests.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -rf $(TARGETS)
