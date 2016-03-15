#ifndef USER_EVENTS
#define USER_EVENTS

/**
 * In this file one should define macros that handle
 * application-specific CÉU output events.
 */

#include <inttypes.h>
#include <string.h>

#ifdef APP_BULB

/* Handlers for the BULB example */
#define ceu_out_emit_OUT_1(arg) \
  env_output_evt_handler("OUT_1", NULL)

#define ceu_out_emit_KEY_UP(arg) \
  handle_key_event ("KEY_UP", arg->_1)

#define ceu_out_emit_KEY_DOWN(arg) \
  handle_key_event ("KEY_DOWN", arg->_1)

void handle_key_event (const char *, int);

#endif

#ifdef APP_PLAYER
/* Handlers and functions for the player example */
void play ();
void stop ();
int64_t get_pos ();

#define ceu_out_emit_PLAY()                   \
  play();                                     \
  env_output_evt_handler("PLAY", NULL)        

#define ceu_out_emit_STOP()                   \
  stop();                                     \
  env_output_evt_handler("STOP", NULL)        

#define ceu_out_emit_SEND_POS(arg)            \
  printf ("Pos: %" PRId64 "\n", arg->_1);     \
  env_output_evt_handler("SEND_POS", NULL)    \
/* arg->_1 should be passed as const char * */

#endif

#endif /* USER_EVENTS */
