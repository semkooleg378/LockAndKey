#include "BleLockClient.h"


BLEUUID serviceUUID("abcd");
BLEUUID publicCharUUID("1234");

// To convert RegServerKey to JSON
void to_json(json &j, const RegServerKey &rsk) {
    j = json{{"macAdr", rsk.macAdr}, {"characteristicUUID", rsk.characteristicUUID}};
}

void from_json(const json &j, RegServerKey &rsk) {
    j.at("macAdr").get_to(rsk.macAdr);
    j.at("characteristicUUID").get_to(rsk.characteristicUUID);
}

ServerReg BleLockClient::regServer;


BleLockClient::BleLockClient(const std::string &lockName)
    : BleLockBase(lockName) {
            memoryFilename = "/ble_key_memory.json";
                pClient = nullptr;
                clientCbk = nullptr;
                advDevice = nullptr;
                pUniqueCharExt = nullptr;
                pScan = nullptr;
    }

void BleLockClient::setup() {
    logColor(LColor::Green,F("Starting BLE setup..."));

    BLEDevice::init(lockName);
    logColor(LColor::Green,F("BLEDevice::init completed"));

    // Get the MAC address and store it
    macAddress = BLEDevice::getAddress().toString();
    logColor(LColor::Green,F("Device MAC address: %s"), macAddress.c_str());

    // Create the JSON parsing queue
    jsonParsingQueue = xQueueCreate(10, sizeof(std::string *));
    if (jsonParsingQueue == nullptr) {
        logColor(LColor::Red,F("Failed to create JSON parsing queue"));
        return;
    }

    // Create the JSON parsing task
    xTaskCreate(jsonParsingTask, "JsonParsingTask", 8192, this, 1, nullptr);
    logColor(LColor::Green,F("JsonParsingTask created"));
    
    xTaskCreate(outgoingMessageTask, "outgoingMessageTask", 8192, this, 1, nullptr);
    logColor(LColor::Green,F("outgoingMessageTask created"));

//    loadCharacteristicsFromMemory();
    secureConnection.LoadRSAKeys();
}

MessageBase *BleLockClient::request(MessageBase *requestMessage, const std::string &destAddr, uint32_t timeout) const {
    requestMessage->sourceAddress = macAddress; // Use the stored MAC address
    requestMessage->destinationAddress = destAddr;
    requestMessage->requestUUID = requestMessage->generateUUID(); // Generate a new UUID for the request

    if (xQueueSend(outgoingQueue, &requestMessage, portMAX_DELAY) != pdPASS) {
        logColor(LColor::Red,F("Failed to send request to the outgoing queue"));
        return nullptr;
    }

    uint32_t startTime = xTaskGetTickCount();
    std::string *receivedMessage;

    while (true) {
        uint32_t elapsed = xTaskGetTickCount() - startTime;
        if (elapsed >= pdMS_TO_TICKS(timeout)) {
            // Timeout reached
            return nullptr;
        }

        // Peek at the queue to see if there is a message
        if (xQueuePeek(responseQueue, &receivedMessage, pdMS_TO_TICKS(timeout) - elapsed) == pdTRUE) {
            // Create an instance of MessageBase from the received message
            MessageBase *instance = MessageBase::createInstance(*receivedMessage);

            // Check if the source address and requestUUID match
            if (instance->sourceAddress == destAddr && instance->requestUUID == requestMessage->requestUUID) {
                // Remove the item from the queue after confirming the source address and requestUUID match
                xQueueReceive(responseQueue, &receivedMessage, 0);
                delete receivedMessage; // Delete the received message pointer
                return instance;
            }
            delete instance;
        }
    }

    return nullptr; // This should never be reached, but it's here to satisfy the compiler
}

std::unordered_map<std::string, std::string> BleLockClient::messageControll; // map for multypart messages

bool BleLockClient::isConnected=false;

