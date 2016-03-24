CFLAGS = -DCEU_DEBUG `pkg-config gio-2.0 lua5.2 --cflags`
LDFLAGS = `pkg-config gio-2.0 lua5.2 --libs` -lm

all: MAIN = main.c 
all: CFLAGS += -DAPP_BULB
all: compile 

maestro: MAIN = maestro_main.c 
maestro: CFLAGS += -DAPP_MAESTRO
maestro: compile 

player: MAIN = player_main.c 							
player: CFLAGS += -DAPP_PLAYER
player: CFLAGS += `pkg-config gstreamer-1.0 --cflags` 
player: LDFLAGS += `pkg-config gstreamer-1.0 --libs`
player: compile 

compile:
	ceu --cpp-args "-I ." $(CEUFILE)
	gcc -Werror -g -Os $(MAIN) env_util.c env_msg_service.c \
	$(CFLAGS) $(LDFLAGS) -o $(basename $(CEUFILE)).out


clean:
	find . -name "*.out"  | xargs rm -f
	find . -name "_ceu_*" | xargs rm -f

.PHONY: all clean compile
