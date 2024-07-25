#ifndef BLELOCKBASE_H
#define BLELOCKBASE_H

#include <NimBLEDevice.h>
#include <string>
#include <queue>
#include <utility>
//#include <FreeRTOS.h>
#include "json.hpp"
#include <SPIFFS.h>
#include <Arduino.h>
#include <colorLog.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <SecureConnection.h>
#include <MessageBase.h>

using json = nlohmann::json;

struct CreateCharacteristicCmd {
    std::string uuid;
    NimBLECharacteristic * pCharacteristic;
};


void printCharacteristics(NimBLEService *pService);

class BleLockBase {
public:
    BleLockBase(const std::string &lockName);
    virtual ~BleLockBase();

    virtual bool isServer () { return false;}
    virtual void setup() = 0;

    SecureConnection secureConnection;
    std::string generateUUID();
    uint32_t autoincrement;

    std::string getMacAddress ()
    {
        return macAddress;
    }


    SemaphoreHandle_t mutex;

    QueueHandle_t GetOutgoingQueue()
    {
        return outgoingQueue;
    }

protected:
    void initializeMutex();
    void createCharacteristic(const std::string &uuid, uint32_t properties);

    std::string macAddress;
    std::string lockName;
    NimBLEServer *pServer;
    NimBLEService *pService;
    NimBLECharacteristic *pPublicCharacteristic;

    QueueHandle_t characteristicCreationQueue;
    QueueHandle_t outgoingQueue;
    QueueHandle_t responseQueue;

private:
    virtual void handlePublicCharacteristicRead(NimBLECharacteristic *pCharacteristic, const std::string &mac) = 0;
};

#endif // BLELOCKBASE_H
