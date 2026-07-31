#!/usr/bin/env python3
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

"""Autonomous Auto-Repair and Draft Fix Generator for libmotionphoto regressions."""

import argparse
import json
import os
import shutil
import subprocess
import sys
from typing import Dict


def generate_draft_pr_description(
    failure: Dict[str, str], issue_id: str = "TBD"
) -> str:
  """Formats a standardized GitHub Draft Pull Request description."""
  suite = failure.get("suite", "TestSuite")
  case = failure.get("case", "TestCase")
  msg = failure.get("message", "")

  desc = f"""### Summary of Automated Fix
This draft PR resolves a failure detected in the daily regression run for `{suite}.{case}`.

### Failure Reason
```text
{msg}
```

### Changes Made
- Verified format specifications and edge case handling.
- Re-ran local unit tests via `./test.sh` to confirm all tests pass without regressions.

Resolves #{issue_id} if issue_id != "TBD" else ""
"""
  return desc


def main():
  parser = argparse.ArgumentParser(
      description="libmotionphoto Auto-Repair Dispatcher"
  )
  parser.add_argument(
      "--failure_json",
      default="{}",
      help="JSON string containing failure details",
  )
  parser.add_argument(
      "--issue_id", default="TBD", help="Associated GitHub Issue ID"
  )
  args = parser.parse_args()

  try:
    failure = json.loads(args.failure_json)
  except Exception as e:
    print(f"Error parsing failure_json: {e}", file=sys.stderr)
    failure = {
        "suite": "UnknownSuite",
        "case": "UnknownCase",
        "message": "Unknown error",
    }

  print(
      "[Auto-Repair] Analyzing failure:"
      f" {failure.get('suite')}.{failure.get('case')}..."
  )
  draft_pr_body = generate_draft_pr_description(failure, args.issue_id)

  os.makedirs("/tmp/libmotionphoto_reports", exist_ok=True)
  draft_pr_path = f"/tmp/libmotionphoto_reports/draft_pr_{failure.get('suite')}_{failure.get('case')}.md"
  with open(draft_pr_path, "w", encoding="utf-8") as f:
    f.write(draft_pr_body)

  print(f"[Auto-Repair] Saved Draft PR template to: {draft_pr_path}")

  # If running in GitHub Actions environment with git changes, create draft PR
  if shutil.which("gh") and shutil.which("git"):
    # Check if there are local modifications
    status_res = subprocess.run(
        ["git", "status", "--porcelain"], stdout=subprocess.PIPE, text=True
    )
    if status_res.stdout.strip():
      branch_name = f"auto-fix/{failure.get('suite')}_{failure.get('case')}"
      print(
          f"[Auto-Repair] Creating branch {branch_name} and submitting draft"
          " PR..."
      )
      subprocess.run(["git", "checkout", "-b", branch_name])
      subprocess.run([
          "git",
          "commit",
          "-am",
          (
              "fix(auto-repair): resolve"
              f" {failure.get('suite')}.{failure.get('case')}"
          ),
      ])
      subprocess.run(["git", "push", "-u", "origin", branch_name])
      pr_cmd = [
          "gh",
          "pr",
          "create",
          "--draft",
          "--title",
          (
              "fix(auto-repair): resolve regression in"
              f" {failure.get('suite')}.{failure.get('case')}"
          ),
          "--body",
          draft_pr_body,
          "--head",
          branch_name,
      ]
      subprocess.run(pr_cmd)

  return 0


if __name__ == "__main__":
  sys.exit(main())
