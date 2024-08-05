#include "BleLockServer.h"

BleLockServer::BleLockServer(const std::string &lockName)
    : BleLockBase(lockName) {
        memoryFilename = "/ble_lock_memory.json";
    }

BLEUUID servserviceUUID("abcd");

void BleLockServer::setup() {
    BLEDevice::init(lockName);
    macAddress = BLEDevice::getAddress().toString();

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks(this));

    pService = pServer->createService(servserviceUUID);//("ABCD");
    createCharacteristic("1234", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    pPublicCharacteristic->setCallbacks(new PublicCharacteristicCallbacks(this));
    logColor(LColor::LightBlue, F("Public characteristic callbacks set"));

    logColor(LColor::LightBlue, F("Public characteristic created"));

    pService->start();
    logColor(LColor::LightBlue, F("Service started"));

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (!pAdvertising) {
        logColor(LColor::Red, F("Failed to get advertising"));
        return;
    }
    pAdvertising->addServiceUUID(servserviceUUID);//("ABCD");
    pAdvertising->addServiceUUID(pService->getUUID()); // Advertise the service UUID
    for (const auto &pair: uniqueCharacteristics) {
        pAdvertising->addServiceUUID(pair.first); // Advertise each characteristic UUID
    }
    logColor(LColor::LightBlue, F("Advertising started"));
    pAdvertising->start();

    logColor(LColor::LightBlue, F("BLE setup complete"));

    xTaskCreate(characteristicCreationTask, "CharacteristicCreationTask", 8192, this, 1, nullptr);
    logColor(LColor::LightBlue, F("CharacteristicCreationTask created"));
    xTaskCreate(outgoingMessageTask, "OutgoingMessageTask", 8192, this, 1, nullptr);
    logColor(LColor::LightBlue, F("OutgoingMessageTask created"));

    // Create the JSON parsing queue
    incomingQueue = xQueueCreate(10, sizeof(std::string *));
    if (incomingQueue == nullptr) {
        logColor(LColor::Red, F("Failed to create JSON parsing queue"));
        return;
    }

    // Create the JSON parsing task
    xTaskCreate(parsingIncomingTask, "JsonParsingTask", 8192, this, 1, nullptr);
    logColor(LColor::LightBlue, F("JsonParsingTask created"));

    loadCharacteristicsFromMemory();

    secureConnection.LoadRSAKeys();
    loadConfirmedDevices();

}

void BleLockServer::loadCharacteristicsFromMemory() {
    logColor(LColor::LightBlue, F("Attempting to mount SPIFFS..."));

    if (SPIFFS.begin(true)) {
        logColor(LColor::LightBlue, F("SPIFFS mounted successfully."));
        File file = SPIFFS.open(memoryFilename.c_str(), FILE_READ);
        if (file) {
            logColor(LColor::LightBlue, F("File opened successfully. Parsing JSON..."));

            json doc;
            try {
                doc = json::parse(file.readString().c_str());
            } catch (json::parse_error &e) {
                logColor(LColor::Red, F("Failed to parse JSON: %s"), e.what());
                file.close();
                return;
            }

            autoincrement = doc.value("autoincrement", 0);
            logColor(LColor::LightBlue, F("Autoincrement loaded: %d"), autoincrement);

            json characteristics = doc["characteristics"];
            for (auto &kv: characteristics.items()) {
                const std::string &uuid = kv.key();
                bool confirmed = kv.value().get<bool>();
                logColor(LColor::LightBlue, F("Loading characteristic with UUID: %s, confirmed: %d"), uuid.c_str(), confirmed);

                if (!pService) {
                    logColor(LColor::Red, F("pService is null. Cannot create characteristic."));
                    continue;
                }

                NimBLECharacteristic *characteristic = pService->createCharacteristic(
                        NimBLEUUID::fromString(uuid),
                        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
                );

                if (!characteristic) {
                    logColor(LColor::Red, F("Failed to create characteristic with UUID: %s"), uuid.c_str());
                    continue;
                }

                characteristic->setCallbacks(new UniqueCharacteristicCallbacks(this, uuid));
                uniqueCharacteristics[uuid] = characteristic;
                confirmedCharacteristics[uuid] = confirmed;
            }

            json devices = doc["pairedDevices"];
            for (auto &kv: devices.items()) {
                const std::string &mac = kv.key();
                std::string uuid = kv.value();
                pairedDevices[mac] = uuid;
                logColor(LColor::LightBlue, F("Loaded paired device with MAC: %s, UUID: %s"), mac.c_str(), uuid.c_str());
            }

            file.close();
            logColor(LColor::LightBlue, F("All characteristics and paired devices loaded successfully."));
        } else {
            logColor(LColor::Red, F("Failed to open file. File may not exist."));
        }
    } else {
        logColor(LColor::Red, F("SPIFFS mount failed, attempting to format..."));
        if (SPIFFS.format()) {
            logColor(LColor::LightBlue, F("SPIFFS formatted successfully."));
            if (SPIFFS.begin()) {
                logColor(LColor::LightBlue, F("SPIFFS mounted successfully after formatting."));
            } else {
                logColor(LColor::Red, F("Failed to mount SPIFFS after formatting."));
            }
        } else {
            logColor(LColor::Red, F("SPIFFS formatting failed."));
        }
    }
}

