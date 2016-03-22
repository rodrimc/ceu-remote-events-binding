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
void seek (int64_t);
int64_t get_pos ();

#define ceu_out_emit_REQUEST_SESSION(arg)                             \
  env_output_evt_handler("REQUEST_SESSION", NULL)      

#define ceu_out_emit_REQUEST_JOIN(arg)                                \
  char *buff1 = env_int_to_char (arg->_1);                            \
  char *buff2 = env_int_to_char (arg->_2);                            \
  env_output_evt_handler("REQUEST_JOIN", buff1, buff2,                \
      NULL);                                                          \
  _free (buff1);                                                      \
  _free (buff2)

#define ceu_out_emit_PLAY(arg)                                        \
  char *session = env_int_to_char (arg->_1);                          \
  char *id = env_int_to_char (arg->_2);                               \
  char *time = env_int_to_char (arg->_4);                             \
  play();                                                             \
  env_output_evt_handler("PLAY", session, id, arg->_3, time, NULL);   \
  _free (session);                                                    \
  _free (id);                                                         \
  _free (time)                                                       

#define ceu_out_emit_STOP(arg)                                        \
  char *buff = env_int_to_char (arg->_1);                             \
  stop();                                                             \
  env_output_evt_handler("STOP", buff, arg->_2, NULL);                \
  _free (buff)

#define ceu_out_emit_SEND_POS(arg)                                    \
  printf ("Pos: %" PRId64 "\n", arg->_1);                             \
  env_output_evt_handler("SEND_POS", NULL)                            \

#endif

#ifdef APP_MAESTRO
/* Handlers and functions for the maestro example */
#define ceu_out_emit_SESSION_CREATED(arg)                             \
  char *buff = env_int_to_char (arg->_1);                             \
  env_output_evt_handler("SESSION_CREATED", buff, NULL);              \
  _free (buff);

#define ceu_out_emit_JOINED(arg)                                      \
  char *buff = env_int_to_char (arg->_1);                             \
  char *buff2 = env_int_to_char (arg->_2);                            \
  env_output_evt_handler("JOINED", buff, buff2, NULL);                \
  _free (buff);                                                       \
  _free (buff2);

#define ceu_out_emit_MEDIA_STARTED(arg)                               \
  char *s = env_int_to_char (arg->_1);                                \
  char *id = env_int_to_char (arg->_2);                               \
  char *t = env_int_to_char (arg->_4);                                \
  env_output_evt_handler("MEDIA_STARTED", s, id, arg->_3, t, NULL);   \
  _free (s);                                                          \
  _free (id);                                                         \
  _free (t)                                                       

#endif

#endif /* USER_EVENTS */
