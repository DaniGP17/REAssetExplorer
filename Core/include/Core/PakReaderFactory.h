#ifndef REASSETEXPLORER_PAKREADERFACTORY_H
#define REASSETEXPLORER_PAKREADERFACTORY_H
#include <memory>

#include "IPakReader.h"

std::unique_ptr<IPakReader> CreatePakReader(PakVersions version);

#endif
