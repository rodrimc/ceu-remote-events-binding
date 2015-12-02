#ifndef ENV_UTIL
#define ENV_UTIL
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <glib.h>
#include <assert.h>
#include <stdio.h>

#ifdef ENV_DEBUG
#define LOG(m) assert (NULL != NULL) 
#else
#define LOG(m)
#endif

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


void 
stackDump (lua_State *L);

int 
parse_message (lua_State *, char *, char **);

char *
serialize (lua_State *, const char *);

#ifndef ceu_out_assert
  #define ceu_out_assert(v) ceu_sys_assert(v)
#endif

#ifndef ceu_out_log
  #define ceu_out_log(m,s) ceu_sys_log(m,s)
#endif

#include "_ceu_app.h"


#endif /* ENV_UTIL */
