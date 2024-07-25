#include <BleLockBase.h>

BleLockBase::BleLockBase(const std::string &lockName)
    : lockName(lockName), pServer(nullptr), pService(nullptr), pPublicCharacteristic(nullptr) {
    characteristicCreationQueue = xQueueCreate(10, sizeof(CreateCharacteristicCmd *));
    outgoingQueue = xQueueCreate(10, sizeof(MessageBase *));
    responseQueue = xQueueCreate(10, sizeof(std::string *));
    autoincrement = 0;
    initializeMutex();
}

BleLockBase::~BleLockBase() {
    vQueueDelete(characteristicCreationQueue);
    vQueueDelete(outgoingQueue);
    vQueueDelete(responseQueue);
    vSemaphoreDelete(mutex);
}

std::string BleLockBase::generateUUID() {
    char uuid[37];
    snprintf(uuid, sizeof(uuid),
             "%08x-%04x-%04x-%04x-%012x",
             esp_random(),
             (autoincrement++ & 0xFFFF),
             (esp_random() & 0x0FFF) | 0x4000,
             (esp_random() & 0x3FFF) | 0x8000,
             esp_random());
    return {uuid};
}


void BleLockBase::initializeMutex() {
    mutex = xSemaphoreCreateMutex();
}

void BleLockBase::createCharacteristic(const std::string &uuid, uint32_t properties) {
    if (pService) {
        pPublicCharacteristic = pService->createCharacteristic(
            BLEUUID(uuid),
            properties
        );
    }
}

void printCharacteristics(NimBLEService *pService) {
    Serial.println("Listing characteristics:");

    std::vector<NimBLECharacteristic *> characteristics = pService->getCharacteristics();
    for (auto &characteristic: characteristics) {
        Serial.print("Characteristic UUID: ");
        Serial.println(characteristic->getUUID().toString().c_str());

        Serial.print("Properties: ");
        uint32_t properties = characteristic->getProperties();
        if (properties & NIMBLE_PROPERTY::READ) {
            Serial.print("READ ");
        }
        if (properties & NIMBLE_PROPERTY::WRITE) {
            Serial.print("WRITE ");
        }
        if (properties & NIMBLE_PROPERTY::NOTIFY) {
            Serial.print("NOTIFY ");
        }
        if (properties & NIMBLE_PROPERTY::INDICATE) {
            Serial.print("INDICATE ");
        }
        Serial.println();
    }
}
