#ifndef OLIVE_E2E_ENVIRONMENT_H
#define OLIVE_E2E_ENVIRONMENT_H

#include "node/color/colormanager/colormanager.h"
#include "node/factory.h"
#include "node/project/serializer/serializer.h"
#include "render/diskmanager.h"

namespace olive::e2e {
// Declare before projects/tasks so their destructors run before service teardown.
struct Environment {
  Environment() {
    ColorManager::SetUpDefaultConfig();
    DiskManager::CreateInstance();
    NodeFactory::Initialize();
    ProjectSerializer::Initialize();
  }
  ~Environment() {
    ProjectSerializer::Destroy();
    NodeFactory::Destroy();
    DiskManager::DestroyInstance();
  }
};
}
#endif
