#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>
#include <inttypes.h>
#include "env_util.h"

static GMainLoop *main_loop;

typedef struct _pair_int_t
{
  int first;
  int second;
} pair_int_t;

typedef struct _pair_int_str_t
{
  int first;
  char *second;
} pair_int_str_t;

typedef struct __4tuple_t
{
  int first;
  int second;
  char *third;
  int64_t fourth;
} _4tuple_t;

void
input_evt_handler (char **evt, int size)
{
  (void)size;
#ifdef CEU_IN_CREATE_SESSION
  if (strcmp (evt[0], "CREATE_SESSION") == 0)
  {
    ceu_sys_go (&app, CEU_IN_CREATE_SESSION, NULL);
  }
#endif
#ifdef CEU_IN_JOIN
  if (strcmp (evt[0], "JOIN") == 0)
  {
    pair_int_t pair;
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
#ifdef CEU_IN_MEDIA_BEGIN
  if (strcmp (evt[0], "MEDIA_BEGIN") == 0)
  {
    _4tuple_t pair;
    char *endptr;

    pair.first = (int) strtol (evt[1], &endptr, 10);
    if (evt[1] != endptr)
    {
      pair.second = (int) strtol (evt[2], &endptr, 10);
      if (evt[2] != endptr)
      {
        pair.third = evt[3];
        if (sscanf (evt[4], "%" SCNd64 "", &pair.fourth) != EOF)
        {
          ceu_sys_go (&app, CEU_IN_MEDIA_BEGIN, &pair);
        }
      }
    }
  }
#endif
#ifdef CEU_IN_MEDIA_END
  if (strcmp (evt[0], "MEDIA_END") == 0)
  {
    pair_int_str_t pair;
    char *endptr;

    pair.first = (int) strtol (evt[1], &endptr, 10);
    if (evt[1] != endptr)
    {
      pair.second = evt[2];
      ceu_sys_go (&app, CEU_IN_MEDIA_END, &pair);
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
