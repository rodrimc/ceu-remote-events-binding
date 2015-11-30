#ifndef ENV_EVENTS
#define ENV_EVENTS

#include <string.h>

/* The following function is defined by the environment and should be called
 * by each function that handles events issued through Céu "emit" statements */
void
env_output_evt_handler (const char * /*, void *, ... */);


/* The following function is called by the environment whenever an external 
 * event is received after being issued by another Céu script. */  
void
input_evt_handler (const char *evt /*, const char *arg1, ...*/);

#define ceu_out_emit_OUT_1(arg) \
  env_output_evt_handler("OUT_1")

#define ceu_out_emit_KEY_UP(arg)\
  env_output_evt_handler("KEY_UP")

#define ceu_out_emit_KEY_DOWN(arg)\
  env_output_evt_handler("KEY_DOWN")

#endif /* ENV_EVENTS */
