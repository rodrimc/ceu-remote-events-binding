#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>
#include <inttypes.h>
#include "env_util.h"

static GMainLoop *main_loop;

void
input_evt_handler (char **evt, int size)
{
#ifdef CEU_IN_CREATE_SESSION
  if (strcmp (evt[0], "CREATE_SESSION") == 0)
  {
    ceu_sys_go (&app, CEU_IN_CREATE_SESSION, NULL);
  }
#endif
#ifdef CEU_IN_JOIN
  if (strcmp (evt[0], "JOIN") == 0)
  {
    pair_t pair;
    char *endptr;
    
    pair.first = (int) strtol (evt[1], &endptr, 10);
    if (evt[1] != endptr)
    {
      pair.second = (int) strtol (evt[2], &endptr, 10);
      if (evt[2] != endptr)
        ceu_sys_go (&app, CEU_IN_JOIN, &pair);
    }
  }
#endif
}

int 
main(int argc, char *argv[])
{
  main_loop = g_main_loop_new (NULL, FALSE);
  
  env_set_timeout (1, main_loop);
  
  if (env_bootstrap (argc, argv) != 0)
  {
    printf ("Error. Exiting...\n");
    return 1;
  }

  g_main_loop_run (main_loop);

  env_finalize ();
  return 0;
}
