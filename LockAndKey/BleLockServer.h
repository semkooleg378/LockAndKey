#ifndef BLELOCKSERVER_H
#define BLELOCKSERVER_H

#include "BleLockBase.h"

class BleLockServer : public BleLockBase {
public:
    BleLockServer(const std::string &lockName);
    void setup() override;
    bool isServer () { return true;}

    void resumeAdvertising();

    std::unordered_map<std::string, BLECharacteristic *> uniqueCharacteristics;
    std::unordered_map<std::string, bool> confirmedCharacteristics;
    std::unordered_map<std::string, std::string> pairedDevices; // map for paired devices
    
    static std::unordered_map<std::string, bool> confirmedDevices; // map for paired devices
    static void loadConfirmedDevices();
    static void saveConfirmedDevices();

    static std::unordered_map<std::string, std::string> messageMacBuff; // map for multypart messages

    QueueHandle_t incomingQueue{};

    [[noreturn]] [[noreturn]] static void characteristicCreationTask(void *pvParameter);
    [[noreturn]] static void outgoingMessageTask(void *pvParameter);
    [[noreturn]] static void parsingIncomingTask(void *pvParameter);

    std::string memoryFilename;
    MessageBase *request(MessageBase *requestMessage, const std::string &destAddr, uint32_t timeout) const;

    bool confirm (std::string mac)
    { 
        return confirmedDevices[mac]; 
        //return true;
    }


private:
    static void processRequestTask(void *pvParameter);
    
    
    void saveCharacteristicsToMemory();

    void loadCharacteristicsFromMemory();

    void handlePublicCharacteristicRead(NimBLECharacteristic *pCharacteristic, const std::string &mac) override;

    class ServerCallbacks : public NimBLEServerCallbacks {
    public:
        explicit ServerCallbacks(BleLockServer *lock);
        void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override;
        void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override;

    private:
        BleLockServer *lock;
    };

    class PublicCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    public:
        explicit PublicCharacteristicCallbacks(BleLockServer *lock);
        void onRead(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) override;

    private:
        BleLockServer *lock;
    };

    class UniqueCharacteristicCallbacks : public BLECharacteristicCallbacks {
    public:
        UniqueCharacteristicCallbacks(BleLockServer *lock, std::string uuid);

        void onWrite(BLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) override;


    private:
        BleLockServer *lock;
        std::string uuid;
    };
};

struct RequestTaskParams {
    BleLockServer *bleLock;
    MessageBase *requestMessage;
};

#endif // BLELOCKSERVER_H
