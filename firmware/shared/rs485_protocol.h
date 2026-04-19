#pragma once

#include <Arduino.h>

namespace RS485Proto {

static const uint8_t BROADCAST_NODE_ID = 255;

struct Frame {
  String raw;
  String body;
  uint8_t nodeId = 0;
  String command;
  String arg1;
  String arg2;
  uint16_t seq = 0;
  bool valid = false;
};

uint16_t crc16(const uint8_t* data, size_t len);
uint16_t crc16(const String& data);
String sanitizeField(const String& value);
String buildFrame(uint8_t nodeId, const String& command, const String& arg1, const String& arg2, uint16_t seq);
Frame parseFrame(const String& raw);
bool payloadValue(const String& payload, const String& key, String& valueOut);
int payloadValueInt(const String& payload, const String& key, int defaultValue = 0);
bool payloadValueBool(const String& payload, const String& key, bool defaultValue = false);
String setPayloadValue(const String& payload, const String& key, const String& value);

}  // namespace RS485Proto