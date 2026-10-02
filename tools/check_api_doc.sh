#!/bin/bash
# Copyright 2026 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Determine VCS
if [ -d .git ]; then
  # Git
  # For PRs in GitHub Actions, we can compare against main
  TARGET_BRANCH=${GITHUB_BASE_REF:-main}
  echo "Checking changes against $TARGET_BRANCH (Git)"
  CHANGED_FILES=$(git diff --name-only origin/$TARGET_BRANCH...HEAD)
elif [ -d .hg ] || [ -f .hg/requires ] || hg root >/dev/null 2>&1; then
  # Mercurial (Fig)
  echo "Checking changes in current commit (Mercurial)"
  CHANGED_FILES=$(hg status --change . --no-status)
else
  echo "Unknown VCS, skipping check."
  exit 0
fi

JAVA_CHANGED=false
DOC_CHANGED=false

JAVA_PATH_PATTERN="lib/java/src/main/java/com/google/libmotionphoto/motionphoto/[^/]+\.java"
DOC_PATH="lib/java/java_api_description.md"

for file in $CHANGED_FILES; do
  # Strip arbitrary prefixes to make path relative to repository root
  clean_file=$(echo "$file" | sed -E 's|^(.*[/\\])?(lib/java/.*)|\2|')
  if [[ "$clean_file" =~ $JAVA_PATH_PATTERN ]]; then
    JAVA_CHANGED=true
    echo "Detected Java API change: $clean_file"
  fi
  if [[ "$clean_file" == "$DOC_PATH" ]]; then
    DOC_CHANGED=true
    echo "Detected Doc change: $clean_file"
  fi
done

if [ "$JAVA_CHANGED" = true ] && [ "$DOC_CHANGED" = false ]; then
  echo "ERROR: Java API changed but java_api_description.md was not updated."
  exit 1
fi

echo "API doc check passed."
exit 0
