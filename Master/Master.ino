// Libraries ------------------------------------------------------------------

#include <esp_now.h>
#include <WiFi.h>

// Logging --------------------------------------------------------------------

/* 
 * Uncommenting DEBUGLOG_DISABLE_LOG disables ASSERT and all log (Release Mode)
 * PRINT and PRINTLN are always valid even in Release Mode 
 */
// #define DEBUGLOG_DISABLE_LOG

#include <DebugLog.h> // https://github.com/hideakitai/DebugLog

/*
 * Possible Options: LVL_NONE, LVL_ERROR, LVL_WARN, LVL_INFO, LVL_DEBUG, LVL_TRACE
 * Default: LVL_INFO
 */
#define DEBUGLOG_LOG_LEVEL DebugLogLevel::LVL_TRACE

// Delay ----------------------------------------------------------------------

#define NODELLAY

#ifdef NODELLAY
	#define DELAY(x)
#else
	#define DELAY(x) delay(x)
#endif

// Pinout ---------------------------------------------------------------------

#include "esp32_pinout.h"

// Geekworm Easy Kit ESP32-C1
#define LED_BUILTIN_RIGHT GPIO0
#define LED_BUILTIN_LEFT GPIO27
#define BTN_USER GPIO0

// Application logic options --------------------------------------------------

// #define DELETEBEFOREPAIR
#define CHANNEL 3
#define SLAVE_SSID_PATTERN "Slave"
const uint8_t broadcastAddress[] = {0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF};


typedef struct sendProtocolData_t {
	float temperature;
	float humidity;
};
sendProtocolData_t sendProtocolData;

// Helpers --------------------------------------------------------------------

#define MAC2String(macStr, mac_addr) snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
#define MACStr2Arr(macStr, arr) sscanf(macStr, "%x:%x:%x:%x:%x:%x", &arr[0], &arr[1], &arr[2], &arr[3], &arr[4], &arr[5]);

// Gloabal variables ----------------------------------------------------------

esp_now_peer_info_t slave;
bool slaveFound = 0;
uint8_t data = 0;
int pendingDataTransmission = 0;
uint8_t recvData[250];
int unhandledRecvData = 0;

// Configs/Inits --------------------------------------------------------------

void DebugLogConfig() {
	#ifndef DEBUGLOG_DISABLE_LOG
	Serial.begin(115200);
	Serial.println("");
	LOG_SET_LEVEL(DEBUGLOG_LOG_LEVEL);
	LOG_SET_OPTION(false, false, false);  // Toggle File, Line, Func
    LOG_SET_DELIMITER("");
	LOG_SET_BASE_RESET(true);
	#endif
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
	pendingDataTransmission--;
	if (pendingDataTransmission < 0) {
		pendingDataTransmission = 0;
		LOG_WARN("pendingDataTransmission < 0");
	}

	#ifndef DEBUGLOG_DISABLE_LOG
	char macStr[18];
	MAC2String(macStr, mac_addr);
	LOG_INFO("Last Packet Sent to: ", macStr);
	if (status == ESP_NOW_SEND_SUCCESS) {
		LOG_INFO("Last Packet Send Status: Delivery Success");
	} else {
		LOG_WARN("Last Packet Send Status: Delivery Fail");
	}
	#endif
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
	memcpy(recvData, data, data_len);
	unhandledRecvData++;

	#ifndef DEBUGLOG_DISABLE_LOG
	char macStr[18];
	MAC2String(macStr, mac_addr);
	LOG_INFO("Last Packet Recv from: ", macStr);
	LOG_INFO("Last Packet Recv Data: ", *data);
	#endif
}

void ESPNowConfig() {
	WiFi.mode(WIFI_STA);
	LOG_INFO("STA MAC: ", WiFi.macAddress());
	WiFi.disconnect();
	if (esp_now_init() != ESP_OK) {
		LOG_INFO("ESPNow Init Failed");
		// ESPNowConfig();
		ESP.restart();
		return;
	}
	LOG_INFO("ESPNow Init Success");

	esp_now_register_send_cb(OnDataSent);
	esp_now_register_recv_cb(OnDataRecv);
}

// ESPNow helpers -------------------------------------------------------------

bool addPeer(esp_now_peer_info_t *slave) {
	esp_err_t addStatus = esp_now_add_peer(slave);
	switch (addStatus) {
		case ESP_OK: LOG_INFO("Pair success"); return true;
		case ESP_ERR_ESPNOW_NOT_INIT: LOG_ERROR("ESPNOW Not Init"); return false;
		case ESP_ERR_ESPNOW_ARG: LOG_ERROR("Invalid Argument"); return false;
		case ESP_ERR_ESPNOW_FULL: LOG_ERROR("Peer list full"); return false;
		case ESP_ERR_ESPNOW_NO_MEM: LOG_ERROR("Out of memory"); return false;
		case ESP_ERR_ESPNOW_EXIST: LOG_WARN("Peer Exists"); return true;
	}
	return false;
}