[[noreturn]] void BleLockClient::outgoingMessageTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLockClient *>(pvParameter);
    MessageBase *responseMessage;
    //extern NimBLERemoteCharacteristic *pUniqueCharExt;

    logColor(LColor::Green,F("Starting outgoingMessageTask..."));
 
    while (true) {
        if (!isConnected)
        logColor(LColor::LightBlue,F("outgoingMessageTask: Waiting to receive message from queue..."));

        if (xQueueReceive(bleLock->outgoingQueue, &responseMessage, portMAX_DELAY) == pdTRUE) {
            logColor(LColor::Green, F("Outgoing queue begin Free heap memory :  %d bytes"), esp_get_free_heap_size());

            logColor(LColor::LightBlue,F("Message received from queue"));

            logColor(LColor::LightBlue,F("BleLock::responseMessageTask msg: %s"), responseMessage->destinationAddress.c_str());

            logColor(LColor::LightBlue,F("outgoingMessageTask: Mutex lock"));

            //auto it = bleLock->pairedDevices.find(responseMessage->destinationAddress);
            if (1)//it != bleLock->pairedDevices.end()) 
            {
                logColor(LColor::LightBlue,F("Destination address found in uniqueCharacteristics %s"),
                            responseMessage->destinationAddress.c_str());

                auto characteristic = bleLock->pUniqueCharExt;// bleLock->uniqueCharacteristics[it->second];
                std::string serializedMessage = responseMessage->serialize();
                logColor(LColor::LightBlue,F("Serialized message: %s"), serializedMessage.c_str());

                //extern volatile bool isDoWrite;
                
                //while (isDoWrite);
                //isDoWrite = true;

                logColor(LColor::LightBlue,F("Characteristic value set (%d)...."),characteristic!=nullptr);
                const int BLE_ATT_ATTR_MAX_LEN_IN = 160;////BLE_ATT_ATTR_MAX_LEN
                if (serializedMessage.length() < BLE_ATT_ATTR_MAX_LEN_IN)
                {
                    characteristic->writeValue(serializedMessage);
                }
                else
                {
                    const TickType_t deleyPart = 10/portTICK_PERIOD_MS;
                    int partsNum = serializedMessage.length()/BLE_ATT_ATTR_MAX_LEN_IN + ((serializedMessage.length()%BLE_ATT_ATTR_MAX_LEN_IN)?1:0);

                    logColor(LColor::Yellow, F("BEGIN_SEND"));
                    //vTaskDelay(deleyPart);
                    for (int i=0; i < partsNum; i++)
                    {
                        std::string subS;
                        if (((i+1)*BLE_ATT_ATTR_MAX_LEN_IN)<serializedMessage.length()) 
                            subS=(serializedMessage.substr(i*BLE_ATT_ATTR_MAX_LEN_IN,BLE_ATT_ATTR_MAX_LEN_IN));
                        else
                            subS=(serializedMessage.substr(i*BLE_ATT_ATTR_MAX_LEN_IN));
                        characteristic->writeValue(subS);
                        logColor(LColor::Yellow, F("PART_SEND <%s>"),subS.c_str());

                       if ((i+1)!=partsNum)
                        vTaskDelay(deleyPart);
                    }
                    //logColor(LColor::Yellow, F("END_SEND"));
                }
            } else {
                logColor(LColor::Red, F("Destination address not found in uniqueCharacteristics"));
            }

            delete responseMessage;
            logColor(LColor::LightBlue,F("Response message deleted"));

            //bleLock->resumeAdvertising();
            //Log.verbose(F("Advertising resumed"));

            // Unlock the mutex for advertising and characteristic operations
            //xSemaphoreGive(bleLock->bleMutex);
            logColor(LColor::LightBlue,F("outgoingMessageTask: Mutex unlock"));
            logColor(LColor::Green, F("Outgoing queue end Free heap memory :  %d bytes"), esp_get_free_heap_size());

        } else {
            logColor(LColor::Red,F("Failed to receive message from queue"));
        }
    }
}

