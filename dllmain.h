#pragma once

#define NUS_EXPORT __declspec(dllexport)

extern "C" {
	NUS_EXPORT int OpenBleNusHandle();
	NUS_EXPORT int SendNusMessage(char* message, unsigned int len);
}