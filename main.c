#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <glib.h>
#include <gio/gio.h>  
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "env_util.h"
#include "env_events.h"


#define TIME_FREQ 1 /*frequency in which the update_ceu_time callback 
                      is called (ms) */
#define DEFAULT_PORT 8888
#define MAX_THREADS 5
#define BUFF_SIZE 128

#define ceu_out_assert(v) ceu_sys_assert(v)
void ceu_sys_assert (int v) 
{
  assert(v);
}

#define ceu_out_log(m,s) ceu_sys_log(m,s)
void ceu_sys_log (int mode, long s) 
{
  switch (mode) 
  {
    case 0:
      printf("%s", (char*)s);
      break;
    case 1:
      printf("%lX", s);
      break;
    case 2:
      printf("%ld", s);
      break;
  }
}

#ifdef CEU_DEBUG
#define _log(m) printf("(%p) LOG: %s\n", g_thread_self(), m)
#else
#define _log(m)
#endif

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

/* Begin of global variables declaration */
static GMainLoop *main_loop;
static GSocketService *service;
static uint16_t port_bound;
static GHashTable *connections;     /* Connections table */
static GHashTable *evt_bindings;    /* Event mappings table */
static lua_State *L;

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
static char *serialize (const char *);

/* End */

/* Begin of ceu_out_emit_* */
#define ceu_out_emit_EVT_BIND(arg) bind (arg->_1, arg->_2, arg->_3)

#define ceu_out_emit_CONNECT(arg)                                    \
  GSocketClient *socket = g_socket_client_new ();                    \
  g_socket_client_connect_to_host_async (socket, arg->_1,            \
      port_bound == DEFAULT_PORT ? port_bound + 1 :                  \
      port_bound - 1, NULL, evt_bind_async_connection_done, arg->_1); \
/* end  */

typedef enum
{
  DISCONNECTED = 0,
  PENDING,
  CONNECTED,
  HOST_NOT_FOUND,
  CONNECTION_REFUSED
} CONNECTION_STATUS; /* TODO: extend this enum */

  typedef struct _evt_bind
{
  char *out_evt;
  char *in_evt;
  char *address;
} evt_bind;

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
  bytes_written = g_output_stream_write_finish(obj, result, &error);
  if (error != 0)
    ceu_sys_go (&app, CEU_IN_EVT_DELIVERED, &bytes_written);

#endif
}

static void stackDump (lua_State *L) 
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

static char *
serialize (const char *evt)
{
  const char *message = NULL;

  luaL_loadstring (L, LUA_SERIALIZE_FUNC); 
  lua_pcall(L, 0, 0, 0);
  lua_getglobal(L, "serialize");
 
  lua_newtable (L);
  lua_pushstring (L, "evt");
  lua_pushstring (L, evt);
  lua_settable (L, -3);

  if (lua_pcall (L, 1, 1, 0) != 0)
    _log (lua_tostring (L, -1));
  else
    message = lua_tostring (L, -1);

  lua_pop (L, 1);  

  return strdup(message);
}


void
default_evt_handler (const char *event)
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
      GOutputStream *out_channel;
      char *buff;

      buff = serialize (bind->in_evt);
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

#include "_ceu_app.c"

static uint64_t old_dt;
static char CEU_DATA[sizeof(CEU_Main)];
static tceu_app app;

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

    _log (log_buff);
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

    _log (error->message);
    _log (log_buff);
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

  _log ("New connection");
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
      char msg[16];
      const char *table = "msg = ";
      size_t len = strlen (table);
      int index;

      memmove (&buff[0] + len, &buff[0], strlen (&buff[0]));
      strncpy (&buff[0], table, len);

      luaL_loadstring (L, LUA_SERIALIZE_FUNC); 
      lua_pcall(L, 0, 0, 0);
      lua_getglobal(L, "serialize");

      sprintf (msg, "%ld bytes read", bytes_read);
      _log (msg);
     
      index = bytes_read + len < BUFF_SIZE ?
        bytes_read + len : BUFF_SIZE - 1;

      buff[bytes_read + len] = '\0';
      _log (buff);
    }
    else /* Error or EOF  */
    {
      if (error != NULL)
        _log (error->message);

      break;
    }
  }

  _log ("Connection closed");
  
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

  max_attempts = 5;

  gboolean success = FALSE;
  service = g_threaded_socket_service_new (MAX_THREADS); 

  port_bound = DEFAULT_PORT;
  do
  {
    GError *error = NULL;
    success = g_socket_listener_add_inet_port ((GSocketListener*) service, 
        port_bound, NULL, &error);

    if (!success)
    {
      printf ("Cannot listen on port %d. Trying %d\n", port_bound, port_bound + 1);
      port_bound += 1;
    } 

  } while (success == FALSE && max_attempts--);

  if (max_attempts == 0)
    return FALSE;

  g_signal_connect (service, "run", G_CALLBACK (incoming_callback), NULL);
  g_socket_service_start (service);
  printf ("Listening on port %d\n", port_bound);

  return TRUE;
}

  int 
main (int argc, char* arg[])
{
  _log ("Main thread");

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
  
  main_loop = g_main_loop_new (NULL, FALSE);

  old_dt = g_get_monotonic_time ();

  g_timeout_add (TIME_FREQ, update_ceu_time, NULL);

  g_main_loop_run (main_loop);
  
  free_evt_bindings ();
  g_hash_table_destroy (evt_bindings);
  g_hash_table_destroy (connections);
  printf ("All done\n");
  return app.ret;
}