void BleLockServer::saveCharacteristicsToMemory() {
    json doc;
    doc["autoincrement"] = autoincrement;

    json characteristics;
    for (const auto &pair: uniqueCharacteristics) {
        if (confirmedCharacteristics[pair.first]) {
            characteristics[pair.first] = true;
        }
    }
    doc["characteristics"] = characteristics;

    json devices;
    for (const auto &pair: pairedDevices) {
        devices[pair.first] = pair.second;
    }
    doc["pairedDevices"] = devices;

    File file = SPIFFS.open(memoryFilename.c_str(), FILE_WRITE, true);
    if (file) {
        file.print(doc.dump().c_str());
        logColor(LColor::LightBlue, F("saving.. %s"), doc.dump().c_str());
        file.close();
    } else {
        logColor(LColor::Red, F("Failed to open file for writing."));
    }
}


void BleLockServer::resumeAdvertising() {
    logColor(LColor::LightBlue, F("Attempting to clear and restart advertising..."));
    NimBLEAdvertising *pAdvertising = pServer->getAdvertising();

    if (!pAdvertising) {
        logColor(LColor::Red, F("Failed to get advertising object"));
        return;
    }

    if (pAdvertising->isAdvertising()) {
        logColor(LColor::Yellow, F("Advertising is currently active. Stopping..."));
        pAdvertising->stop();
    }

    if (pAdvertising->start()) {
        logColor(LColor::LightBlue, F("Advertising successfully restarted"));
    } else {
        logColor(LColor::Red, F("Failed to restart advertising"));
    }
}

void BleLockServer::handlePublicCharacteristicRead(NimBLECharacteristic *pCharacteristic, const std::string &mac) {
    if (pairedDevices.find(mac) != pairedDevices.end()) {
        // If device is already paired, provide existing UUID
        std::string existingUUID = pairedDevices[mac];
        pCharacteristic->setValue(existingUUID);
        resumeAdvertising();
        logColor(LColor::Green, F("Device already paired, provided existing UUID: %s"), existingUUID.c_str());
        auto cmd = new CreateCharacteristicCmd{existingUUID, pCharacteristic};
        if (xQueueSend(characteristicCreationQueue, &cmd, portMAX_DELAY) != pdPASS) {
            logColor(LColor::Red, F("Failed to send UUID to characteristicCreationQueue"));
            delete cmd;
        }
        return;
    }

    std::string newUUID = generateUUID();
    logColor(LColor::LightBlue, F("Generated new UUID: %s"), newUUID.c_str());
    pCharacteristic->setValue(newUUID);
    //awaitingKeys.insert(newUUID);
    pairedDevices[mac] = newUUID;
    resumeAdvertising();
    logColor(LColor::LightBlue, F(" - resumeAdvertising"));
    auto cmd = new CreateCharacteristicCmd{newUUID, pCharacteristic};
    if (xQueueSend(characteristicCreationQueue, &cmd, portMAX_DELAY) != pdPASS) {
        logColor(LColor::Red, F("Failed to send UUID to characteristicCreationQueue"));
        delete cmd;
    }
}

BleLockServer::ServerCallbacks::ServerCallbacks(BleLockServer *lock) : lock(lock) {}

void BleLockServer::ServerCallbacks::onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) {
    std::string mac = NimBLEAddress(desc->peer_ota_addr).toString();
    logColor(LColor::LightBlue, F("Device connected (mac=%s)"), mac.c_str());

    /// confirmed devices append if needed
    if (confirmedDevices.find(mac)==confirmedDevices.end())
    {
        confirmedDevices[mac] = false;
        saveConfirmedDevices();
    }

    printCharacteristics(pServer->getServiceByUUID(servserviceUUID));//("ABCD"));
}

