#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <gio/gio.h>  
#include "env_util.h"


#define TIME_FREQ 1 /*frequency in which the update_ceu_time callback 
                      is called (ms) */
#define DEFAULT_PORT 8888
#define MAX_THREADS 5
#define BUFF_SIZE 128


/* Begin of global variables declaration */
static GMainLoop *main_loop;
static GSocketService *service;
static uint16_t port;
static GHashTable *connections;     /* Connections table */
static GHashTable *evt_bindings;    /* Event mappings table */
static lua_State *L;
static char *routes_file;
static GOptionEntry entries [] = 
 {
   { "routes", 'r', 0, G_OPTION_ARG_FILENAME, &routes_file, "File that "
     "specifies the static route table", "<file>" },
   { NULL }
 };

/* End */

/*Begin of forward declarations */
static void evt_bind_async_connection_done (GObject *, GAsyncResult *, 
    gpointer);
static void bind (const char *, const char *, const char *);
static gboolean incoming_callback (GSocketService *, GSocketConnection *,
    GObject *, gpointer);
static gboolean update_ceu_time (gpointer);
static gboolean setup_service (void);
static void send_done (GObject *, GAsyncResult *, gpointer);
static gboolean input_callback (GIOChannel *, GIOCondition, gpointer);
static void load_route_table ();
/* End */

/* Begin of ceu_out_emit_* */
#define ceu_out_emit_EVT_BIND(arg) bind (arg->_1, arg->_2, arg->_3)

#define ceu_out_emit_CONNECT(arg)                                     \
  GSocketClient *socket = g_socket_client_new ();                     \
  g_socket_client_connect_to_host_async (socket, arg->_1,             \
      port == DEFAULT_PORT ? port + 1 :                   \
      port - 1, NULL, evt_bind_async_connection_done, arg->_1); \
/* end  */

static void
free_evt_bindings ()
{
  GHashTableIter it;
  gpointer key;
  gpointer value;

  g_hash_table_iter_init (&it, evt_bindings);
  while (g_hash_table_iter_next (&it, &key, &value))
  {
    GSList *l;

    l = (GSList *) value;
    while (l)
    {
      evt_bind *bind = (evt_bind *) l->data;

      g_free (bind->out_evt);
      g_free (bind->in_evt);
      g_free (bind->address);

      l = l->next;
    }

    g_free (key);
  }
}

static void 
bind (const char *out_evt, const char *in_evt, const char *address)
{
  evt_bind *bind; 

  bind = (evt_bind *) malloc (sizeof (evt_bind));

  bind->out_evt = strdup (out_evt);
  bind->in_evt = strdup (in_evt);
  bind->address = strdup (address);
  
  g_hash_table_insert (evt_bindings, bind->out_evt, 
      g_slist_append (g_hash_table_lookup(evt_bindings, bind->out_evt), bind));

}

static void
send_done (GObject *obj, GAsyncResult *result, gpointer data)
{
#ifdef CEU_IN_EVT_DELIVERED
  evt_bind *bind;
  GError *error = NULL;
  gssize bytes_written = 0;
  
  bind = (evt_bind *) data;
  bytes_written = g_output_stream_write_finish(G_OUTPUT_STREAM(obj), result,
      &error);
  printf ("bytes written: %ld\n", bytes_written);
  if (error != 0)
    ceu_sys_go (&app, CEU_IN_EVT_DELIVERED, &bytes_written);

#endif
}


void
env_output_evt_handler (const char *event)
{
  GSList *list = NULL;
  list = g_hash_table_lookup (evt_bindings, event);

  while (list != NULL)
  {
    GSocketConnection *conn = NULL;
    evt_bind *bind = (evt_bind *) list->data;
   
    conn = g_hash_table_lookup (connections, bind->address);
    if (conn != NULL)
    {
      char *buff;
      GOutputStream *out_channel;

      buff = serialize (L, bind->in_evt);
      if (buff == NULL)
        return;
 
      out_channel = g_io_stream_get_output_stream (G_IO_STREAM(conn));
      g_output_stream_write_async (out_channel, buff, strlen (buff), 
          G_PRIORITY_DEFAULT, NULL, send_done, bind);

      free (buff);
    }
    list = list->next;
  }

}

#include "env_events.h"
#include "_ceu_app.c"

static uint64_t old_dt;
static char CEU_DATA[sizeof(CEU_Main)];
tceu_app app;

static void
evt_bind_async_connection_done (GObject *obj,
    GAsyncResult *result, gpointer data)
{
  evt_bind *bind;
  GError *error = NULL;
  GSocketConnection *conn;
  CONNECTION_STATUS status;
  char log_buff[64];
  
  conn = g_socket_client_connect_finish ((GSocketClient *)obj, result, &error);
  if (conn != NULL)
  {
    const char *address = (char *) data;
  
    status = CONNECTED;
    g_hash_table_insert (connections, strdup (address), conn);
    sprintf (log_buff, "Connected to %s", address);

    LOG (log_buff);
  }
  else
  {
    /*TODO: parse GError and set the status to the appropriate value 
     *  (see gio/gioenums.h) */
    if (error->code == 39) /*Connection refused */
      status = CONNECTION_REFUSED;
    else
      status = DISCONNECTED;
    sprintf (log_buff, "Error code: %d", error->code);

    LOG (error->message);
    LOG (log_buff);
  }
#ifdef CEU_IN_CONNECT_DONE
  ceu_sys_go (&app, CEU_IN_CONNECT_DONE, &status);
#endif
 
  g_object_unref (obj);
}