[[noreturn]] void BleLockClient::jsonParsingTask(void *pvParameter) {
    auto *bleLock = static_cast<BleLockClient *>(pvParameter);
    std::tuple<std::string *, std::string *> *receivedMessageStrAndMac;

    while (true) {
        logColor(LColor::LightBlue,F("parsingIncomingTask: Waiting to receive message from queue..."));

        if (xQueueReceive(bleLock->jsonParsingQueue, &receivedMessageStrAndMac, portMAX_DELAY) == pdTRUE) {
            logColor(LColor::Green, F("parsingIncomingTask begin Free heap memory :  %d bytes"), esp_get_free_heap_size());
            auto receivedMessage = std::get<0>(*receivedMessageStrAndMac);
            auto address = std::get<1>(*receivedMessageStrAndMac);
            logColor(LColor::LightBlue,F("parsingIncomingTask: Received message: %s from mac: %s"), receivedMessage->c_str(),
                        address->c_str());

            bool isPartOfMessage = false;

            logColor(LColor::Yellow,F("Try message parse "));
            try {
                auto msg = MessageBase::createInstance(*receivedMessage);
                if (msg) {
                    msg->sourceAddress = *address;
                    logColor(LColor::LightBlue,F("Received request from: %s "), msg->sourceAddress.c_str());

                    MessageBase *responseMessage = msg->processRequest(bleLock);
                    messageControll[*address].clear();
                    if (responseMessage) {
                        Log.verbose(F("Sending response message to outgoing queue"));
                        responseMessage->destinationAddress = msg->sourceAddress;
                        responseMessage->sourceAddress = msg->destinationAddress;
                        responseMessage->requestUUID = msg->requestUUID;
                        if (xQueueSend(bleLock->outgoingQueue, &responseMessage, portMAX_DELAY) != pdPASS) {
                            logColor(LColor::Red,F("Failed to send response message to outgoing queue"));
                            delete responseMessage;
                        }
                    } else if (msg->type != MessageType::resOk && msg->type != MessageType::resKey && msg->type != MessageType::ReceivePublic) {
                        auto responseMessageStr = new std::string(*receivedMessage);
                        logColor(LColor::LightBlue,F("Sending response message string to response queue"));
                        if (xQueueSend(bleLock->responseQueue, &responseMessageStr, portMAX_DELAY) != pdPASS) {
                            logColor(LColor::Red,F("Failed to send response message string to response queue"));
                            delete responseMessageStr;
                        }
                    }
                    delete msg; // Make sure to delete the msg after processing
                } else {
                    isPartOfMessage=true;
                    logColor(LColor::Red,F("Failed to create message instance"));
                }
            } catch (const json::parse_error &e) {
                isPartOfMessage=true;
                logColor(LColor::LightBlue,F("JSON parse error: %s"), e.what());
            } catch (const std::exception &e) {
                isPartOfMessage=true;
                logColor(LColor::LightBlue,F("Exception occurred: %s"), e.what());
            }
            catch (...) {
                isPartOfMessage=true;
                logColor(LColor::LightBlue,F("Exception occurred!!!"));
            }

            if (isPartOfMessage)
            {
                logColor(LColor::Yellow,F("Try message parse all "));
                messageControll[*address]+=*receivedMessage;

                try {
                    auto msg = MessageBase::createInstance(messageControll[*address]);
                    if (msg) {
                        msg->sourceAddress = *address;
                        logColor(LColor::LightBlue,F("Received request from: %s "), msg->sourceAddress.c_str());

                        MessageBase *responseMessage = msg->processRequest(bleLock);
                        messageControll[*address].clear();
                        if (responseMessage) {
                            logColor(LColor::LightBlue,F("Sending response message to outgoing queue"));
                            responseMessage->destinationAddress = msg->sourceAddress;
                            responseMessage->sourceAddress = msg->destinationAddress;
                            responseMessage->requestUUID = msg->requestUUID;
                            if (xQueueSend(bleLock->outgoingQueue, &responseMessage, portMAX_DELAY) != pdPASS) {
                                logColor(LColor::LightBlue,F("Failed to send response message to outgoing queue"));
                                delete responseMessage;
                            }
                        } else if (msg->type != MessageType::resOk && msg->type != MessageType::resKey && msg->type != MessageType::ReceivePublic) {
                            auto responseMessageStr = new std::string(*receivedMessage);
                            logColor(LColor::LightBlue,F("Sending response message string to response queue"));
                            if (xQueueSend(bleLock->responseQueue, &responseMessageStr, portMAX_DELAY) != pdPASS) {
                                logColor(LColor::LightBlue,F("Failed to send response message string to response queue"));
                                delete responseMessageStr;
                            }
                        }
                        delete msg; // Make sure to delete the msg after processing
                    } else {
                        logColor(LColor::LightBlue,F("Failed to create message instance"));
                    }
                } catch (const json::parse_error &e) {
                    logColor(LColor::LightBlue,F("JSON parse error: %s"), e.what());
                } catch (const std::exception &e) {
                    logColor(LColor::LightBlue,F("Exception occurred: %s"), e.what());
                }
                catch (...) {
                    logColor(LColor::LightBlue,F("Exception occurred!!!"));
                }

            }

            logColor(LColor::Green, F("parsingIncomingTask Free heap memory :  %d bytes"), esp_get_free_heap_size());

            // Free the allocated memory for the received message
            delete receivedMessage;
            delete address;
            delete receivedMessageStrAndMac;
            logColor(LColor::Green, F("parsingIncomingTask end Free heap memory :  %d bytes"), esp_get_free_heap_size());
        }
    }
}



