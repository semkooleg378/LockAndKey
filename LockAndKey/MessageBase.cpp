#include "MessageBase.h"
#include <sstream>
#include <utility>
#include <random>
#include "Arduino.h"

std::unordered_map<int, std::string> IntSAtringMap::iToS;
std::unordered_map<std::string, int> IntSAtringMap::sToI;

std::unordered_map<MessageType, MessageBase::Constructor> MessageBase::constructors;

void MessageBase::registerConstructor(const MessageType& type, Constructor constructor) {
    constructors[type] = std::move(constructor);
}

MessageBase* MessageBase::createInstance(const std::string& input) {
    Serial.println("Try parsing");
    try {
        MessageBase mBase;
        mBase.deserialize(input);
        if ( /*MessageType::typeError*/MessageTypeError==mBase.type)
            return nullptr;
        auto it = constructors.find(mBase.type);
        if (it != constructors.end()) {
            Serial.println("Constructor found");
            MessageBase* instance = it->second();
            instance->deserialize(input);
            if ( MessageTypeError==instance->type)
            {                
                delete instance;
                return nullptr;
            }
            return instance;
        } else {
            Serial.println("Unknown message type.");
            return nullptr;
        }
    } catch (json::parse_error& e) {
        Serial.printf("Failed to parse JSON: %s\n", e.what());
        return nullptr;
    } catch (std::exception& e) {
        Serial.printf("Exception: %s\n", e.what());
        return nullptr;
    } catch (...) {
        Serial.println("Unknown error occurred.");
        return nullptr;
    }
}


std::string MessageBase::serialize() {
    json doc;
    doc["sourceAddress"] = sourceAddress;
    doc["destinationAddress"] = destinationAddress;
    doc["type"] = IntSAtringMap::findString(type);
    doc["requestUUID"] = requestUUID; // Serialize the request UUID

    serializeExtraFields(doc);
    return {doc.dump()};
}


void MessageBase::deserialize(const std::string& input) {
//    auto doc = json::parse(input);
    try {
        Serial.printf("Parse JSON: <<%s>>\n",input.c_str());
        auto doc = json::parse(input,nullptr,false);
        if (doc.is_discarded())
        {
            Serial.println("parsing error");
            type = MessageTypeError;
            return;
        }

        sourceAddress = doc["sourceAddress"];
        destinationAddress = doc["destinationAddress"];
    //    Serial.println(doc["type"].get<std::string>().c_str());
        type = IntSAtringMap::findInt (doc["type"]);
    //    if (type==MessageType::reqRegKey)
    //        Serial.println("reqReg!!!");
        requestUUID = doc["requestUUID"]; // Deserialize the request UUID

        deserializeExtraFields(doc);    
    } catch (json::parse_error& e) {
        Serial.printf("Failed to parse JSON: %s\n", e.what());
            type = MessageTypeError;
    } catch (std::exception& e) {
        Serial.printf("Exception: %s\n", e.what());
            type = MessageTypeError;
    } catch (...) {
        Serial.println("Unknown error occurred.");
            type = MessageTypeError;
    }
}

std::string MessageBase::generateUUID() {
    static std::random_device rd;
    static std::mt19937 generator(rd());
    static std::uniform_int_distribution<uint32_t> distribution(0, 0xFFFFFFFF);

    std::ostringstream oss;
    oss << std::hex << std::setw(8) << std::setfill('0') << distribution(generator);
    return oss.str();
}

////////////////////
void IntSAtringMap::insert (int i, std::string s)
{
    iToS[i] = s;
    sToI[s] = i;
}

int IntSAtringMap::findInt (std::string s)
{
    if (sToI.find(s) == sToI.end())
    {
        Serial.println((s + "  Type not found - return ERROR!").c_str());
        return -1;
    }
    return sToI[s];
}
std::string IntSAtringMap::findString (int i)
{
    if (iToS.find(i) == iToS.end())
    {
        Serial.print (i);
        Serial.println("  Type not found - return ERROR!");
        return "";
    }
    return iToS[i];
}
