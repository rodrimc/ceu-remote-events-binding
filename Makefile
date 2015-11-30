CFLAGS = -DCEU_DEBUG #-DCEU_DEBUG_TRAILS

all:
	ceu --cpp-args "-I ." $(CEUFILE)
	gcc -Werror -g -Os env_util.c main.c env_events.c	  $(CFLAGS) `pkg-config gio-2.0 lua5.2 --cflags --libs` \
	-o $(basename $(CEUFILE))

clean:
	find . -name "*.exe"  | xargs rm -f
	find . -name "_ceu_*" | xargs rm -f

.PHONY: all clean
