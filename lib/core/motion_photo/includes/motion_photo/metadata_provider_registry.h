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

#ifndef MOTION_PHOTO_METADATA_PROVIDER_REGISTRY_H_
#define MOTION_PHOTO_METADATA_PROVIDER_REGISTRY_H_

#include <memory>
#include <vector>

#include "motion_photo/metadata_provider.h"

namespace libmotionphoto {
namespace motion_photo {

class MetadataProviderRegistry {
 public:
  // Register a 3P provider globally.
  static void Register3pProvider(std::shared_ptr<IMetadataProvider> provider);

  // Returns all registered 3P providers.
  static const std::vector<std::shared_ptr<IMetadataProvider>>& Get3pProviders();

 private:
  static std::vector<std::shared_ptr<IMetadataProvider>>& Registry();
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_METADATA_PROVIDER_REGISTRY_H_
