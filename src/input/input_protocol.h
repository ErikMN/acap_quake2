#pragma once

#include "input_queue.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Browser input wire protocol version.
 *
 * Byte 0 contains this version and byte 1 contains an acap_input_message_type.
 * Multi-byte values are little-endian. The complete packet layouts are documented in docs/ARCHITECTURE.md.
 */
#define ACAP_INPUT_PROTOCOL_VERSION 1

/*
 * Message type values are part of the version 1 wire format.
 * Keep them in sync with the frontend MessageType enum in QuakeInputHandler.tsx.
 */
enum acap_input_message_type {
  ACAP_INPUT_MESSAGE_KEY = 1,
  ACAP_INPUT_MESSAGE_MOUSE = 2,
  ACAP_INPUT_MESSAGE_BUTTON = 3,
  ACAP_INPUT_MESSAGE_WHEEL = 4,
  ACAP_INPUT_MESSAGE_RESET = 5,
};

/*
 * Validate and decode one complete protocol packet.
 *
 * Returns true when the packet is valid and event contains the decoded input.
 * Returns false for an unsupported version, type, length or field value. The caller must ignore event in that case.
 */
bool acap_input_protocol_decode(const uint8_t *data, size_t len, struct acap_input_event *event);
