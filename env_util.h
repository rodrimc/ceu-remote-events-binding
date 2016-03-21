#ifndef ENV_UTIL
#define ENV_UTIL
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <glib.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include "env_msg_service.h"

#ifdef ENV_DEBUG
#define LOG(m) printf("(%p) LOG: %s\n", g_thread_self(), m) 
#else
#define LOG(m)
#endif

#define LOCK(m) g_mutex_lock(m)
#define UNLOCK(m) g_mutex_unlock(m)

#define BUFF_SIZE 128
#define ARGS_DELIMITER "*"

#define _free(p) g_free(p); p=NULL;

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

typedef struct _conn_data
{
  GSocketConnection *conn;
  msg_service *service;
  const char *address;
  gboolean has_pending;
  
  /* Maybe this buffer could be in other struct */
  char input_buff[BUFF_SIZE + 1]; 
} conn_data;

void 
ceu_sys_assert (int);

void 
ceu_sys_log (int, long);

void 
stackDump (lua_State *L);

int 
parse_message (lua_State *, char *, char ***, int *);

char *
serialize (const char *, const char *);

int
env_bootstrap (int, char **);

void
env_set_timeout (int, GMainLoop *);

void
env_finalize ();

char *
env_int_to_char (int64_t);

const char *
env_get_class ();

int
env_get_id ();

int64_t
env_get_current_time ();

#ifndef ceu_out_assert
  #define ceu_out_assert(v) ceu_sys_assert(v)
#endif

#ifndef ceu_out_log
  #define ceu_out_log(m,s) ceu_sys_log(m,s)
#endif

#include "_ceu_app.h"
extern tceu_app app;

#endif /* ENV_UTIL */
