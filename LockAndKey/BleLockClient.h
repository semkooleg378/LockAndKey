#ifndef BLELOCKCLIENT_H
#define BLELOCKCLIENT_H

#include "BleLockBase.h"
#include "CommandManager.h"

#define DeviceFile "devices.json"

extern BLEUUID serviceUUID;
extern BLEUUID publicCharUUID;


struct RegServerKey {
    std::string macAdr;
    std::string characteristicUUID;
};

class ServerReg {
    std::unordered_map<std::string, RegServerKey> uniqueServers;

public:
    void serialize() {
        json j;
        for (const auto &kv : uniqueServers) {
            j[kv.first] = kv.second.characteristicUUID;
        }

        File file = SPIFFS.open(DeviceFile, FILE_WRITE);
        if (!file) {
            Serial.println("Failed to open file for writing");
            return;
        }

        file.print(j.dump().c_str());
        file.close();
    }

    void deserialize() {
        File file = SPIFFS.open(DeviceFile, FILE_READ);
        if (!file) {
            Serial.println("Failed to open file for reading");
            return;
        }

        size_t size = file.size();
        if (size == 0) {
            file.close();
            return;
        }

        std::unique_ptr<char[]> buf(new char[size]);
        file.readBytes(buf.get(), size);

        json j = json::parse(buf.get());
        uniqueServers.clear();
        for (auto &el : j.items()) {
            uniqueServers[el.key()] = RegServerKey();
            uniqueServers[el.key()].macAdr = el.key(); 
            uniqueServers[el.key()].characteristicUUID = el.value();
        }

        file.close();
    }

    bool getServerDataFirst(std::string &name) {
        if (!uniqueServers.empty()) {
            name = uniqueServers.begin()->first;
            return true;
        }
        return false;
    }

    bool getServerData(const std::string &name, RegServerKey &val) {
        if (uniqueServers.find(name) != uniqueServers.end()) {
            val = uniqueServers[name];
            return true;
        }
        return false;
    }

    void insert(const std::string &name, RegServerKey &val) {
        uniqueServers.insert_or_assign(name, val);
        serialize();
    }

    void remove(const std::string &name) {
        uniqueServers.erase(name);
    }

    ServerReg() = default;
    ~ServerReg() = default;
};

enum KeyStatusType
{
    statusWaitForAnswer,
    statusNone,
    statusPublickKeyExist,
    statusSessionKeyCreated,
    statusOpenCommand,
    statusEnd
};


class BleLockClient : public BleLockBase {
public:
    BleLockClient(const std::string &lockName);
    void setup() override;
    bool isServer () { return false;}


    static ServerReg regServer;

    void connectToServer();

    static BleLockClient * currentClient;
    static void SetCurrentClient (BleLockClient * client)
    {
        currentClient = client;
    }
    static BleLockClient * GetCurrentClient ()
    {
        return currentClient;
    }
        
    QueueHandle_t jsonParsingQueue{};

    MessageBase *request(MessageBase *requestMessage, const std::string &destAddr, uint32_t timeout) const;
    std::string memoryFilename;
    static std::unordered_map<std::string, std::string> messageControll; // map for multypart messages

    static CommandManager commandManager;
    std::string servMac;

    NimBLEAdvertisedDevice *advDevice;
    std::string uniqueUUID;

    NimBLERemoteCharacteristic *pUniqueCharExt;
    NimBLEScan *pScan;
    static void StartScan ();
    
    static std::unordered_map<std::string, KeyStatusType> Locks;

    class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *advertisedDevice) override {
        logColor(LColor::Green, F("Found device: %s (%s)"), advertisedDevice->toString().c_str(), advertisedDevice->getName().c_str());
        if (advertisedDevice->haveServiceUUID()) {
            logColor(LColor::Green, F("Service device: %d === %s"), (int)advertisedDevice->isAdvertisingService(serviceUUID), serviceUUID.toString().c_str());
        } else {
            logColor(LColor::Green, F("No service"));
        }
        if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID)) {
            logColor(LColor::Green, F("Found device: %s"), advertisedDevice->toString().c_str());
            BleLockClient::GetCurrentClient()->advDevice = advertisedDevice;
            if (BleLockClient::GetCurrentClient()->advDevice->getName() == "BleLock") {
                NimBLEDevice::getScan()->stop();
                commandManager.sendCommand("connectToServer");
            }
        }
    }
};

    static bool isConnected;
private:
    NimBLEClient *pClient;
    NimBLEClientCallbacks *clientCbk;


    void handlePublicCharacteristicRead(NimBLECharacteristic *pCharacteristic, const std::string &mac) override;

    class ClientCallbacks : public NimBLEClientCallbacks {
    public:
        explicit ClientCallbacks(BleLockClient *lock);
        void onConnect(NimBLEClient *pClient) override;
        void onDisconnect(NimBLEClient *pClient) override;

    private:
        BleLockClient *lock;
    };

    [[noreturn]] static void outgoingMessageTask(void *pvParameter);

    [[noreturn]] static void jsonParsingTask(void *pvParameter);

    QueueHandle_t getOutgoingQueueHandle() const;

    std::string key;
};


#endif // BLELOCKCLIENT_H
