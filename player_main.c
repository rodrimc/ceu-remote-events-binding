#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>
#include <glib.h>
#include <gio/gio.h>
#include <stdio.h>
#include <inttypes.h>
#include "env_util.h"

static GMainLoop *main_loop;
static GstElement *pipeline;
static GstElement *sink;

typedef struct _pair_int_t
{
  int first;
  int second;
} pair_int_t;

void
input_evt_handler (char **evt, int size)
{
#ifdef CEU_IN_SESSION_CREATED
  if (strcmp (evt[0], "SESSION_CREATED") == 0)
  {
    char *endptr;
    int value;
    value = (int) strtol (evt[1], &endptr, 10);
    if (evt[1] != endptr)
    {
      ceu_sys_go (&app, CEU_IN_SESSION_CREATED, &value);
    }
  }
#endif
#ifdef CEU_IN_DEVICE_JOINED
  if (strcmp (evt[0], "DEVICE_JOINED") == 0)
  {
    char *endptr;
    pair_int_t pair;
    pair.first = (int) strtol (evt[1], &endptr, 10);
    if (evt[1] != endptr)
    {
      pair.second = (int) strtol (evt[2], &endptr, 10);
      if (evt[2] != endptr)
        ceu_sys_go (&app, CEU_IN_DEVICE_JOINED, &pair);
    }
  }
#endif
}

static gboolean
bus_cb (GstBus *bus, GstMessage *msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;
  switch (GST_MESSAGE_TYPE(msg)) 
  {
    case GST_MESSAGE_EOS:
    {
      printf ("End of stream\n");
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      char *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);

      fprintf (stderr, "Error (%s): %s\n", GST_OBJECT_NAME(msg->src), 
          error->message);
      fprintf (stderr, "Debugging info: %s\n", debug ? debug : "none");

      g_error_free (error);
      g_free (debug);
      
      g_main_loop_quit (loop);
      break;
    }
  }
}

static void
cb_pad_added (GstElement *dec, GstPad *pad, gpointer data)
{
  GstCaps *caps;
  GstStructure *str;
  const gchar *name;
  GstPadTemplate *templ;
  GstElementClass *klass;

  /* check media type */
  caps = gst_pad_query_caps (pad, NULL);
  str = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (str);

  klass = GST_ELEMENT_GET_CLASS (sink);

  if (g_str_has_prefix (name, "audio")) 
    templ = gst_element_class_get_pad_template (klass, "audio_sink");
  else if (g_str_has_prefix (name, "video")) 
    templ = gst_element_class_get_pad_template (klass, "video_sink");
  else if (g_str_has_prefix (name, "text")) 
    templ = gst_element_class_get_pad_template (klass, "text_sink");
  else 
    templ = NULL;

  if (templ) 
  {
    GstPad *sinkpad;

    sinkpad = gst_element_request_pad (sink, templ, NULL, NULL);

    if (!gst_pad_is_linked (sinkpad))
      gst_pad_link (pad, sinkpad);

    gst_object_unref (sinkpad);
  }
}

void
stop ()
{
  if (pipeline != NULL)
  {
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_main_loop_quit (main_loop);
  }
}

void
play ()
{
  if (pipeline != NULL)
  {
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
  }
}

void
seek (int64_t pos)
{
  /*TODO: */
}

int64_t
get_pos ()
{
  int64_t pos;
  if (pipeline == NULL || 
      !gst_element_query_position (pipeline, GST_FORMAT_TIME, &pos))
  {
    pos = 0;
  }
  return pos;
}

static gboolean 
keyboard_input_callback (GIOChannel *io, GIOCondition condition, gpointer data)
{
  gchar key;
  GError *error = NULL;

  switch (g_io_channel_read_chars (io, &key, sizeof(key), NULL, &error))
  {
    case G_IO_STATUS_NORMAL:
      if (key == 'f')
      {
        /* seek */ 
      }
      break;
    case G_IO_STATUS_ERROR: 
      printf ("Error: %s\n", error->message);
      break;
  }
  return TRUE;
}


int 
main(int argc, char *argv[])
{
  GstElement *decoder;
  GstBus *bus;
  GIOChannel *in_channel;

  if (argc <= 2)
  {
    printf ("Usage: %s <uri>\n", argv[0]);
    return -1;
  }
  
  gst_init (&argc, &argv);
 
  main_loop = g_main_loop_new (NULL, FALSE);
  in_channel = g_io_channel_unix_new (STDIN_FILENO);
  g_io_add_watch (in_channel, G_IO_IN, keyboard_input_callback, NULL); 
  
  pipeline = gst_pipeline_new ("pipeline");
  decoder = gst_element_factory_make ("uridecodebin", "decoder");
  sink = gst_element_factory_make ("playsink", "sink");

  g_object_set (G_OBJECT (decoder), "uri", argv[1], NULL);
  g_signal_connect (G_OBJECT(decoder), "pad-added", 
      G_CALLBACK (cb_pad_added), NULL);
  
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, bus_cb, main_loop);
  gst_object_unref (bus);

  gst_bin_add_many (GST_BIN(pipeline), decoder, sink, NULL);

  env_set_timeout (1, main_loop);
  
  if (env_bootstrap (argc, argv) != 0)
  {
    printf ("Error. Exiting...\n");
    return 1;
  }

  /*gst_element_set_state (pipeline, GST_STATE_PLAYING);*/
  g_main_loop_run (main_loop);

  g_io_channel_unref (in_channel);
  if (GST_STATE (pipeline) != GST_STATE_NULL)
    gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT(pipeline));
  
  return 0;
}
