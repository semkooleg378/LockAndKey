
#include <BleLockAndKey.h>

BleLockBase * createAndInitLock (bool isServer, std::string name)
{
	BleLockBase * result = nullptr;

	logColorBegin();

	if (isServer)
	{
		logColor (LColor::Green, F("Xreate Server"));
		result = new BleLockServer (name);
	}
	else
	{
		logColor (LColor::Green, F("Create Client"));
		result = new BleLockClient (name);
		((BleLockClient*)result)->regServer.deserialize();
	}
	logColor (LColor::Green, F("Setup BLE"));
	result->setup();
	return result;
}

unsigned char publicKey[1600];
unsigned char privateKey[3200]; // Увеличим размер буфера для приватного ключа