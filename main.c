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
   { "port", 'p', 0, G_OPTION_ARG_INT, &port, "Port to listen for incoming "
     "connections. Default=8888", "PORT" },
   { NULL }
 };
/* End */

/*Begin of forward declarations */
static void evt_bind_async_connection_done (GObject *, GAsyncResult *, 
    gpointer);
static void bind (const char *, const char *, const char *);
static gboolean connection_handler (GSocketService *, GSocketConnection *,
    GObject *, gpointer);
static gboolean update_ceu_time (gpointer);
static gboolean setup_service (void);
static void send_done (GObject *, GAsyncResult *, gpointer);
static gboolean keyboard_input_callback (GIOChannel *, GIOCondition, gpointer);
static void load_route_table (void);
static void destroy_conn_data (gpointer);
/* End */

/* Begin of ceu_out_emit_* */
#define ceu_out_emit_EVT_BIND(arg) bind (arg->_1, arg->_2, arg->_3)
/* end  */

static void
destroy_conn_data (gpointer _data)
{
  conn_data *data = (conn_data *) _data;
  if (data->conn != NULL)
    g_object_unref (G_OBJECT(data->conn));
  msg_service_destroy (data->service);
  free (data);
}

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
send_done (GObject *obj, GAsyncResult *result, gpointer _data)
{
  conn_data *data;
  GError *error = NULL;
  gssize bytes_written = 0;
  
  data = (conn_data *) _data;
  bytes_written = g_output_stream_write_finish(G_OUTPUT_STREAM(obj), result,
      &error);
  printf ("bytes written: %ld\n", bytes_written);
  if (error == 0)
  {
#ifdef CEU_IN_EVT_DELIVERED
    ceu_sys_go (&app, CEU_IN_EVT_DELIVERED, &bytes_written);
#endif
  }
  else
  {
    printf ("ERROR: %s\n", error->message);
  }
  if (msg_service_has_pending_message (data->service))
    msg_service_send (data->service, data->conn);
}


