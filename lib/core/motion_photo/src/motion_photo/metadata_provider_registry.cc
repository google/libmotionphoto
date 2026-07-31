// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "motion_photo/metadata_provider_registry.h"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "motion_photo/metadata_provider.h"
#include "plugins/legacy_micro_video/legacy_micro_video_provider.h"

namespace libmotionphoto {
namespace motion_photo {

static std::mutex g_registry_mutex;

// static
void MetadataProviderRegistry::Register3pProvider(
    std::shared_ptr<IMetadataProvider> provider) {
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  Registry().push_back(std::move(provider));
}

// static
const std::vector<std::shared_ptr<IMetadataProvider>>&
MetadataProviderRegistry::Get3pProviders() {
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  return Registry();
}

// static
std::vector<std::shared_ptr<IMetadataProvider>>&
MetadataProviderRegistry::Registry() {
  static auto* registry =
      new std::vector<std::shared_ptr<IMetadataProvider>>{
          std::make_shared<LegacyMicroVideoProvider>(),
      };
  return *registry;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