void BleLockServer::ServerCallbacks::onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) {
    std::string mac = NimBLEAddress(desc->peer_ota_addr).toString();
    logColor(LColor::LightBlue, F("Device disconnected (mac=%s)"), mac.c_str());
}

BleLockServer::PublicCharacteristicCallbacks::PublicCharacteristicCallbacks(BleLockServer *lock) : lock(lock) {}

void BleLockServer::PublicCharacteristicCallbacks::onRead(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) {
    NimBLECharacteristicCallbacks::onRead(pCharacteristic, desc);
    auto mac = NimBLEAddress(desc->peer_ota_addr).toString();
    logColor(LColor::Green, F("PublicCharacteristicCallbacks::onRead called from %s"), mac.c_str());
    lock->handlePublicCharacteristicRead(pCharacteristic, mac);
}

BleLockServer::UniqueCharacteristicCallbacks::UniqueCharacteristicCallbacks(BleLockServer *lock, std::string uuid)
        : lock(lock), uuid(std::move(uuid)) {}

void BleLockServer::UniqueCharacteristicCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) {
    logColor(LColor::Yellow, F("UniqueCharacteristicCallbacks::onWrite called from: %s"),
             NimBLEAddress(desc->peer_ota_addr).toString().c_str());
    
    logMemory("onWrite");
    {
        std::string receivedMessage = pCharacteristic->getValue();
        logColor(LColor::LightBlue, F("Received message: %s"), receivedMessage.c_str());
        std::string peerMac = NimBLEAddress(desc->peer_ota_addr).toString();

        // Allocate memory for the received message and copy the string
        auto *receivedMessageStrAndMac = new std::tuple{new std::string(receivedMessage),
                                                        new std::string(NimBLEAddress(desc->peer_ota_addr).toString())};

        // Send the message to the JSON parsing queue
        if (xQueueSend(lock->incomingQueue, &receivedMessageStrAndMac, portMAX_DELAY) != pdPASS) {
            logColor(LColor::Red, F("Failed to send message to JSON parsing queue"));
            delete std::get<0>(*receivedMessageStrAndMac); // Delete the received message string
            delete std::get<1>(*receivedMessageStrAndMac); // Delete the MAC address string
            delete receivedMessageStrAndMac; // Delete the tuple itself
        }
    }
    logMemory("onWrite");
}

[[noreturn]] void BleLockServer::characteristicCreationTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLockServer *>(pvParameter);
    CreateCharacteristicCmd *cmd;

    while (true) {
        logColor(LColor::Green, F("characteristicCreationTask: Waiting to receive UUID from queue..."));

        if (xQueueReceive(bleLock->characteristicCreationQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            std::string uuidStr(cmd->uuid);
            logColor(LColor::Green, F("BleLock::characteristicCreationTask uuid to create: %s"), uuidStr.c_str());

            logMemory("characteristicCreationTask: Before waiting for Mutex");

            logColor(LColor::LightBlue, F("characteristicCreationTask: Waiting for Mutex"));
            xSemaphoreTake(bleLock->mutex, portMAX_DELAY);
            logColor(LColor::LightBlue, F("characteristicCreationTask: Mutex lock"));

            logMemory("characteristicCreationTask: After Mutex lock, before createCharacteristic");
            auto newCharacteristic = bleLock->pService->getCharacteristic(uuidStr);
            if (newCharacteristic == nullptr) {
                newCharacteristic = bleLock->pService->createCharacteristic(
                        NimBLEUUID::fromString(uuidStr),
                        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
                );
                logColor(LColor::LightBlue, F(" - createCharacteristic"));
            }
            printCharacteristics(bleLock->pService);

            newCharacteristic->setCallbacks(new UniqueCharacteristicCallbacks(bleLock, uuidStr));
            logColor(LColor::LightBlue, F(" - setCallbacks"));

            bleLock->uniqueCharacteristics[uuidStr] = newCharacteristic;
            bleLock->confirmedCharacteristics[uuidStr] = false;

            logColor(LColor::LightBlue, F(" - uuid memory freed"));

            bleLock->saveCharacteristicsToMemory();
            logColor(LColor::LightBlue, F(" - saveCharacteristicsToMemory"));

            bleLock->resumeAdvertising();

            xSemaphoreGive(bleLock->mutex);
            logColor(LColor::LightBlue, F("characteristicCreationTask: Mutex unlock"));

            logMemory("characteristicCreationTask: After characteristic creation");

            delete cmd;
        }
    }
}

