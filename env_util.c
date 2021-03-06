#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "env_util.h"

#define DEFAULT_PORT 8888
#define MAX_THREADS 5

/* Begin of global variables declaration */
static GSocketService *service;
static uint16_t port;
static int my_id;
static GHashTable *outgoing_connections;     /* Outgoing connections table */
static GList *incoming_connections;         /* Incoming connections list */
static GMutex in_conn_mutex;
static GHashTable *evt_bindings;             /* Event mappings table */
static char *routes_file;
static const char *my_class;
static GOptionEntry entries [] = 
 {
   { "routes", 'r', 0, G_OPTION_ARG_FILENAME, &routes_file, "File that "
     "specifies the static route table", "<file>" },
   { "port", 'p', 0, G_OPTION_ARG_INT, &port, "Port to listen for incoming "
     "connections. Default=8888", "PORT" },
   { "class", 'c', 0, G_OPTION_ARG_STRING, &my_class, "Instance class. "
     "(Required)", "<class>"},
   { "id", 'i', 0, G_OPTION_ARG_INT, &my_id, "ID to identify an instance "
     "within a class. Default=0 ", "ID"},
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
static int load_route_table (void);
static void destroy_conn_data (gpointer);
static void send_messages_to_outcoming_connections (gpointer,
    gpointer, gpointer);
static void send_messages_to_incoming_connections (const char *);
static void handle_message (char *, lua_State *);
/* 
 * The following function is called whenever a device sends a message
 * addressed to ANY 
 */
static void incoming_any_message (GObject *, GAsyncResult *, gpointer); 
/* End */

/* Begin of ceu_out_emit_* */
#define ceu_out_emit_EVT_BIND(arg) bind (arg->_1, arg->_2, arg->_3)
/* end  */

void ceu_sys_assert (int v) 
{
  assert(v);
}

void ceu_sys_log (int mode, long s) 
{
  switch (mode) 
  {
    case 0:
      printf("%s", (char*)s);
      break;
    case 1:
      printf("%lx", s);
      break;
    case 2:
      printf("%ld", s);
      break;
  }
}

#define LUA_SERIALIZE_FUNC                           \
              "function serialize (o)\n"             \
                 "local result = '{\\n'\n"           \
                 "for k,v in pairs(o) do\n"          \
                   "result = result .. '  ' ..  k"   \
                    " .. ' = \"' .. v .. '\",\\n'\n" \
                 "end\n"                             \
                 "result = result .. '}\\n'\n"       \
                 "return result\n"                   \
               "end" 


void 
stackDump (lua_State *L) 
{
  int i;
  int top = lua_gettop(L);
  for (i = 1; i <= top; i++) 
  {  /* repeat for each level */
    int t = lua_type(L, i);
    switch (t) 
    {

      case LUA_TSTRING:  /* strings */
        printf("`%s'", lua_tostring(L, i));
        break;

      case LUA_TBOOLEAN:  /* booleans */
        printf(lua_toboolean(L, i) ? "true" : "false");
        break;

      case LUA_TNUMBER:  /* numbers */
        printf("%g", lua_tonumber(L, i));
        break;

      default:  /* other values */
        printf("%s", lua_typename(L, t));
        break;

    }
    printf("  ");  /* put a separator */
  }
  printf("\n");  /* end the listing */
}

int 
parse_message (lua_State *L, char *buff, char ***evt, int *size)
{
  const char *prefix = "msg = ";
  size_t len = strlen (prefix);
  int index = 0;

  *size = 0;

  memmove (buff + len, buff, strlen (buff));
  strncpy (buff, prefix, len);

  /* printf ("(%p) BUFF: %s\n", g_thread_self(), buff); */
  if (luaL_loadstring (L, buff) != 0 || lua_pcall(L, 0, 0, 0) != 0)
  {
    LOG (lua_tostring (L, -1));
    lua_pop (L, 1);
    return 1;
  }
  lua_getglobal(L, "msg");

  lua_pushnil (L);
  while (lua_next (L, 1) != 0)
  {
    char *key;
    char *value;
   
    *size = *size + 1;
    key = strdup (lua_tostring (L, -2));
    value = strdup (lua_tostring (L, -1));
    
    lua_pop (L, 2);
    lua_pushstring (L, value);
    lua_pushstring (L, key);
    lua_pushstring (L, key);

    _free (key);
    _free (value);
  }

  *evt = (char **) malloc (*size * sizeof (char *));
  while (lua_gettop (L) > 1)
  {
    const char *key;
    const char *value;
    char *end;
    int index = 0;

    key = lua_tostring (L, -1);
    value = lua_tostring (L, -2);
    
    if (strcmp (key, "evt") != 0)
    {
      index = (int) strtol (&key[3], &end, 10);
      if (&key[3] == end)
        index = -1;
      else
        index++;
    }
    if (index >= 0)
      (*evt)[index] = strdup(value);

    lua_pop (L, 2);
  }
 

  lua_pop (L, 1);

  return 0;
}

char *
serialize (const char *evt, const char *args)
{
  char *message = NULL;
  const char *token;
  char *encoded_args = NULL; 
  char *saveptr;
  int i = 0;
  lua_State *L;
  
  L = luaL_newstate ();
  luaL_openlibs (L);
  
  luaL_loadstring (L, LUA_SERIALIZE_FUNC); 
  lua_pcall(L, 0, 0, 0);
  lua_getglobal(L, "serialize");
 
  lua_newtable (L);
  lua_pushstring (L, "evt");
  lua_pushstring (L, evt);
  lua_settable (L, -3);

  encoded_args = strdup (args);
  token = strtok_r (encoded_args, ARGS_DELIMITER, &saveptr);
  while (token != NULL)
  {
    char prefix[6];
    
    sprintf (prefix, "arg%d", i++);

    lua_pushstring (L, prefix);
    lua_pushstring (L, token);
    lua_settable (L, -3);
    token = strtok_r (NULL, ARGS_DELIMITER, &saveptr);
  }
  
  _free (encoded_args);
  
  if (lua_pcall (L, 1, 1, 0) != 0)
    LOG (lua_tostring (L, -1));
  else
    message = strdup (lua_tostring (L, -1));

  lua_pop (L, 1);  

  lua_close (L);
  return message;
}

static void
destroy_conn_data (gpointer _data)
{
  conn_data *data = (conn_data *) _data;
  if (data->conn != NULL)
    g_object_unref (G_OBJECT(data->conn));
  msg_service_destroy (data->service);
  _free (data);
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

      _free (bind->out_evt);
      _free (bind->in_evt);
      _free (bind->address);
      _free (bind);
      
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
send_messages_to_incoming_connections (const char *args)
{
  GList *current;
  
  LOCK (&in_conn_mutex);
  for (current = incoming_connections;current != NULL; current = current->next)
  {
    msg_service *service = ((conn_data *)current->data)->service;
    msg_service_push (service, args, NULL);
    msg_service_send (service, ((conn_data *)current->data)->conn);
  }

  UNLOCK (&in_conn_mutex);
}

void
send_messages_to_outcoming_connections (gpointer _, 
    gpointer value, gpointer args)
{
  conn_data *data = (conn_data *) value;
  if (data->conn != NULL && 
           g_socket_connection_is_connected(data->conn) == TRUE)
  {
    msg_service_push (data->service, (char *) args, data);
    msg_service_send (data->service, data->conn);
  }
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

    serialized_args = serialize (bind->in_evt, encoded_args);
    if (strcmp (bind->address, "ANY") == 0)
    {
      send_messages_to_incoming_connections (serialized_args);
      g_hash_table_foreach (outgoing_connections,
         send_messages_to_outcoming_connections, serialized_args);
    }
    else
    {
      data = g_hash_table_lookup (outgoing_connections, bind->address);
      if (data == NULL)
      {
        msg_service *service = msg_service_new (); 
        data = g_new0 (conn_data, 1);
        data->service = service;
        data->address = bind->address; /* Don't need to free data->address */

        msg_service_register_send_callback (service, send_done);
        g_hash_table_insert (outgoing_connections, strdup (bind->address), 
            data);
      }

      conn = data->conn;

      msg_service_push (data->service, serialized_args, data); 

      _free (serialized_args);
      _free (encoded_args);

      if (conn != NULL && g_socket_connection_is_connected(conn) == TRUE)
      {
        msg_service_send (data->service, conn);
      }
      else if (data->has_pending == FALSE)
      {
        GSocketClient *socket = g_socket_client_new (); 
        g_socket_client_connect_to_host_async (socket, bind->address,
            DEFAULT_PORT, NULL, evt_bind_async_connection_done, data);
      }
    }
    list = list->next;
  }
}

void
incoming_any_message (GObject *source, GAsyncResult *result, gpointer _data)
{
  GError *error;
  GInputStream *input_stream;
  lua_State *L;
  conn_data *data = (conn_data *) _data;
  
  input_stream = g_io_stream_get_input_stream (G_IO_STREAM (data->conn));
  gssize len = g_input_stream_read_finish (input_stream, result, &error);
  
  L = luaL_newstate ();
  luaL_openlibs (L);
 
  if (len > 0)
  {
    handle_message (&data->input_buff[0], L);
  }
  else if (len == 1)
  {
    printf ("Error: %s\n", error->message);
  }
  lua_close (L);
  
  memset (data->input_buff, 0, BUFF_SIZE * sizeof (data->input_buff[0]));

  g_input_stream_read_async (input_stream, data->input_buff, BUFF_SIZE,
      G_PRIORITY_DEFAULT, NULL, incoming_any_message, data);  
}

#include "user_events.h"
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
    GInputStream *input_stream = NULL;
    status = CONNECTED;
    sprintf (log_buff, "Connected to %s", data->address);

    LOG (log_buff);
    data->has_pending = FALSE;

    input_stream = g_io_stream_get_input_stream (G_IO_STREAM (data->conn));
    g_input_stream_read_async (input_stream, data->input_buff, BUFF_SIZE,
        G_PRIORITY_DEFAULT, NULL, incoming_any_message, data);  
    
    msg_service_send (data->service, data->conn);
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
        port, NULL, evt_bind_async_connection_done, data);
    data->has_pending = TRUE;
  }
