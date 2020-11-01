#include "pch.h"
#include "dllmain.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <bthdef.h>
#include <combaseapi.h>
#include <Bluetoothleapis.h>
#include <initguid.h>
#include <devpkey.h>
#include <devpropdef.h>
#pragma comment(lib, "SetupAPI")
#pragma comment(lib, "BluetoothApis.lib")

#define TO_SEARCH_DEVICE_UUID "{6E400001-B5A3-F393-E0A9-E50E24DCCA9E}"
#define NUS_TX_CHARACTERISTIC_SHORT_UUID 0x0002

#define WINDOWS_BLUETOOTH_CLASS_GUID "{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}"
#define WINDOWS_DEVICE_FRIENDLY_NAME L"NFS_Controller"

HANDLE nusDeviceHandle = NULL;
PBTH_LE_GATT_CHARACTERISTIC nusTxCharacteristic = NULL;

int GetControllerStatus(__in GUID AGuid) {

	HDEVINFO hDI;
	GUID BluetoothInterfaceGUID = AGuid;
	SP_DEVICE_INTERFACE_DATA did;
	SP_DEVINFO_DATA dd;
	DWORD size = 0;

	hDI = SetupDiGetClassDevs(&BluetoothInterfaceGUID, NULL, NULL, DIGCF_PRESENT);
	if (hDI == INVALID_HANDLE_VALUE) return -1;

	dd.cbSize = sizeof(SP_DEVINFO_DATA);
	did.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	DWORD index = 0;

	while (SetupDiEnumDeviceInfo(hDI, index, &dd) == TRUE) {

		DEVPROPTYPE ulPropertyType;
		DWORD dwSize;
		ULONG devst = 0;

		SetupDiGetDeviceProperty(hDI, &dd, &DEVPKEY_NAME, &ulPropertyType, NULL, 0, &dwSize, 0);
		wchar_t* dev_name;
		dev_name = (wchar_t*)malloc(dwSize);
		SetupDiGetDeviceProperty(hDI, &dd, &DEVPKEY_NAME, &ulPropertyType, (PBYTE)dev_name, dwSize, NULL, 0);
		if (wcscmp(WINDOWS_DEVICE_FRIENDLY_NAME, (const wchar_t*)dev_name) == 0) {
			free(dev_name);
			SetupDiGetDeviceProperty(hDI, &dd, &DEVPKEY_Device_DevNodeStatus, &ulPropertyType, (BYTE*)&devst, sizeof(devst), &dwSize, 0);
			SetupDiDestroyDeviceInfoList(hDI);
			if (devst & 0x2000000) return -1;
			else return 0;
			return -1;
		}
		else {
			free(dev_name);
		}
		index++;
	}
	SetupDiDestroyDeviceInfoList(hDI);
	return -1;
}

HANDLE GetBLEHandle(__in GUID AGuid)
{
	HDEVINFO hDI;
	SP_DEVICE_INTERFACE_DATA did;
	SP_DEVINFO_DATA dd;
	GUID BluetoothInterfaceGUID = AGuid;
	HANDLE hComm = NULL;

	hDI = SetupDiGetClassDevs(&BluetoothInterfaceGUID, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (hDI == INVALID_HANDLE_VALUE) return NULL;

	did.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	dd.cbSize = sizeof(SP_DEVINFO_DATA);

	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDI, NULL, &BluetoothInterfaceGUID, i, &did); i++)
	{
		SP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData;

		DeviceInterfaceDetailData.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

		DWORD size = 0;

		if (!SetupDiGetDeviceInterfaceDetail(hDI, &did, NULL, 0, &size, 0))
		{
			int err = GetLastError();
			if (err == ERROR_NO_MORE_ITEMS) break;

			PSP_DEVICE_INTERFACE_DETAIL_DATA pInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)GlobalAlloc(GPTR, size);
			pInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

			if (!SetupDiGetDeviceInterfaceDetail(hDI, &did, pInterfaceDetailData, size, &size, &dd)) {
				GlobalFree(pInterfaceDetailData);
				break;
			}

			hComm = CreateFile(pInterfaceDetailData->DevicePath, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

			GlobalFree(pInterfaceDetailData);
		}
	}

	SetupDiDestroyDeviceInfoList(hDI);
	return hComm;
}

