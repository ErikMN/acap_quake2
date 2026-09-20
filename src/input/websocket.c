#include "websocket.h"

#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>

#include <libwebsockets.h>

#define ACAP_WEBSOCKET_PORT 9000

struct websocket_server_state {
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  struct lws_context *context;
  bool thread_created;
  bool startup_complete;
  bool running;
  bool stop_requested;
};

static struct websocket_server_state server = {
  .mutex = PTHREAD_MUTEX_INITIALIZER,
  .condition = PTHREAD_COND_INITIALIZER,
};

static int
websocket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in,
                   size_t len)
{
  (void)wsi;
  (void)user;
  (void)in;

  switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
      syslog(LOG_INFO, "ACAP input: WebSocket client connected");
      break;

    case LWS_CALLBACK_RECEIVE:
      syslog(LOG_INFO, "ACAP input: WebSocket received %zu bytes", len);
      break;

    case LWS_CALLBACK_CLOSED:
      syslog(LOG_INFO, "ACAP input: WebSocket client disconnected");
      break;

    default:
      break;
  }

  return 0;
}

static const struct lws_protocols protocols[] = {
  {
    .name = "acap-input",
    .callback = websocket_callback,
    .per_session_data_size = 0,
    .rx_buffer_size = 0,
  },
  LWS_PROTOCOL_LIST_TERM
};

static void *
websocket_thread(void *arg)
{
  struct lws_context_creation_info info;
  struct lws_context *context;
  int service_result;

  (void)arg;

  memset(&info, 0, sizeof(info));
  info.port = ACAP_WEBSOCKET_PORT;
  info.iface = "lo";
  info.protocols = protocols;
  info.options = LWS_SERVER_OPTION_DISABLE_IPV6;
  info.gid = -1;
  info.uid = -1;

  lws_set_log_level(LLL_ERR | LLL_WARN, NULL);

  context = lws_create_context(&info);

  pthread_mutex_lock(&server.mutex);
  server.context = context;
  server.running = context != NULL;
  server.startup_complete = true;
  pthread_cond_broadcast(&server.condition);
  pthread_mutex_unlock(&server.mutex);

  if (!context) {
    syslog(LOG_ERR, "ACAP input: failed to create WebSocket context");
    return NULL;
  }

  syslog(LOG_INFO, "ACAP input: WebSocket server listening on 127.0.0.1:%d",
         ACAP_WEBSOCKET_PORT);

  for (;;) {
    bool stop_requested;

    pthread_mutex_lock(&server.mutex);
    stop_requested = server.stop_requested;
    pthread_mutex_unlock(&server.mutex);

    if (stop_requested) {
      break;
    }

    service_result = lws_service(context, 0);

    if (service_result < 0) {
      syslog(LOG_ERR, "ACAP input: WebSocket service failed: %d", service_result);
      break;
    }
  }

  pthread_mutex_lock(&server.mutex);
  server.context = NULL;
  server.running = false;
  pthread_mutex_unlock(&server.mutex);

  lws_context_destroy(context);
  syslog(LOG_INFO, "ACAP input: WebSocket server stopped");

  return NULL;
}

bool
acap_websocket_start(void)
{
  bool running;
  int result;

  pthread_mutex_lock(&server.mutex);

  if (server.thread_created) {
    running = server.running;
    pthread_mutex_unlock(&server.mutex);
    return running;
  }

  server.context = NULL;
  server.startup_complete = false;
  server.running = false;
  server.stop_requested = false;

  result = pthread_create(&server.thread, NULL, websocket_thread, NULL);

  if (result != 0) {
    pthread_mutex_unlock(&server.mutex);
    syslog(LOG_ERR, "ACAP input: failed to create WebSocket thread: %s", strerror(result));
    return false;
  }

  server.thread_created = true;

  while (!server.startup_complete) {
    pthread_cond_wait(&server.condition, &server.mutex);
  }

  running = server.running;
  pthread_mutex_unlock(&server.mutex);

  if (!running) {
    pthread_join(server.thread, NULL);

    pthread_mutex_lock(&server.mutex);
    server.thread_created = false;
    pthread_mutex_unlock(&server.mutex);
  }

  return running;
}

void
acap_websocket_stop(void)
{
  struct lws_context *context;
  pthread_t thread;
  int result;

  pthread_mutex_lock(&server.mutex);

  if (!server.thread_created) {
    pthread_mutex_unlock(&server.mutex);
    return;
  }

  server.stop_requested = true;
  context = server.context;
  thread = server.thread;

  if (context) {
    lws_cancel_service(context);
  }

  pthread_mutex_unlock(&server.mutex);

  result = pthread_join(thread, NULL);

  if (result != 0) {
    syslog(LOG_ERR, "ACAP input: failed to join WebSocket thread: %s", strerror(result));
  }

  pthread_mutex_lock(&server.mutex);
  server.context = NULL;
  server.thread_created = false;
  server.startup_complete = false;
  server.running = false;
  server.stop_requested = false;
  pthread_mutex_unlock(&server.mutex);
}
