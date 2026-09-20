#include "acap_input.h"

#include "websocket.h"

bool
acap_input_init(void)
{
  return acap_websocket_start();
}

void
acap_input_shutdown(void)
{
  acap_websocket_stop();
}
