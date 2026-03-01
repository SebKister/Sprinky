#include <WiFiUdp.h>
#include "ntp.h"

unsigned long currentEpoch = 0;
unsigned long lastEpochUpdateMillis = 0;

static WiFiUDP Udp;
static const int NTP_PACKET_SIZE = 48;
static byte packetBuffer[NTP_PACKET_SIZE];

unsigned long fetchNTPTime() {
  Udp.begin(2390); // Local port
  for (int attempt = 0; attempt < 5; attempt++) {
    // Send NTP packet
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    packetBuffer[0]  = 0b11100011; // LI, Version, Mode
    packetBuffer[1]  = 0;          // Stratum
    packetBuffer[2]  = 6;          // Polling Interval
    packetBuffer[3]  = 0xEC;       // Peer Clock Precision
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    Udp.beginPacket("pool.ntp.org", 123);
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();

    // Wait for response
    unsigned long startWait = millis();
    while (millis() - startWait < 2000) {
      if (Udp.parsePacket()) {
        Udp.read(packetBuffer, NTP_PACKET_SIZE);
        // Extract timestamp from bytes 40-43
        unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
        unsigned long lowWord  = word(packetBuffer[42], packetBuffer[43]);
        unsigned long secsSince1900 = highWord << 16 | lowWord;
        const unsigned long seventyYears = 2208988800UL;
        Udp.stop();
        return secsSince1900 - seventyYears;
      }
      delay(10);
    }
  }
  Udp.stop();
  return 0; // Timeout
}