static gboolean
incoming_callback (GSocketService *service, GSocketConnection *connection,
    GObject *source, gpointer user_data)
{
  GInputStream *input_stream = NULL;

  LOG ("New connection");
  input_stream = g_io_stream_get_input_stream (G_IO_STREAM (connection));

  while (g_socket_connection_is_connected (connection))
  {
    GError *error = NULL;
    char buff [BUFF_SIZE];
    gssize bytes_read;

    bytes_read = g_input_stream_read (input_stream, &buff, BUFF_SIZE, NULL, 
        &error);

    if (bytes_read > 0)
    {
      char msg[64];
      char *evt;

      LOG (buff);

      sprintf (msg, "%ld bytes read", bytes_read);
      buff[bytes_read] = '\0';
      LOG (msg);

      if (parse_message (L, buff, &evt) == 0)
      {
        sprintf (msg, "Event received: %s", evt);
        LOG (msg);
        input_evt_handler (evt);
      }
      else
      {
        LOG ("Error while parsing the message");
      }

    }
    else /* Error or EOF  */
    {
      if (error != NULL)
      {
        LOG (error->message);
      } 

      break;
    }
  }

  LOG ("Connection closed");
  
  return FALSE;
}

static gboolean
update_ceu_time (gpointer data)
{
  if (app.isAlive)
  {
    uint64_t dt = g_get_monotonic_time () - old_dt;
#ifdef CEU_WCLOCKS
    ceu_sys_go(&app, CEU_IN__WCLOCK, &dt);
#endif
#ifdef CEU_ASYNCS
    ceu_sys_go(&app, CEU_IN__ASYNC, NULL);
#endif
    old_dt += dt;

    return G_SOURCE_CONTINUE;
  }
  else
  {
    g_main_loop_quit (main_loop);
    return G_SOURCE_REMOVE;
  }
}

static gboolean
setup_service (void)
{
  int max_attempts;
  char log_msg[32];

  max_attempts = 5;

  gboolean success = FALSE;
  service = g_threaded_socket_service_new (MAX_THREADS); 

  port = DEFAULT_PORT;
  do
  {
    GError *error = NULL;
    success = g_socket_listener_add_inet_port ((GSocketListener*) service, 
        port, NULL, &error);

    if (!success)
    {
      char debug[64];
      sprintf (debug, "Cannot listen on port %d. Trying %d\n", port, port + 1);
      LOG (debug);
      port += 1;
    } 

  } while (success == FALSE && max_attempts--);

  if (max_attempts == 0)
    return FALSE;

  g_signal_connect (service, "run", G_CALLBACK (incoming_callback), NULL);
  g_socket_service_start (service);

  sprintf (log_msg, "Listening on port %d\n", port);
  LOG (log_msg);

  return TRUE;
}

static gboolean 
input_callback (GIOChannel *io, GIOCondition condition, gpointer data)
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

static void 
load_route_table ()
{
  int index;
  if (luaL_loadfile (L, routes_file) != 0 || lua_pcall(L, 0, 1, 0) != 0)
  {
    printf ("%s", lua_tostring (L, -1));
    lua_pop (L, 1);
  }
  lua_pushnil (L);
  while (lua_next (L, 1) != 0)
  {
    luaL_checktype (L, -1, LUA_TTABLE);
    int n_keys = luaL_len (L, -1);
    if (n_keys == 3)
    {
      const char *in_evt;
      const char *out_evt;
      const char *address;
      
      lua_rawgeti (L, 3, 1);
      lua_rawgeti (L, 3, 2);
      lua_rawgeti (L, 3, 3);
      
      in_evt = lua_tostring (L, -3);
      out_evt = lua_tostring (L, -2);
      address = lua_tostring (L, -1);
     
      bind (in_evt, out_evt, address);
      
      lua_pop (L, 3);
    }
    lua_pop (L, 1);
  }
  lua_pop (L, 1);
}

int 
main (int argc, char* argv[])
{
  GIOChannel *in_channel;
  GOptionContext *context;
  GError *error = NULL;

  LOG ("Main thread");

  if (setup_service () == FALSE)
  {
    printf ("Could not setup the service.\nExiting...");
    return 0;
  }

  evt_bindings = g_hash_table_new (g_str_hash, g_str_equal);
  connections = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);

  L = luaL_newstate ();
  luaL_openlibs (L);

  app.data = (tceu_org*) &CEU_DATA;
  app.init = &ceu_app_init;
  app.init (&app);
  
  context = g_option_context_new (NULL);
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
  {
    printf ("Option parsing failed: %s\n", error->message);
    return 1;
  }
  if (routes_file != NULL)
    load_route_table();

  main_loop = g_main_loop_new (NULL, FALSE);
  in_channel = g_io_channel_unix_new (STDIN_FILENO);
  g_io_add_watch (in_channel, G_IO_IN, input_callback, NULL); 

  old_dt = g_get_monotonic_time ();

  g_timeout_add (TIME_FREQ, update_ceu_time, NULL);

  g_main_loop_run (main_loop);
  g_main_loop_unref (main_loop);
  g_io_channel_unref (in_channel);

  free_evt_bindings ();
  g_hash_table_destroy (evt_bindings);
  g_hash_table_destroy (connections);
  printf ("All done\n");
  return app.ret;
}
