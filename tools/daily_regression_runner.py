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

"""Daily Regression Runner, Triage, and Auto-Fix Dispatcher for libmotionphoto."""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from typing import Any, Dict, List, Optional


def run_command(
    cmd: List[str], cwd: Optional[str] = None
) -> subprocess.CompletedProcess:
  """Runs a shell command and captures stdout/stderr."""
  return subprocess.run(
      cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
  )


def parse_test_failures(output: str) -> List[Dict[str, str]]:
  """Extracts failed test cases and error messages from test output."""
  failures = []
  fail_pattern = re.compile(
      r"\[\s*FAILED\s*\]\s+([A-Za-z0-9_]+)\.([A-Za-z0-9_]+)(?::\s*(.*))?"
  )
  for line in output.splitlines():
    match = fail_pattern.search(line)
    if match:
      suite, case, msg = match.groups()
      failures.append({
          "suite": suite,
          "case": case,
          "full_name": f"{suite}.{case}",
          "message": msg.strip() if msg else "Test assertion failed.",
          "raw_line": line.strip(),
      })
  return failures


def compute_failure_fingerprint(failure: Dict[str, str]) -> str:
  """Computes a deterministic SHA256 fingerprint for deduplication."""
  payload = f"{failure['suite']}::{failure['case']}::{failure['message']}"
  return hashlib.sha256(payload.encode("utf-8")).hexdigest()[:16]


def find_recent_changes(limit: int = 5) -> List[Dict[str, str]]:
  """Retrieves recent commits / changes for culprit identification."""
  res = run_command(["git", "log", f"-n{limit}", "--oneline"])
  if res.returncode == 0 and res.stdout.strip():
    changes = []
    for line in res.stdout.strip().splitlines():
      parts = line.split(" ", 1)
      if len(parts) == 2:
        changes.append({"id": parts[0], "summary": parts[1]})
    return changes
  return []


def format_bug_report(
    failure: Dict[str, str],
    fingerprint: str,
    env_info: Dict[str, Any],
    full_log: str,
) -> str:
  """Formats a structured GitHub issue description."""
  changes = find_recent_changes()
  recent_changes_str = (
      "\n".join([f"- `{c['id']}`: {c['summary']}" for c in changes])
      if changes
      else "N/A"
  )

  report = f"""## [Daily Regression] libmotionphoto: {failure['full_name']} Failed

### 1. Failure Summary
- **Test Case**: `{failure['full_name']}`
- **Fingerprint**: `{fingerprint}`
- **Environment**: OS=`{env_info.get('os', 'Linux')}`, Sanitizer=`{env_info.get('sanitizer', 'None')}`
- **Error**: `{failure['message']}`

### 2. Recent Commits / Blamelist
{recent_changes_str}

### 3. Reproduction Command
```bash
cd motion_photo_sample_src
./build.sh && ./test.sh
```

### 4. Failure Log Snippet
```text
{failure['raw_line']}
```

### 5. Autonomous Action
An automated draft repair workflow has been triggered to generate a candidate fix pull request.
"""
  return report


def file_or_update_github_issue(
    failure: Dict[str, str], fingerprint: str, body: str
) -> Optional[str]:
  """Files a new GitHub issue or comments on an existing one using `gh` CLI."""
  if not shutil.which("gh"):
    return None

  title = f"[Daily Regression] {failure['full_name']} failed"
  label = f"regression:{fingerprint}"

  # Search for existing open issue with this fingerprint
  search_cmd = [
      "gh",
      "issue",
      "list",
      "--search",
      f"label:{label} state:open",
      "--json",
      "number",
      "--jq",
      ".[0].number",
  ]
  search_res = run_command(search_cmd)
  existing_num = search_res.stdout.strip() if search_res.returncode == 0 else ""

  if existing_num and existing_num.isdigit():
    print(
        f"[GitHub Triage] Existing issue #{existing_num} found for fingerprint"
        f" [{fingerprint}]. Appending comment..."
    )
    run_command([
        "gh",
        "issue",
        "comment",
        existing_num,
        "--body",
        (
            "⚠️ Failure reproduced in daily regression"
            f" run.\n\n```text\n{failure['raw_line']}\n```"
        ),
    ])
    return existing_num
  else:
    print(
        f"[GitHub Triage] Filing new issue for fingerprint [{fingerprint}]..."
    )
    create_cmd = [
        "gh",
        "issue",
        "create",
        "--title",
        title,
        "--body",
        body,
        "--label",
        "bug,regression",
    ]
    create_res = run_command(create_cmd)
    if create_res.returncode == 0:
      print(f"[GitHub Triage] Issue created: {create_res.stdout.strip()}")
      return create_res.stdout.strip()
  return None