#ifdef CEU_IN_CONNECT_DONE
  ceu_sys_go (&app, CEU_IN_CONNECT_DONE, &status);
#endif
 
  g_object_unref (obj);
}

void
handle_message (char *buff, lua_State *L)
{
  int size;
  char **evt = NULL;

  LOG (buff);

  if (parse_message (L, buff, &evt, &size) == 0)
  {
    char msg[64];
    int i;
    sprintf (msg, "Event received: %s", evt[0]);
    LOG (msg);
    input_evt_handler (evt, size);
    for (i = 0; i < size; i++)
    {
      _free (evt[i]);
    }
    _free (evt);
  }
  else
  {
    LOG ("Error while parsing the message");
  }
}

static gboolean
connection_handler (GSocketService *service, GSocketConnection *connection,
    GObject *source, gpointer user_data)
{
  lua_State *L;
  conn_data *conn;
  GInputStream *input_stream = NULL;
  msg_service *msg_handler = msg_service_new ();
  
  conn = g_new0 (conn_data, 1);
  conn->service = msg_handler;
  conn->conn = connection;
  g_object_ref (connection);
  
  L = luaL_newstate ();
  luaL_openlibs (L);

  LOG ("New connection");
  input_stream = g_io_stream_get_input_stream (G_IO_STREAM (connection));
  
  LOCK (&in_conn_mutex);
  incoming_connections = g_list_prepend (incoming_connections, conn);
  UNLOCK (&in_conn_mutex);
 
  while (g_socket_connection_is_connected (connection))
  {
    GError *error = NULL;
    char buff [BUFF_SIZE + 1];
    gssize bytes_read;
    char msg[64];
    
    memset (buff, 0, sizeof (char) * (BUFF_SIZE + 1));
    bytes_read = msg_service_receive (msg_handler, connection, &buff[0], 
        BUFF_SIZE);
    
    sprintf (msg, "%ld bytes read", bytes_read);
    LOG (msg);

    if (bytes_read > 0)
    { 
      handle_message (&buff[0], L);
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

  LOCK (&in_conn_mutex);
  incoming_connections = g_list_remove (incoming_connections, conn);
  UNLOCK (&in_conn_mutex);
  
  lua_close (L);
  msg_service_destroy (msg_handler);
  g_object_unref (connection);
  g_free (conn);
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
    g_main_loop_quit ((GMainLoop *) data);
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

static int 
load_route_table ()
{
  int index;
  int code = 0;
  lua_State *L;
  
  L = luaL_newstate ();
  luaL_openlibs (L);
  
  if (luaL_loadfile (L, routes_file) != 0 || lua_pcall(L, 0, 1, 0) != 0)
  {
    printf ("Lua error: %s", lua_tostring (L, -1));
    lua_pop (L, 1);
    code = 1;
  }
  else
  {
    lua_pushstring (L, my_class);
    lua_gettable (L, -2);
    lua_remove (L, 1);
   
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
  }  
  lua_pop (L, 1);
  lua_close(L);
  return code;
}

int
env_bootstrap (int argc, char *argv[])
{
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
  
  if (my_class == NULL)
  {
    printf ("Instance class not specified.\nExiting...\n");
    return 1;
  }

  port = (port == 0) ? DEFAULT_PORT : port;
  
  if (setup_service () == FALSE)
  {
    printf ("Could not setup the service.\nExiting...\n");
    return 1;
  }

  evt_bindings = g_hash_table_new (g_str_hash, g_str_equal);
  outgoing_connections = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, destroy_conn_data);
  
  g_mutex_init (&in_conn_mutex);

  if (routes_file != NULL)
  {
    if (load_route_table() != 0)
      printf ("Error while parsing the routes file.\n");
  }
  
  app.data = (tceu_org*) &CEU_DATA;
  app.init = &ceu_app_init;
  app.init (&app);
  
  old_dt = g_get_monotonic_time ();
  
  return 0;
}

void
env_set_timeout (int freq, GMainLoop *loop)
{
  if (loop != NULL)
    g_timeout_add (freq, update_ceu_time, loop);
}

const char *
env_get_class ()
{
  return my_class;
}

int
env_get_id ()
{
  return my_id;
}

void
env_finalize ()
{
  free_evt_bindings ();
  g_hash_table_destroy (evt_bindings);
  g_hash_table_destroy (outgoing_connections);
}

char *
env_int_to_char (int64_t value)
{
  size_t size;
  char *buff;
  if (value == 0)
    size = 2;
  else
    size = (size_t) (log10 ((double)abs(value < 0 ? value * -1 : value)) + 2);

  buff = (char *) malloc (size);
  sprintf (buff, "%" PRId64  "", value);
  return buff;
}

int64_t
env_get_current_time ()
{
  return g_get_real_time ();
}
