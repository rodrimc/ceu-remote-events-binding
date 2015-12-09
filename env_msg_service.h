#ifndef ENV_MSG_SERVICE
#define ENV_MSG_SERVICE

#include <gio/gio.h>

typedef struct _msg_service msg_service;

msg_service *
msg_service_new (void);

void
msg_service_destroy (msg_service *);

void
msg_service_push (msg_service *, const char *, gpointer);

gboolean
msg_service_send (msg_service *, GSocketConnection *);

gboolean
msg_service_has_pending_message (msg_service *);

void
msg_service_has_pending (msg_service *);

void
msg_service_register_send_callback (msg_service *, GAsyncReadyCallback);

gssize
msg_service_receive (msg_service *, GSocketConnection *, char *, gssize);


#endif /* ENV_MSG_SERVICE */