[[noreturn]] void BleLockServer::outgoingMessageTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLockServer *>(pvParameter);
    MessageBase *responseMessage;

    logColor(LColor::LightBlue, F("Starting outgoingMessageTask..."));

    while (true) {
        logColor(LColor::LightBlue, F("outgoingMessageTask: Waiting to receive message from queue..."));

        if (xQueueReceive(bleLock->outgoingQueue, &responseMessage, portMAX_DELAY) == pdTRUE) {
            logMemory("outgoingMessageTask: Before processing message");

            logColor(LColor::LightBlue, F("Message received from queue"));

            logColor(LColor::LightBlue, F("BleLock::responseMessageTask msg: %s"), responseMessage->destinationAddress.c_str());

            logColor(LColor::LightBlue, F("outgoingMessageTask: Mutex lock"));

            xSemaphoreTake(bleLock->mutex, portMAX_DELAY); // добавлен вызов xSemaphoreTake

            auto it = bleLock->pairedDevices.find(responseMessage->destinationAddress);
            if (it != bleLock->pairedDevices.end()) {
                logColor(LColor::LightBlue, F("Destination address found in uniqueCharacteristics %s"), responseMessage->destinationAddress.c_str());

                auto characteristic = bleLock->uniqueCharacteristics[it->second];

                logColor(LColor::LightBlue, F("write to characteristic: %s"),characteristic->getUUID().toString().c_str());
                std::string serializedMessage = responseMessage->serialize();
                int partsNum = serializedMessage.length()/80 + ((serializedMessage.length()%80)?1:0);
                logColor(LColor::LightBlue, F("Serialized message: part-%d <"),partsNum);
                for (int i=0; i < partsNum; i++)
                {
                    if (((i+1)*80)<serializedMessage.length()) 
                        logColor(LColor::LightBlue, F("%s"), serializedMessage.substr(i*80,80).c_str());
                    else
                        logColor(LColor::LightBlue, F("%s"), serializedMessage.substr(i*80).c_str());
                }
                logColor(LColor::LightBlue, F("> Serialized message; "));

                const int BLE_ATT_ATTR_MAX_LEN_IN = 160;////BLE_ATT_ATTR_MAX_LEN
                if (serializedMessage.length() < BLE_ATT_ATTR_MAX_LEN_IN)
                {
                    logMemory("outgoingMessageTask: Before setValue");
                    characteristic->setValue(serializedMessage);
                    size_t nSubs = characteristic->getSubscribedCount();

                    logMemory("outgoingMessageTask: After setValue Subscribers");

                    logColor(LColor::Yellow, F("Subscribed %d"), nSubs);

                    logColor(LColor::LightBlue, F("Characteristic value set"));

                    logMemory("outgoingMessageTask: Before notify");
                    characteristic->notify();
                    logMemory("outgoingMessageTask: After notify");

                    logColor(LColor::LightBlue, F("Characteristic notified"));
                }
                else
                {// send parts
                    const TickType_t deleyPart = 10/portTICK_PERIOD_MS;
                    partsNum = serializedMessage.length()/BLE_ATT_ATTR_MAX_LEN_IN + ((serializedMessage.length()%BLE_ATT_ATTR_MAX_LEN_IN)?1:0);

                    logColor(LColor::Yellow, F("BEGIN_SEND"));
                    //vTaskDelay(deleyPart);
                    for (int i=0; i < partsNum; i++)
                    {
                        std::string subS;
                        if (((i+1)*BLE_ATT_ATTR_MAX_LEN_IN)<serializedMessage.length()) 
                            subS=(serializedMessage.substr(i*BLE_ATT_ATTR_MAX_LEN_IN,BLE_ATT_ATTR_MAX_LEN_IN));
                        else
                            subS=(serializedMessage.substr(i*BLE_ATT_ATTR_MAX_LEN_IN));
                        characteristic->setValue(subS);
                        characteristic->notify();
                        logColor(LColor::Yellow, F("PART_SEND <%s>"),subS.c_str());

                        vTaskDelay(deleyPart);
                    }
                    logColor(LColor::Yellow, F("END_SEND"));

                }
            } else {
                logColor(LColor::Red, F("Destination address not found in uniqueCharacteristics"));
            }

            delete responseMessage;
            logColor(LColor::LightBlue, F("Response message deleted"));

            logMemory("outgoingMessageTask: Before resumeAdvertising");
            bleLock->resumeAdvertising();
            logMemory("outgoingMessageTask: After resumeAdvertising");
            logColor(LColor::LightBlue, F("Advertising resumed"));

            xSemaphoreGive(bleLock->mutex);
            logColor(LColor::LightBlue, F("outgoingMessageTask: Mutex unlock"));

            logMemory("outgoingMessageTask: After processing message");
        } else {
            logColor(LColor::Red, F("Failed to receive message from queue"));
        }
    }
}