void writeChar(unsigned char* data, unsigned long length, HANDLE hDevice, PBTH_LE_GATT_CHARACTERISTIC parentCharacteristic)
{
	BTH_LE_GATT_RELIABLE_WRITE_CONTEXT relialeWriteContext = NULL;

	HRESULT hr = BluetoothGATTBeginReliableWrite(hDevice, &relialeWriteContext, BLUETOOTH_GATT_FLAG_NONE);

	PBTH_LE_GATT_CHARACTERISTIC_VALUE newValue = (PBTH_LE_GATT_CHARACTERISTIC_VALUE)malloc(sizeof(BTH_LE_GATT_CHARACTERISTIC_VALUE) + length);

	if (SUCCEEDED(hr))
	{
		memset(newValue, 0, sizeof(BTH_LE_GATT_CHARACTERISTIC_VALUE) + length);
		newValue->DataSize = (ULONG)length;
		memcpy(newValue->Data, data, length);
		hr = BluetoothGATTSetCharacteristicValue( hDevice, parentCharacteristic, newValue, NULL, BLUETOOTH_GATT_FLAG_NONE);
	}
	else {
		free(newValue);
		return;
	}

	if (NULL != relialeWriteContext)
	{
		BluetoothGATTEndReliableWrite(hDevice, relialeWriteContext, BLUETOOTH_GATT_FLAG_NONE);
	}
	else {
		free(newValue);
		return;
	}

	free(newValue);
}

NUS_EXPORT int OpenBleNusHandle() {

	GUID AGuid;

	CLSIDFromString(TEXT(WINDOWS_BLUETOOTH_CLASS_GUID), &AGuid);
	if (GetControllerStatus(AGuid)) return -1;

	CLSIDFromString(TEXT(TO_SEARCH_DEVICE_UUID), &AGuid);
	HANDLE hLEDevice = GetBLEHandle(AGuid);
	if (hLEDevice == NULL) return -1;
	nusDeviceHandle = hLEDevice;

	USHORT serviceBufferCount;

	HRESULT hr = BluetoothGATTGetServices( hLEDevice, 0, NULL, &serviceBufferCount, BLUETOOTH_GATT_FLAG_NONE);
	if (HRESULT_FROM_WIN32(ERROR_MORE_DATA) != hr)  return -1;

	PBTH_LE_GATT_SERVICE pServiceBuffer = (PBTH_LE_GATT_SERVICE)malloc(sizeof(BTH_LE_GATT_SERVICE) * serviceBufferCount);
	if (NULL == pServiceBuffer) return -1;
	else RtlZeroMemory(pServiceBuffer, sizeof(BTH_LE_GATT_SERVICE) * serviceBufferCount);

	USHORT numServices;
	hr = BluetoothGATTGetServices(hLEDevice, serviceBufferCount, pServiceBuffer, &numServices, BLUETOOTH_GATT_FLAG_NONE);
	if (S_OK != hr) {
		free(pServiceBuffer);
		return - 1;
	}

	USHORT charBufferSize;
	hr = BluetoothGATTGetCharacteristics(hLEDevice, pServiceBuffer, 0, NULL, &charBufferSize, BLUETOOTH_GATT_FLAG_NONE);
	if (HRESULT_FROM_WIN32(ERROR_MORE_DATA) != hr) return -1;

	PBTH_LE_GATT_CHARACTERISTIC pCharBuffer = NULL;

	if (charBufferSize > 0) {
		pCharBuffer = (PBTH_LE_GATT_CHARACTERISTIC) malloc(charBufferSize * sizeof(BTH_LE_GATT_CHARACTERISTIC));

		if (NULL == pCharBuffer) return -1;
		else RtlZeroMemory(pCharBuffer, charBufferSize * sizeof(BTH_LE_GATT_CHARACTERISTIC));

		USHORT numChars;
		hr = BluetoothGATTGetCharacteristics(hLEDevice, pServiceBuffer, charBufferSize, pCharBuffer, &numChars, BLUETOOTH_GATT_FLAG_NONE);
		if ((S_OK != hr) || (numChars != charBufferSize)) {
			free(pServiceBuffer);
			free(pCharBuffer);
			return -1;
		}
	}

	PBTH_LE_GATT_CHARACTERISTIC currGattChar;
	for (int ii = 0; ii < charBufferSize; ii++) {

		currGattChar = &pCharBuffer[ii];

		if (currGattChar->IsWritable && currGattChar->CharacteristicUuid.Value.ShortUuid == NUS_TX_CHARACTERISTIC_SHORT_UUID) {
			nusTxCharacteristic = currGattChar;
			return 0;
		}
	}
	return -1;
}

NUS_EXPORT int SendNusMessage(char* message, unsigned int len) {
	if (nusDeviceHandle == NULL) return -1;
	if (nusTxCharacteristic == NULL) return -1;
	writeChar((unsigned char*)message, len, nusDeviceHandle, nusTxCharacteristic);
	return 0;
}