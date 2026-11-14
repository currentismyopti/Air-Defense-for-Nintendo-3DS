# Builds the PC test harness (software renderer + scripted input) and the headless soak test.
CC ?= gcc
CFLAGS = -std=c11 -O1 -g -Wall -Wextra -Wno-restrict -fsanitize=address,undefined
SRC = ../src/main.c ../src/sim.c ../src/air.c ../src/catalog.c ../src/gfx.c ../src/view.c ../src/ui.c ../src/assets.c plat_host.c
all: adc_host soak
adc_host: $(SRC) ../src/*.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm
soak: soak.c ../src/sim.c ../src/air.c ../src/catalog.c ../src/*.h
	$(CC) $(CFLAGS) -o $@ soak.c ../src/sim.c ../src/air.c ../src/catalog.c -lm
clean:
	rm -f adc_host soak