CommandManager BleLockClient::commandManager;


void BleLockClient::handlePublicCharacteristicRead(NimBLECharacteristic *pCharacteristic, const std::string &mac) {
    // Handle the public characteristic read specific to client
}

BleLockClient::ClientCallbacks::ClientCallbacks(BleLockClient *lock) : lock(lock) {}

void BleLockClient::ClientCallbacks::onConnect(NimBLEClient *pClient) {
        logColor(LColor::Green,F("Connected to the server."));
        BleLockClient::commandManager.sendCommand("onConnect");
        BleLockClient::isConnected = true;
}

void BleLockClient::ClientCallbacks::onDisconnect(NimBLEClient *pClient) {
        BleLockClient::isConnected = false;
        logColor(LColor::Green,F("Disconnected from the server."));
        BleLockClient::commandManager.sendCommand("onDisconnect");
}

BleLockClient * BleLockClient::currentClient = nullptr;


static void onNotify(NimBLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) {
    logColor(LColor::Yellow,F("onNotify NimBLERemoteCharacteristic"));
    std::string data((char *)pData, length);
    logColor(LColor::Yellow, F("Notification: %s"), data.c_str());
    BleLockClient *key = BleLockClient::GetCurrentClient();

    std::string *addr = new std::string(key->servMac);
    std::tuple<std::string *, std::string *> *messageAndMac = new std::tuple<std::string *, std::string *>(new std::string(data), addr);

    if (xQueueSend(key->jsonParsingQueue, &messageAndMac, portMAX_DELAY) != pdPASS) {
        logColor(LColor::Yellow, F("Notification NOT stored"));
    } else {
        logColor(LColor::Yellow, F("Notification stored"));
    }
}