void BleLockServer::processRequestTask(void *pvParameter) {
    auto *params = static_cast<RequestTaskParams *>(pvParameter);
    auto *bleLock = params->bleLock;
    auto *requestMessage = params->requestMessage;

    logColor(LColor::LightBlue, F("processRequestTask: Начало обработки запроса"));

    // Обработайте запрос
    MessageBase *responseMessage = requestMessage->processRequest(bleLock);

    if (responseMessage) {
        logColor(LColor::LightBlue, F("Отправка ответного сообщения в исходящую очередь"));
        responseMessage->destinationAddress = requestMessage->sourceAddress;
        responseMessage->sourceAddress = requestMessage->destinationAddress;
        responseMessage->requestUUID = requestMessage->requestUUID;
        if (xQueueSend(bleLock->outgoingQueue, &responseMessage, portMAX_DELAY) != pdPASS) {
            logColor(LColor::Red, F("Не удалось отправить ответное сообщение в исходящую очередь"));
            delete responseMessage;
        }
    } else {
        auto responseMessageStr = new std::string(requestMessage->serialize());
        logColor(LColor::LightBlue, F("Отправка строкового ответа в очередь ответов"));
        if (xQueueSend(bleLock->responseQueue, &responseMessageStr, portMAX_DELAY) != pdPASS) {
            logColor(LColor::Red, F("Не удалось отправить строковое ответное сообщение в очередь ответов"));
            delete responseMessageStr;
        }
    }

    delete requestMessage; // Очистка запроса
    delete params; // Очистка параметров задачи
    vTaskDelete(nullptr); // Удаление задачи
}

std::unordered_map<std::string, std::string> BleLockServer::messageMacBuff; // map for multypart messages

[[noreturn]] void BleLockServer::parsingIncomingTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLockServer *>(pvParameter);
    std::tuple<std::string *, std::string *> *receivedMessageStrAndMac;

    while (true) {
        logColor(LColor::LightBlue, F("parsingIncomingTask: Ожидание получения сообщения из очереди..."));

        if (xQueueReceive(bleLock->incomingQueue, &receivedMessageStrAndMac, portMAX_DELAY) == pdTRUE) {
            logMemory("parsingIncomingTask: До обработки сообщения");
            auto receivedMessage = std::get<0>(*receivedMessageStrAndMac);
            auto address = std::get<1>(*receivedMessageStrAndMac);
            logColor(LColor::LightBlue, F("parsingIncomingTask: Получено сообщение: %s от MAC: %s"), receivedMessage->c_str(), address->c_str());

            bool partOfMessage = false;
            try {
                auto msg = MessageBase::createInstance(*receivedMessage);
                if (msg) {
                    msg->sourceAddress = *address;
                    logColor(LColor::LightBlue, F("Получен запрос от: %s "), msg->sourceAddress.c_str());

                    // Создайте структуру параметров задачи
                    auto *taskParams = new RequestTaskParams{bleLock, msg};

                    // Создайте новую задачу для обработки запроса
                    if (xTaskCreate(processRequestTask, "ProcessRequestTask", 8192, taskParams, 1, nullptr) != pdPASS) {
                        logColor(LColor::Red, F("Не удалось создать задачу ProcessRequestTask"));
                        delete msg;
                        delete taskParams;
                    }
                    messageMacBuff[*address].clear();
                } else {
                    partOfMessage = true;
                    logColor(LColor::Red, F("Не удалось создать экземпляр сообщения"));
                }
            } catch (const json::parse_error &e) {
                partOfMessage = true;
                logColor(LColor::Red, F("Ошибка парсинга JSON: %s"), e.what());
            } catch (const std::exception &e) {
                partOfMessage = true;
                logColor(LColor::Red, F("Произошло исключение: %s"), e.what());
            }

            if (partOfMessage)
            {
                messageMacBuff[*address] += *receivedMessage;

                try {
                    auto msg = MessageBase::createInstance(messageMacBuff[*address]);
                    if (msg) {
                        msg->sourceAddress = *address;
                        logColor(LColor::LightBlue, F("Получен запрос от: %s "), msg->sourceAddress.c_str());

                        // Создайте структуру параметров задачи
                        auto *taskParams = new RequestTaskParams{bleLock, msg};

                        // Создайте новую задачу для обработки запроса
                        if (xTaskCreate(processRequestTask, "ProcessRequestTask", 8192, taskParams, 1, nullptr) != pdPASS) {
                            logColor(LColor::Red, F("Не удалось создать задачу ProcessRequestTask"));
                            delete msg;
                            delete taskParams;
                        }
                        messageMacBuff[*address].clear();
                    } else {
                        logColor(LColor::Red, F("Не удалось создать экземпляр сообщения"));
                    }
                } catch (const json::parse_error &e) {
                    logColor(LColor::Red, F("Ошибка парсинга JSON: %s"), e.what());
                } catch (const std::exception &e) {
                    logColor(LColor::Red, F("Произошло исключение: %s"), e.what());
                }

            }
            logMemory("parsingIncomingTask: После обработки сообщения");

            // Освобождение выделенной памяти для полученного сообщения
            delete receivedMessage;
            delete address;
            delete receivedMessageStrAndMac;
            logMemory("parsingIncomingTask: После освобождения памяти");
        }
    }
}

