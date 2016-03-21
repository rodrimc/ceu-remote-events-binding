return {
  -- Source: PLAYER
  ['PLAYER'] = 
  {
    {"REQUEST_JOIN", "JOIN", "localhost:8888"},
    {"REQUEST_SESSION", "CREATE_SESSION", "localhost:8888"},
    {"PLAY", "MEDIA_BEGIN", "localhost:8888"},
    {"STOP", "MEDIA_END", "localhost:8888"},
  },
  -- Source: MAESTRO
  ['MAESTRO'] = 
  {
    {"SESSION_CREATED", "SESSION_CREATED", "ANY"},
    {"MEDIA_STARTED", "START_MEDIA", "ANY"},
    {"JOINED", "DEVICE_JOINED", "ANY"},
  },
}