def main():
  parser = argparse.ArgumentParser(
      description="libmotionphoto Daily Regression Runner & Triage"
  )
  parser.add_argument(
      "--mode",
      default="cmake",
      choices=["google3", "cmake", "github_ci"],
      help="Execution mode",
  )
  parser.add_argument("--os", default="Linux", help="Operating system")
  parser.add_argument(
      "--sanitizer",
      default="none",
      help="Sanitizer type (asan, ubsan, tsan, none)",
  )
  parser.add_argument("--commit", default="", help="Current commit / CL hash")
  parser.add_argument("--repo", default="", help="Repository name")
  parser.add_argument(
      "--auto_fix",
      action="store_true",
      default=True,
      help="Automatically trigger auto-repair on failure",
  )
  args = parser.parse_args()

  print(
      "[Daily Regression] Starting libmotionphoto regression suite"
      f" (mode={args.mode}, sanitizer={args.sanitizer})..."
  )

  test_output = ""
  returncode = 0

  if args.mode == "google3":
    blaze_cli = "/google/bin/releases/arca9-local-blaze-cli/blaze-for-agents"
    if not os.path.exists(blaze_cli):
      blaze_cli = "blaze"
    target = os.environ.get("LIBMOTIONPHOTO_G3_TARGET", ".../libmotionphoto:all")
    cmd = [
        blaze_cli,
        "test",
        target,
    ]
    res = run_command(cmd)
    test_output = res.stdout + "\n" + res.stderr
    returncode = res.returncode
  else:
    cmd = ["./test.sh"]
    cwd = (
        "motion_photo_sample_src"
        if os.path.exists("motion_photo_sample_src")
        else "."
    )
    res = run_command(cmd, cwd=cwd)
    test_output = res.stdout + "\n" + res.stderr
    returncode = res.returncode

  if returncode == 0:
    print(
        "[Daily Regression] All libmotionphoto tests passed successfully!"
        " Baseline healthy."
    )
    return 0

  print(
      f"[Daily Regression] Test failures detected (exit code {returncode})!"
      " Initiating triage..."
  )
  failures = parse_test_failures(test_output)
  if not failures:
    failures = [{
        "suite": "General",
        "case": "BuildOrExecutionFailure",
        "full_name": "General.BuildOrExecutionFailure",
        "message": "Test binary returned non-zero exit code.",
        "raw_line": test_output[:500],
    }]

  env_info = {"os": args.os, "sanitizer": args.sanitizer, "commit": args.commit}

  for failure in failures:
    fingerprint = compute_failure_fingerprint(failure)
    bug_report = format_bug_report(failure, fingerprint, env_info, test_output)
    print("\n" + "=" * 60)
    print(f"Generated Bug Report for Fingerprint [{fingerprint}]:")
    print("=" * 60)
    print(bug_report)

    # Save bug report to artifact / output file
    os.makedirs("/tmp/libmotionphoto_reports", exist_ok=True)
    report_path = f"/tmp/libmotionphoto_reports/bug_{fingerprint}.md"
    with open(report_path, "w", encoding="utf-8") as f:
      f.write(bug_report)
    print(f"Report saved to: {report_path}")

    # File or update GitHub issue if in GitHub CI
    issue_id = None
    if args.mode == "github_ci" or shutil.which("gh"):
      issue_id = file_or_update_github_issue(failure, fingerprint, bug_report)

    if args.auto_fix:
      print(
          "[Daily Regression] Dispatching auto_repair for failure:"
          f" {failure['full_name']}..."
      )
      auto_repair_script = os.path.join(
          os.path.dirname(__file__), "auto_repair.py"
      )
      if os.path.exists(auto_repair_script):
        cmd = [
            sys.executable,
            auto_repair_script,
            "--failure_json",
            json.dumps(failure),
        ]
        if issue_id:
          cmd.extend(["--issue_id", str(issue_id)])
        subprocess.run(cmd)

  return 1


if __name__ == "__main__":
  sys.exit(main())