MessageBase *BleLockServer::request(MessageBase *requestMessage, const std::string &destAddr, uint32_t timeout) const {
    requestMessage->sourceAddress = macAddress; // Use the stored MAC address
    requestMessage->destinationAddress = destAddr;
    requestMessage->requestUUID = requestMessage->generateUUID(); // Generate a new UUID for the request

    logColor(LColor::Red, F("start request"));

    if (xQueueSend(outgoingQueue, &requestMessage, portMAX_DELAY) != pdPASS) {
        logColor(LColor::Red, F("Failed to send request to the outgoing queue"));
        return nullptr;
    }

    uint32_t startTime = xTaskGetTickCount();
    std::string *receivedMessage;

    while (true) {
        uint32_t elapsed = xTaskGetTickCount() - startTime;
        if (elapsed >= pdMS_TO_TICKS(timeout)) {
            // Timeout reached
            logColor (LColor::Red, F("request timeout reached"));
            return nullptr;
        }

        // Peek at the queue to see if there is a message
        if (xQueuePeek(responseQueue, &receivedMessage, pdMS_TO_TICKS(timeout) - elapsed) == pdTRUE) {
            // Create an instance of MessageBase from the received message
            MessageBase *instance = MessageBase::createInstance(*receivedMessage);

            logColor (LColor::Red, F("request answer!: %s"), receivedMessage->c_str());

            // Check if the source address and requestUUID match
            if (instance->sourceAddress == destAddr && instance->requestUUID == requestMessage->requestUUID) {
                // Remove the item from the queue after confirming the source address and requestUUID match
                xQueueReceive(responseQueue, &receivedMessage, 0);
                delete receivedMessage; // Delete the received message pointer
                logColor (LColor::Red, F("RemoveFromQueue"));
                return instance;
            }
            else
                logColor (LColor::Red, F("FAIL RemoveFromQueue"));

            delete instance;
        }
    }

    return nullptr; // This should never be reached, but it's here to satisfy the compiler
}

std::unordered_map<std::string, bool> BleLockServer::confirmedDevices; // map for paired devices
void BleLockServer::loadConfirmedDevices()
{
    json doc;
    std::string text;

    File f = SPIFFS.open ("confirmed.json");
    if (!f)
        return;
    size_t sz = f.size();
    char buf[128];
    size_t len = sz;
    while (sz > 0)
    {
        if(sz<=128)
            len = sz;
        else
            len = 128;
        f.readBytes (buf, len);
        text += std::string(buf,len);
        sz -= len;
    }
    f.close();
    doc.parse(text,nullptr,false);
    if (doc.is_discarded())
    {
        return;
    }
    confirmedDevices.clear();
    for (auto it = doc.begin(); it!=doc.end(); it++)
    {
        BleLockServer::confirmedDevices[it.key()] = it.value();
    }

}
void BleLockServer::saveConfirmedDevices()
{
    json doc;
    std::string text;

    for (auto it = BleLockServer::confirmedDevices.begin(); it != BleLockServer::confirmedDevices.end(); it++)
    {
        doc[it->first] = it->second;
    }

    File f = SPIFFS.open ("confirmed.json","w",true);
    if (!f)
        return;
    text = doc.dump();
    f.write ((uint8_t*)text.c_str(), text.length());
    f.close();
}
