CFLAGS = -DCEU_DEBUG `pkg-config gio-2.0 lua5.2 --cflags`
LDFLAGS = `pkg-config gio-2.0 lua5.2 --libs`

all: MAIN = main.c 
all: compile 

player: MAIN = player_main.c 							
player: CFLAGS += `pkg-config gstreamer-1.0 --cflags` 
player: LDFLAGS += `pkg-config gstreamer-1.0 --libs`
player: compile 

compile:
	ceu --cpp-args "-I ." $(CEUFILE)
	gcc -Werror -g -Os $(MAIN) env_util.c  env_events.c	env_msg_service.c \
	$(CFLAGS) $(LDFLAGS) -o $(basename $(CEUFILE))


clean:
	find . -name "*.exe"  | xargs rm -f
	find . -name "_ceu_*" | xargs rm -f

.PHONY: all clean compile