void
env_output_evt_handler (const char *event, ...)
{
  GSList *list = NULL;
  GString *tmp_string;
  const char* arg;
  char *encoded_args;
  char *serialized_args;
  va_list args;

  tmp_string = g_string_new (NULL);
  va_start (args, event);
  while ((arg = va_arg (args, const char*)) != NULL)
  {
    g_string_append (tmp_string, arg);
    g_string_append (tmp_string, ARGS_DELIMITER);
  }
  va_end (args);
  
  encoded_args = g_string_free (tmp_string, FALSE);
  list = g_hash_table_lookup (evt_bindings, event);

  while (list != NULL)
  {
    conn_data *data = NULL;
    GSocketConnection *conn = NULL;
    evt_bind *bind = (evt_bind *) list->data;
   
    data = g_hash_table_lookup (connections, bind->address);
    if (data == NULL)
    {
      msg_service *service = msg_service_new (); 
      data = g_new0 (conn_data, 1);
      data->service = service;
      data->address = bind->address; /* Don't need to free data->address */
     
      msg_service_register_send_callback (service, send_done);
      g_hash_table_insert (connections, strdup (bind->address), data);
    }

    conn = data->conn;

    serialized_args = serialize (L, bind->in_evt, encoded_args);
    msg_service_push (data->service, serialized_args, data); 
    free (serialized_args);

    if (conn != NULL && g_socket_connection_is_connected(conn) == TRUE)
    {
      msg_service_send (data->service, conn);
    }
    else
    {
      GSocketClient *socket = g_socket_client_new (); 
      g_socket_client_connect_to_host_async (socket, bind->address,
          port == DEFAULT_PORT ? port + 1 :             
          port - 1, NULL, evt_bind_async_connection_done, 
          data);
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
evt_bind_async_connection_done (GObject *obj, GAsyncResult *result, 
    gpointer _data)
{
  evt_bind *bind;
  conn_data *data;
  GError *error = NULL;
  GSocketConnection *conn;
  CONNECTION_STATUS status;
  char log_buff[64];
  
  data = (conn_data *) _data;
  conn = g_socket_client_connect_finish ((GSocketClient *)obj, result, &error);
  if (data->conn != NULL)
    g_object_unref (G_OBJECT(data->conn));

  data->conn = conn;
  
  if (conn != NULL)
  {
    status = CONNECTED;
    /* g_hash_table_insert (connections, strdup (data->address), conn); */
    sprintf (log_buff, "Connected to %s", data->address);

    msg_service_send (data->service, data->conn);

    LOG (log_buff);
  }
  else
  {
    GSocketClient *socket = g_socket_client_new (); 
    /*TODO: parse GError and set the status to the appropriate value 
     *  (see gio/gioenums.h) */
    /* if (error->code == 39) /*Connection refused *1/ */
    /*   status = CONNECTION_REFUSED; */
    /* else */
    /*   status = DISCONNECTED; */
    /* sprintf (log_buff, "Error code: %d", error->code); */

    LOG (error->message);
    LOG (log_buff);
    g_socket_client_connect_to_host_async (socket, data->address,
        port == DEFAULT_PORT ? port + 1 :             
        port - 1, NULL, evt_bind_async_connection_done, 
        data);
  }
#ifdef CEU_IN_CONNECT_DONE
  ceu_sys_go (&app, CEU_IN_CONNECT_DONE, &status);
#endif
 
  g_object_unref (obj);
}

static gboolean
connection_handler (GSocketService *service, GSocketConnection *connection,
    GObject *source, gpointer user_data)
{
  GInputStream *input_stream = NULL;
  msg_service *msg_handler = msg_service_new ();

  LOG ("New connection");
  input_stream = g_io_stream_get_input_stream (G_IO_STREAM (connection));

  while (g_socket_connection_is_connected (connection))
  {
    GError *error = NULL;
    char buff [BUFF_SIZE + 1];
    gssize bytes_read;

    memset (buff, 0, sizeof (char) * (BUFF_SIZE + 1));
    /* bytes_read = g_input_stream_read (input_stream, &buff, BUFF_SIZE, NULL, */ 
    /*     &error); */
    bytes_read = msg_service_receive (msg_handler, connection, &buff[0], 
        BUFF_SIZE);

    if (bytes_read > 0)
    {
      int size;
      char msg[64];
      char **evt;

      LOG (buff);

      sprintf (msg, "%ld bytes read", bytes_read);
      LOG (msg);

      if (parse_message (L, buff, &evt, &size) == 0)
      {
        int i;
        sprintf (msg, "Event received: %s", evt[0]);
        LOG (msg);
        input_evt_handler (evt, size);
        for (i = 0; i < size; i++)
          free (evt[i]);
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

  msg_service_destroy (msg_handler);
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
  GError *error = NULL;
  int max_attempts;
  char log_msg[32];
  gboolean success = FALSE;
  
  service = g_threaded_socket_service_new (MAX_THREADS); 
    
  success = g_socket_listener_add_inet_port ((GSocketListener*) service, 
      port, NULL, &error);

  if (!success)
  {
    char debug[64];
    sprintf (debug, "Cannot listen on port %d.\n", port);
    LOG (debug);
  } 
  else
  {
    g_signal_connect (service, "run", G_CALLBACK (connection_handler), NULL);
    g_socket_service_start (service);

    sprintf (log_msg, "Listening on port %d\n", port);
    LOG (log_msg);
  }
  return success;
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

  context = g_option_context_new (NULL);
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
  {
    printf ("Option parsing failed: %s\n", error->message);
    return 1;
  }

  port = (port == 0 ) ? DEFAULT_PORT : port;
  
  if (setup_service () == FALSE)
  {
    printf ("Could not setup the service.\nExiting...\n");
    return 0;
  }

  evt_bindings = g_hash_table_new (g_str_hash, g_str_equal);
  connections = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, destroy_conn_data);

  L = luaL_newstate ();
  luaL_openlibs (L);

  app.data = (tceu_org*) &CEU_DATA;
  app.init = &ceu_app_init;
  app.init (&app);
  
  if (routes_file != NULL)
    load_route_table();

  main_loop = g_main_loop_new (NULL, FALSE);
  in_channel = g_io_channel_unix_new (STDIN_FILENO);
  g_io_add_watch (in_channel, G_IO_IN, keyboard_input_callback, NULL); 

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
