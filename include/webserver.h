#pragma once

extern bool webReqInside;
extern bool webReqOutside;
extern bool otaInProgress;

void webserverBegin();
void webserverPushStatus();
void printWiFiStatus();