bool deletePeer() {
	esp_err_t delStatus = esp_now_del_peer(slave.peer_addr);
	switch (delStatus) {
		case ESP_OK: LOG_INFO("Slave Delete Status: Success"); return true;
		case ESP_ERR_ESPNOW_NOT_INIT: LOG_ERROR("Slave Delete Status: ESPNOW Not Init"); return false;
		case ESP_ERR_ESPNOW_ARG: LOG_ERROR("Slave Delete Status: Invalid Argument"); return false;
		case ESP_ERR_ESPNOW_NOT_FOUND: LOG_ERROR("Slave Delete Status: Peer not found."); return false;
	}
	return false;
}

bool manageSlave() {
	#ifdef DELETEBEFOREPAIR
	deletePeer();
	#endif

	bool exists = esp_now_is_peer_exist(slave.peer_addr);
	if (exists) {
		LOG_INFO("Slave Status: Already Paired");
		return true;
	}

	return addPeer(&slave);
}

void sendData(esp_now_peer_info_t slave, void *data, size_t len) {
	//LOG_INFO("Sending: ", data);
	
	while(pendingDataTransmission > 0) {}
	pendingDataTransmission++;

	if (!WiFi.softAP("Master_1", "Slave_1_Password", slave.channel, 1)) {
		LOG_ERROR("AP Config failed.");
		return;
	}

	esp_err_t result = esp_now_send(slave.peer_addr, (const uint8_t*) data, len);
	//esp_err_t result = esp_now_send(slave.peer_addr, &data, sizeof(data));
	
	switch (result) {
		case ESP_OK: LOG_INFO("Send Status: Success"); return;
		case ESP_ERR_ESPNOW_NOT_INIT: LOG_ERROR("Send Status: ESPNOW not Init."); return;
		case ESP_ERR_ESPNOW_ARG: LOG_ERROR("Send Status: Invalid Argument"); return;
		case ESP_ERR_ESPNOW_INTERNAL: LOG_ERROR("Send Status: Internal Error"); return;
		case ESP_ERR_ESPNOW_NO_MEM: LOG_ERROR("Send Status: ESP_ERR_ESPNOW_NO_MEM"); return;
		case ESP_ERR_ESPNOW_NOT_FOUND: LOG_ERROR("Send Status: Peer not found."); return;
	}
}

// Application logic ----------------------------------------------------------

void ScanForSlave() {
	int8_t scanResults = WiFi.scanNetworks();
	if (scanResults == 0) {
		LOG_INFO("No WiFi devices in AP Mode found");
		WiFi.scanDelete();
		return;
	}

	#ifndef DEBUGLOG_DISABLE_LOG
	LOG_INFO("+--------------------+");
	LOG_INFO("| WiFi Access Points |");
	LOG_INFO("+--------------------+");
	for (int i = 0; i < scanResults; ++i) {
		LOG_INFO("| ", i + 1, ") ", WiFi.SSID(i), ":", WiFi.channel(i), " (", WiFi.RSSI(i), ")", " [", WiFi.BSSIDstr(i), "]");
		DELAY(10);
	}
	LOG_INFO("+--------------------+");
	#endif

	slaveFound = 0;
	memset(&slave, 0, sizeof(slave));
	for (int i = 0; i < scanResults; ++i) {
		if (WiFi.SSID(i).indexOf(SLAVE_SSID_PATTERN) != 0) {
			continue;
		}

		MACStr2Arr(WiFi.BSSIDstr(i).c_str(), slave.peer_addr);
		//slave.peer_addr[5]--;
		//slave.peer_addr[0] = 0xFF;
		//slave.peer_addr[1] = 0xFF;
		//slave.peer_addr[2] = 0xFF;
		//slave.peer_addr[3] = 0xFF;
		//slave.peer_addr[4] = 0xFF;
		//slave.peer_addr[5] = 0xFF;

		slave.channel = WiFi.channel(i);
		//slave.channel = 0;
		slave.encrypt = 0;
		slaveFound = 1;
	}

	LOG_INFO(slaveFound ? "Slave Found, processing..." : "Slave Not Found, trying again.");
}

void setup() {
	DebugLogConfig();	
	LOG_INFO("+----------------------+");
	LOG_INFO("| ESPNow Testing Suite |");
	LOG_INFO("+----------------------+");
	
	ESPNowConfig();
}

void loop() {
	ScanForSlave();

	bool isPaired = manageSlave();
	if (!isPaired) {
		LOG_WARN("Slave pair failed!");
		return;
	}

	data++;
	sendProtocolData.temperature = 22.3;
	sendProtocolData.humidity = 35.5;
	sendData(slave, &sendProtocolData, sizeof(sendProtocolData_t));

	DELAY(3000);
}