void BleLockClient::connectToServer() {
    logColor(LColor::Green, F("Attempting to connect to server"));
    if (pClient == nullptr) {
        pClient = NimBLEDevice::createClient();
    }

    if (clientCbk == nullptr) {
        clientCbk = new BleLockClient::ClientCallbacks(this);
    }
    pClient->setClientCallbacks(clientCbk, false);

    if (pClient->connect(advDevice)) {
        logColor(LColor::Green, F("Connected to server"));

        NimBLERemoteService *pService = pClient->getService(serviceUUID);
        if (pService) {
            NimBLERemoteCharacteristic *pPublicChar = pService->getCharacteristic(publicCharUUID);
            if (pPublicChar) 
            {
                std::string data = pPublicChar->readValue();
                uniqueUUID = data;

                std::string mac = advDevice->getAddress().toString();
                RegServerKey reg;
                reg.characteristicUUID = uniqueUUID;
                reg.macAdr = mac;
                servMac = mac;

                regServer.insert(mac, reg);
                logColor(LColor::Yellow, F("Registered server: MAC - %s, UUID - %s"), mac.c_str(), uniqueUUID.c_str());


                auto pUniqueChar = pService->getCharacteristic(uniqueUUID);
                if (pUniqueChar) {
                    logColor(LColor::LightCyan, F("Attempting to subscribe to characteristic: %s"), uniqueUUID.c_str());
                    bool success = pUniqueChar->subscribe(true, onNotify);
                    if (success) {
                        logColor(LColor::Green, F("Subscribed to unique characteristic %s"), uniqueUUID.c_str());
                        pUniqueCharExt = pUniqueChar;
                    } else {
                        logColor(LColor::Red, F("Subscription to characteristic %s failed, reconnecting..."), uniqueUUID.c_str());
                        pClient->disconnect();
                    }
                } else {
                    logColor(LColor::Red, F("Characteristic %s not found, reconnecting..."), uniqueUUID.c_str());
                    pClient->disconnect();
                }
            } else {
                logColor(LColor::Red, F("Public characteristic %s not found, reconnecting..."), publicCharUUID.toString().c_str());
                pClient->disconnect();
            }
        } else {
            logColor(LColor::Red, F("Service %s not found, reconnecting..."), serviceUUID.toString().c_str());
            pClient->disconnect();
        }
    } else {
        logColor(LColor::Red, F("Connection to server failed"));
    }
}

std::unordered_map<std::string, KeyStatusType> BleLockClient::Locks;

void BleLockClient::StartScan ()
{
    BleLockClient::GetCurrentClient()->pScan = NimBLEDevice::getScan();

    commandManager.registerHandler("connectToServer", []() {
        BleLockClient::GetCurrentClient()->connectToServer();
    });

    commandManager.registerHandler("rescanToServer", []() {
        logColor(LColor::Green, F("Handled rescanToServer event"));

        BleLockClient::GetCurrentClient()->pScan->setAdvertisedDeviceCallbacks(new BleLockClient::AdvertisedDeviceCallbacks());
        BleLockClient::GetCurrentClient()->pScan->setActiveScan(true);
        BleLockClient::GetCurrentClient()->pScan->start(30, false);
    });

    commandManager.registerHandler("onConnect", []() {
        logColor(LColor::Green, F("Handled onConnect event"));
        if (BleLockClient::GetCurrentClient()->pUniqueCharExt!=nullptr)
        {
            BleLockClient::Locks[BleLockClient::GetCurrentClient()->servMac] = KeyStatusType::statusNone;
            auto trio = BleLockClient::GetCurrentClient()->secureConnection.keys.find(BleLockClient::GetCurrentClient()->servMac);
            if (trio != BleLockClient::GetCurrentClient()->secureConnection.keys.end())
                BleLockClient::Locks[BleLockClient::GetCurrentClient()->servMac] = KeyStatusType::statusPublickKeyExist;
        }
    });

    commandManager.registerHandler("onDisconnect", []() {
        logColor(LColor::Yellow, F("Handled onDisconnect event, attempting to reconnect..."));
        commandManager.sendCommand("rescanToServer");
    });

    commandManager.startProcessing();

    BleLockClient::GetCurrentClient()->pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    BleLockClient::GetCurrentClient()->pScan->setActiveScan(true);
    BleLockClient::GetCurrentClient()->pScan->start(30, false);
}

