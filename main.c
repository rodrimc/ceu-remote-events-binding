#include <stdio.h>
#include <stdlib.h>
#include <gio/gio.h>  
#include "env_util.h"
#include "env_events.h"


#define TIME_FREQ 1 /*frequency in which the update_ceu_time callback 
                      is called (ms) */
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
      value = (int) strtol (evt[1], &endptr, 10);
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

void 
handle_key_event (const char *evt, int value)
{
  char buff[5];
  sprintf (buff, "%d", value);
  env_output_evt_handler(evt, &buff[0], NULL);
}

static gboolean 
keyboard_input_callback (GIOChannel *io, GIOCondition condition, gpointer data)
{
  gchar key;
  GError *error = NULL;

  switch (g_io_channel_read_chars (io, &key, sizeof(key), NULL, &error))
  {
#ifdef CEU_IN_KEY_INPUT
    case G_IO_STATUS_NORMAL:
      ceu_sys_go (&app, CEU_IN_KEY_INPUT, &key);
      break;
#endif    
    case G_IO_STATUS_ERROR: 
      LOG (error->message);
      break;
  }
  return TRUE;
}

int 
main (int argc, char* argv[])
{
  GMainLoop *main_loop;
  GIOChannel *in_channel;

  if (env_bootstrap (argc, argv) != 0)
  {
    printf ("Error. Exiting...\n");
    return 1;
  }

  main_loop = g_main_loop_new (NULL, FALSE);
  in_channel = g_io_channel_unix_new (STDIN_FILENO);
  g_io_add_watch (in_channel, G_IO_IN, keyboard_input_callback, NULL); 

  env_set_timeout (TIME_FREQ, main_loop);

  g_main_loop_run (main_loop);

  env_finalize ();
  g_io_channel_unref (in_channel);
  printf ("All done\n");
  return app.ret;
}
