#include "env_events.h"
#include "env_util.h"
#include "_ceu_app.h"

extern tceu_app app;

void
input_evt_handler (char **evt, int size)
{
#ifdef CEU_IN_IN_1
  if (strcmp (evt[0], "IN_1") == 0)
  {
    ceu_sys_go (&app, CEU_IN_IN_1, NULL);
  }
#endif
  if (strcmp (evt[0], "BULB_INCREASE") == 0 || 
      strcmp (evt[0], "BULB_DECREASE") == 0)
  {
    if (size < 2)
      LOG ("Bad arguments");
    else
    {
      char *endptr;
      int value;
      value = (int )strtol (evt[1], &endptr, 10);
      if (evt[1] != endptr)
      {
        if (strcmp (evt[0], "BULB_INCREASE") == 0)
        {
#ifdef CEU_IN_BULB_INCREASE
        ceu_sys_go (&app, CEU_IN_BULB_INCREASE, &value);
#endif
        }
        else
        {
#ifdef CEU_IN_BULB_DECREASE
          ceu_sys_go (&app, CEU_IN_BULB_DECREASE, &value);
#endif
        }
      }
    }
  }
}

void handle_key_event (const char *evt, int value)
{
  char buff[5];
  sprintf (buff, "%d", value);
  env_output_evt_handler(evt, &buff[0], NULL);
}
