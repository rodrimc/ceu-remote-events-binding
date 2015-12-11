#include "env_msg_service.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define LOCK(m) g_mutex_lock(m)
#define UNLOCK(m) g_mutex_unlock(m)

#define INDEXES_FOUND           0
#define OPEN_BRACE_NOT_FOUND    1
#define CLOSE_BRACE_NOT_FOUND   2
#define BAD_FORMAT_STRING       3
#define INTERNAL_ERROR          4

static int _find_indexes (const char *, int *, int *);
static char *trim (char *);

struct _msg_service
{
  GQueue *pending;
  GString *read_buffer;
  int incomplete_reading;
  GMutex mutex;
  GAsyncReadyCallback send_callback;
  gpointer user_data;
};

msg_service *
msg_service_new (void)
{
  msg_service *service = g_new0 (msg_service, 1);
  service->pending = g_queue_new ();
  service->read_buffer = g_string_new (NULL);
  service->incomplete_reading = 0;
  g_mutex_init (&service->mutex);
  return service;
}

void
msg_service_destroy (msg_service *service)
{
  if (service != NULL)
  {
    g_queue_free_full (service->pending, g_free);
    g_mutex_clear (&service->mutex);
    g_free (service);
    g_string_free (service->read_buffer, TRUE);
  }
}

void
msg_service_register_send_callback (msg_service *service, 
    GAsyncReadyCallback callback)
{
  if (service != NULL)
  {
    service->send_callback = callback;
  }
}

gboolean
msg_service_has_pending_message (msg_service *service)
{
  if (service != NULL)
    return !g_queue_is_empty(service->pending);

  return FALSE;
}
  
void
msg_service_push (msg_service *service, const char *args, gpointer user_data)
{
  if (service != NULL)
  {
    char *data;
    data = strdup (args);
    service->user_data = user_data;
    LOCK (&service->mutex);
    g_queue_push_tail (service->pending, data);
    UNLOCK (&service->mutex);
  }
}

gboolean
msg_service_send (msg_service *service, GSocketConnection *conn)
{
  if (service != NULL && msg_service_has_pending_message (service))
  {
    GOutputStream *out_channel;
    out_channel = g_io_stream_get_output_stream (G_IO_STREAM(conn));
    
    if (g_output_stream_has_pending (out_channel) == FALSE)
    {
      char *data;
      data = g_queue_pop_head (service->pending);
      g_output_stream_write_async (out_channel, data, strlen (data), 
         G_PRIORITY_DEFAULT, NULL, service->send_callback, service->user_data);
      g_free (data);
    
      return TRUE;
    }

  }
  return FALSE;
}

gssize
msg_service_receive (msg_service *service, GSocketConnection *conn, 
    char *buffer, gssize size)
{
  gssize bytes = 0;
  if (service != NULL)
  {
    char *internal_buffer, *save_ptr;
    int begin, end;
    save_ptr = internal_buffer = g_malloc0 ((size + 1) * sizeof (char));

    if (service->incomplete_reading || service->read_buffer->len == 0)
    {
      GError *error = NULL;
      GInputStream *input_stream = NULL;
      input_stream = g_io_stream_get_input_stream (G_IO_STREAM (conn));
      bytes = g_input_stream_read (input_stream, internal_buffer, size, NULL, 
          &error);
      internal_buffer = trim (internal_buffer);
    }
    else
    {
      bytes = MIN (size * sizeof(char), service->read_buffer->len);
      strncpy (internal_buffer, service->read_buffer->str, bytes); 
      g_string_erase (service->read_buffer, 0, bytes);
    }
    if (bytes > 0)
    {
      switch (_find_indexes (internal_buffer, &begin, &end))
      {
        case INDEXES_FOUND:
        {
          gssize offset = end - begin + 1;
          memcpy (buffer, &internal_buffer[begin], offset * sizeof (char));
          if (offset < bytes)
          {
            g_string_append (service->read_buffer, &internal_buffer[end +1]);
          }
          break;
        }
        case OPEN_BRACE_NOT_FOUND:
        {
          if (service->incomplete_reading == 1)
          {
            gssize offset = end + 1;
            strcpy (buffer, service->read_buffer->str);
            strncat (buffer, internal_buffer, offset);
            g_string_erase (service->read_buffer, 0, -1);
            if (offset + 1 < size)
              g_string_append (service->read_buffer, 
                                                &internal_buffer[offset + 1]);
            bytes = strlen (buffer);
            service->incomplete_reading = 0;
          }
          break;
        }
        case CLOSE_BRACE_NOT_FOUND:
        {
          service->incomplete_reading = 1;
          g_string_append (service->read_buffer, &internal_buffer[begin]);
          g_free (save_ptr);
          return msg_service_receive (service, conn, buffer, size); 
        }
        case BAD_FORMAT_STRING:
        case INTERNAL_ERROR:
        {
          buffer = NULL;
          bytes = 0;
          break;
        }
        default:
          /* NOT REACHEABLE */
          break;
      }
    }
    else
    {
      buffer = NULL;
    }
    g_free (save_ptr);
  }
  return bytes;
}

/**
 * It parses @buff (null terminated string) to find the indexes of '{' and '}' 
 * characteres  (in this sequence). The indexes @open_brace and @close_brace
 * hold the position of these characters whithin the string 
 * (-1 meas the characterer is not found).
 *
 * Return: 
 *    INDEXES_FOUND if both characteres were found in the correct sequence
 *    OPEN_BRACE_NOT_FOUND if the character '}' was found before the '{'
 *    CLOSE_BRACE_NOT_FOUND if the character '{' was found, but the '{' was not
 *    BAD_FORMAT_STRING if the string contains two '{' without any '}'
 *    INTERNAL_ERROR in case of any internal error
 */
static int 
_find_indexes (const char *buff, int *open_brace, int *close_brace)
{
  int i = 0, open_found = 0;
  *open_brace = -1;
  *close_brace = -1;
  if (buff == NULL)
    return INTERNAL_ERROR;

  while (buff[i] != '\0')
  {
    if (buff[i] == '{')
    {
      if (!open_found)
      {
        *open_brace = i;
        open_found = 1;
      }
      else
      {
        *open_brace = -1;
        return BAD_FORMAT_STRING;
      }
    }
    else if (buff[i] == '}')
    {
      *close_brace = i;
      if (!open_found)
        return OPEN_BRACE_NOT_FOUND;
      else
        return INDEXES_FOUND;
    }
    i++;
  }
  if (open_found)
    return CLOSE_BRACE_NOT_FOUND;
  
  return INTERNAL_ERROR;
}

static char *
trim (char *str)
{
  char *end;

  // Trim leading space
  while(isspace(*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace(*end)) end--;

  // Write new null terminator
  *(end+1) = 0;

  return str;
}

