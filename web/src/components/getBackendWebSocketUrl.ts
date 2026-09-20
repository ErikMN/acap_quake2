/* Build the WebSocket URL for the ACAP reverse proxy */
export const getBackendWebSocketUrl = (): string => {
  const baseUrl = import.meta.env.BASE_URL.endsWith('/')
    ? import.meta.env.BASE_URL
    : `${import.meta.env.BASE_URL}/`;
  const url = new URL(`${baseUrl}input`, window.location.origin);

  url.protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';

  return url.toString();
};
