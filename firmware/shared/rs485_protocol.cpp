#include "rs485_protocol.h"

namespace RS485Proto {

uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

uint16_t crc16(const String& data) {
  return crc16(reinterpret_cast<const uint8_t*>(data.c_str()), data.length());
}

String sanitizeField(const String& value) {
  String out = value;
  out.replace("\r", " ");
  out.replace("\n", " ");
  out.replace(",", ";");
  out.replace("*", "#");
  out.trim();
  return out;
}

String buildFrame(uint8_t nodeId, const String& command, const String& arg1, const String& arg2, uint16_t seq) {
  String body = String(nodeId);
  body += ",";
  body += sanitizeField(command);
  body += ",";
  body += sanitizeField(arg1);
  body += ",";
  body += sanitizeField(arg2);
  body += ",";
  body += String(seq);

  char crcBuffer[5];
  snprintf(crcBuffer, sizeof(crcBuffer), "%04X", crc16(body));

  String frame = "@";
  frame += body;
  frame += "*";
  frame += crcBuffer;
  return frame;
}

Frame parseFrame(const String& raw) {
  Frame frame;
  frame.raw = raw;
  String line = raw;
  line.trim();
  if (!line.startsWith("@")) {
    return frame;
  }

  const int starPos = line.lastIndexOf('*');
  if (starPos <= 1 || starPos >= line.length() - 1) {
    return frame;
  }

  String body = line.substring(1, starPos);
  String crcField = line.substring(starPos + 1);
  crcField.trim();
  const uint16_t expected = static_cast<uint16_t>(strtoul(crcField.c_str(), nullptr, 16));
  const uint16_t actual = crc16(body);
  if (expected != actual) {
    return frame;
  }

  int positions[4];
  int searchFrom = 0;
  for (int index = 0; index < 4; ++index) {
    positions[index] = body.indexOf(',', searchFrom);
    if (positions[index] < 0) {
      return frame;
    }
    searchFrom = positions[index] + 1;
  }

  frame.body = body;
  frame.nodeId = static_cast<uint8_t>(body.substring(0, positions[0]).toInt());
  frame.command = body.substring(positions[0] + 1, positions[1]);
  frame.arg1 = body.substring(positions[1] + 1, positions[2]);
  frame.arg2 = body.substring(positions[2] + 1, positions[3]);
  frame.seq = static_cast<uint16_t>(body.substring(positions[3] + 1).toInt());
  frame.valid = true;
  return frame;
}

bool payloadValue(const String& payload, const String& key, String& valueOut) {
  const String prefix = key + "=";
  int start = 0;
  while (start < payload.length()) {
    int end = payload.indexOf(';', start);
    if (end < 0) {
      end = payload.length();
    }
    const String token = payload.substring(start, end);
    if (token.startsWith(prefix)) {
      valueOut = token.substring(prefix.length());
      return true;
    }
    start = end + 1;
  }
  return false;
}

int payloadValueInt(const String& payload, const String& key, int defaultValue) {
  String value;
  if (!payloadValue(payload, key, value)) {
    return defaultValue;
  }
  return value.toInt();
}

bool payloadValueBool(const String& payload, const String& key, bool defaultValue) {
  String value;
  if (!payloadValue(payload, key, value)) {
    return defaultValue;
  }
  value.toLowerCase();
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

String setPayloadValue(const String& payload, const String& key, const String& value) {
  const String prefix = key + "=";
  String output;
  bool replaced = false;
  int start = 0;
  while (start < payload.length()) {
    int end = payload.indexOf(';', start);
    if (end < 0) {
      end = payload.length();
    }
    String token = payload.substring(start, end);
    if (token.length() > 0) {
      if (token.startsWith(prefix)) {
        token = prefix + sanitizeField(value);
        replaced = true;
      }
      if (output.length() > 0) {
        output += ";";
      }
      output += token;
    }
    start = end + 1;
  }
  if (!replaced) {
    if (output.length() > 0) {
      output += ";";
    }
    output += prefix + sanitizeField(value);
  }
  return output;
}

}  // namespace RS485Proto