#include <string.h>
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
parse_message (lua_State *L, char *buff, char **evt)
{
  const char *prefix = "msg = ";
  size_t len = strlen (prefix);
  int index;

  memmove (buff + len, buff, strlen (buff));
  strncpy (buff, prefix, len);

  if (luaL_loadstring (L, buff) != 0 || lua_pcall(L, 0, 0, 0) != 0)
  {
    _log (lua_tostring (L, -1));
    lua_pop (L, 1);
    return 1;
  }
  lua_getglobal(L, "msg");

  lua_pushstring (L, "evt");
  lua_gettable (L, -2);
  
#if 0
  index = bytes_read + len < BUFF_SIZE ?
    bytes_read + len : BUFF_SIZE - 1;

  buff[bytes_read + len] = '\0';
  _log (buff);
#endif
  *evt = strdup (lua_tostring (L, -1));
  lua_pop (L, 2);

  return 0;
}

char *
serialize (lua_State *L, const char *evt)
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

