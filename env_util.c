#include <string.h>
#include <stdlib.h>
#include "env_util.h"

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
    const char *key;
    const char *value;
   
    *size = *size + 1;
    key = lua_tostring (L, -2);
    value = lua_tostring (L, -1);
    
    lua_pop (L, 2);
    lua_pushstring (L, value);
    lua_pushstring (L, key);
    lua_pushstring (L, key);
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
 
  /* for (index = 0; index < *size; index++) */
  /*   printf ("evt[%d]: %s\n", index, (*evt)[index]); */

  lua_pop (L, 1);

  return 0;
}

char *
serialize (lua_State *L, const char *evt, va_list args)
{
  const char *message = NULL;
  const char *arg;
  int i = 0;

  luaL_loadstring (L, LUA_SERIALIZE_FUNC); 
  lua_pcall(L, 0, 0, 0);
  lua_getglobal(L, "serialize");
 
  lua_newtable (L);
  lua_pushstring (L, "evt");
  lua_pushstring (L, evt);
  lua_settable (L, -3);

  while ((arg = va_arg (args, const char*)) != NULL)
  {
    char prefix[6];
    
    sprintf (prefix, "arg%d", i++);

    lua_pushstring (L, prefix);
    lua_pushstring (L, arg);
    lua_settable (L, -3);
  }
  
  if (lua_pcall (L, 1, 1, 0) != 0)
    LOG (lua_tostring (L, -1));
  else
    message = lua_tostring (L, -1);

  lua_pop (L, 1);  

  return strdup(message);
}

