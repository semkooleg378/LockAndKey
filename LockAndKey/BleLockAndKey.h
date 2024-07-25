#ifndef BLELOCKANDKEY_H
#define BLELOCKANDKEY_H

#include <BleLockServer.h>
#include <BleLockClient.h>

BleLockBase * createAndInitLock (bool isServer, std::string name);

#endif
