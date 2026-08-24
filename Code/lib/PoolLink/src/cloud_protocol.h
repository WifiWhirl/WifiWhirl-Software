#pragma once

// Handshake (plaintext)
#define POOLLINK_MSG_HELLO        0x01  // Device → Server
#define POOLLINK_MSG_HELLO_ACK    0x02  // Server → Device

// Encrypted messages
#define POOLLINK_MSG_SENSOR_DATA  0x03  // Device → Server
#define POOLLINK_MSG_SENSOR_ACK   0x0A  // Server → Device (includes outdoor_temp)
#define POOLLINK_MSG_COMMAND      0x04  // Server → Device
#define POOLLINK_MSG_COMMAND_ACK  0x05  // Device → Server
#define POOLLINK_MSG_PING         0x06  // Both directions
#define POOLLINK_MSG_PONG         0x07  // Both directions
#define POOLLINK_MSG_ERROR        0x08  // Both directions
#define POOLLINK_MSG_GOODBYE      0x09  // Both directions
#define POOLLINK_MSG_QUEUE_DATA   0x0B  // Device → Server (command queue snapshot)
#define POOLLINK_MSG_SETTINGS_DATA 0x0C // Device → Server (spa settings snapshot)

// Frame layout constants
#define POOLLINK_FRAME_LENGTH_SIZE   2
#define POOLLINK_FRAME_TYPE_SIZE     1
#define POOLLINK_FRAME_SEQ_SIZE      8
#define POOLLINK_FRAME_TAG_SIZE      16
#define POOLLINK_FRAME_HEADER_SIZE   (POOLLINK_FRAME_TYPE_SIZE + POOLLINK_FRAME_SEQ_SIZE)
#define POOLLINK_FRAME_OVERHEAD      (POOLLINK_FRAME_LENGTH_SIZE + POOLLINK_FRAME_HEADER_SIZE + POOLLINK_FRAME_TAG_SIZE)
// Full compact-key state snapshot (states + times + smart schedule) runs
// ~800 B; 1024 leaves margin for future fields. MUST match the backend's
// poollink/protocol.py MAX_PAYLOAD.
#define POOLLINK_MAX_PAYLOAD         1024
#define POOLLINK_MAX_FRAME           (POOLLINK_FRAME_OVERHEAD + POOLLINK_MAX_PAYLOAD)

#define POOLLINK_NONCE_SIZE          32  // must match backend poollink/protocol.py NONCE_SIZE
#define POOLLINK_SESSION_KEY_SIZE    32
#define POOLLINK_AUTH_TAG_SIZE       16
#define POOLLINK_MAX_HOSTNAME        32

#define POOLLINK_PROTOCOL_VERSION    0x01
#define POOLLINK_HKDF_INFO           "poollink-v1"
