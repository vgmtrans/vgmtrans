#!/usr/bin/env python3
"""Compare two value-core shell builds against a private, newline-separated input list.

Retain bytes, raw logs, exit statuses, and a JSON comparison. Differences are
reported strictly; deliberate fixes and partial failures need separate review.
This compares revisions of the value core, not value versus legacy conversion.
"""

import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess


POLICIES = {
    "synth": "--loops 0",
    "events": "--loops 0 --modulation events --used-instruments --pitch-transitions pitch-bend --tuning rpn",
}


def file_hash(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def run(binary, source, directory, options, timeout):
    directory.mkdir(parents=True)
    exports = directory / "exports"
    command = f'export all "{exports}" midi sf2 dls {options}'
    result = {"status": "timeout"}
    try:
        process = subprocess.run(
            [str(binary), "-c", "assets", "-c", "collections", "-c", "diagnostics", "-c", command, "--", str(source)],
            capture_output=True, timeout=timeout,
        )
        result["status"] = process.returncode
        stdout, stderr = process.stdout, process.stderr
    except subprocess.TimeoutExpired as error:
        stdout, stderr = error.stdout or b"", error.stderr or b""
    for name, data in (("stdout", stdout), ("stderr", stderr)):
        (directory / f"{name}.log").write_bytes(data)
        result[name] = data.decode("utf-8", errors="replace").replace(str(exports), "<exports>")
    result["files"] = {
        str(path.relative_to(exports)): file_hash(path)
        for path in sorted(exports.rglob("*")) if path.is_file()
    }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("inputs", type=Path, help="one source path per line")
    parser.add_argument("output", type=Path, help="new directory for retained results")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    binaries = [args.baseline.resolve(strict=True), args.candidate.resolve(strict=True)]
    sources = [Path(line).resolve(strict=True) for line in args.inputs.read_text().splitlines() if line]
    if not sources or args.jobs < 1 or args.timeout <= 0:
        parser.error("provide inputs, positive jobs, and a positive timeout")
    output = args.output.resolve()
    if '"' in str(output) or "\n" in str(output):
        parser.error("the output path cannot contain a quote or newline")
    output.mkdir(parents=True, exist_ok=False)
    (output / "manifest.json").write_text(json.dumps({
        "binaries": [{"path": str(path), "sha256": file_hash(path)} for path in binaries],
        "inputs": [{"path": str(path), "sha256": file_hash(path)} for path in sources],
        "policies": POLICIES, "timeout_seconds": args.timeout,
    }, indent=2))

    def compare(job):
        index, source, policy = job
        directory = output / f"{index:03d}-{policy}"
        before, after = [run(binary, source, directory / label, POLICIES[policy], args.timeout)
                         for binary, label in zip(binaries, ("before", "after"))]
        names = before["files"].keys() | after["files"].keys()
        changes = sorted(name for name in names if before["files"].get(name) != after["files"].get(name))
        result = {"source": str(source), "policy": policy, "before": before, "after": after,
                  "changed_files": changes,
                  "same_status": before["status"] == after["status"] != "timeout",
                  "same_stdout": before["stdout"] == after["stdout"],
                  "same_stderr": before["stderr"] == after["stderr"]}
        (directory / "comparison.json").write_text(json.dumps(result, indent=2))
        print(f"{index:03d} {policy}: {len(changes)} changed files; "
              f"status {before['status']} / {after['status']}; {source.name}", flush=True)
        return result

    jobs = [(index, source, policy) for index, source in enumerate(sources) for policy in POLICIES]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(compare, jobs))
    summary = {"inputs": len(sources), "comparisons": len(results),
               "baseline_files": sum(len(row["before"]["files"]) for row in results),
               "changed_files": sum(len(row["changed_files"]) for row in results),
               "different_statuses": sum(not row["same_status"] for row in results),
               "different_logs": sum(not row["same_stdout"] or not row["same_stderr"] for row in results),
               "baseline_nonzero_statuses": sum(row["before"]["status"] != 0 for row in results)}
    (output / "summary.json").write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))
    return int(any(summary[key] for key in ("changed_files", "different_statuses", "different_logs")))


if __name__ == "__main__":
    raise SystemExit(main())
