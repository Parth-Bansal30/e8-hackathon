UI := ../../firmware/soundsense/ui
SRC := ui_sim.c $(UI)/gfx.c $(UI)/ui.c $(UI)/ui_led.c $(UI)/ui_fonts.c
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -I$(UI)

ui_sim: $(SRC) $(UI)/*.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lz -lm

run: ui_sim
	mkdir -p out
	./ui_sim

clean:
	rm -rf ui_sim out

.PHONY: run clean
.DEFAULT_GOAL := run
