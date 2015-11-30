#include "env_events.h"
#include "env_util.h"
#include "_ceu_app.h"

extern tceu_app app;

void
input_evt_handler (const char *evt /*, const char *arg1, ...*/)
{
#ifdef CEU_IN_IN_1
  if (strcmp (evt, "IN_1") == 0)
  {
    ceu_sys_go (&app, CEU_IN_IN_1, NULL);
  }
#endif
}
