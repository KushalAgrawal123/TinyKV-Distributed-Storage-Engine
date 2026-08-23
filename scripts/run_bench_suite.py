#!/usr/bin/env python3
"""Runs tinykv-bench across a matrix of client counts (default: the
project's own 10/100/1000 target) and prints one combined summary table.

Pure orchestration - the actual load generation happens in the C++
tinykv-bench binary, kept that way so interpreter overhead never leaks
into the latency numbers it measures.
"""

import argparse
import csv
import os
import subprocess
import sys
import time


def project_root():
    return os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def run_one(binary, host, port, clients, requests_per_client, set_get_ratio, value_size, csv_path):
    cmd = [
        binary,
        "--host", host,
        "--port", str(port),
        "--clients", str(clients),
        "--requests-per-client", str(requests_per_client),
        "--set-get-ratio", str(set_get_ratio),
        "--value-size", str(value_size),
        "--csv-out", csv_path,
    ]
    print(f"==> {clients} clients: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    print(result.stdout)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        raise SystemExit(f"tinykv-bench failed for --clients {clients}")


def read_row(csv_path):
    with open(csv_path, newline="") as f:
        return next(csv.DictReader(f))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6380)
    parser.add_argument("--requests-per-client", type=int, default=500)
    parser.add_argument("--set-get-ratio", type=float, default=0.5)
    parser.add_argument("--value-size", type=int, default=64)
    parser.add_argument("--clients-matrix", type=int, nargs="+", default=[10, 100, 1000])
    parser.add_argument("--binary", default=None,
                         help="path to tinykv-bench (default: build/benchmarks/tinykv-bench)")
    args = parser.parse_args()

    root = project_root()
    binary = args.binary or os.path.join(root, "build", "benchmarks", "tinykv-bench")
    if not os.path.isfile(binary):
        raise SystemExit(f"tinykv-bench not found at {binary} - build the project first")

    results_dir = os.path.join(root, "benchmarks", "results")
    os.makedirs(results_dir, exist_ok=True)
    suite_stamp = time.strftime("%Y%m%d_%H%M%S")

    rows = []
    for clients in args.clients_matrix:
        csv_path = os.path.join(results_dir, f"suite_{suite_stamp}_clients{clients}.csv")
        run_one(binary, args.host, args.port, clients, args.requests_per_client, args.set_get_ratio,
                args.value_size, csv_path)
        rows.append(read_row(csv_path))

    print("\n===== Summary =====")
    header = ["clients", "throughput_ops_sec", "latency_avg_us", "latency_p50_us", "latency_p95_us",
              "latency_p99_us", "errors"]
    print(" | ".join(f"{h:>18}" for h in header))
    for row in rows:
        print(" | ".join(f"{row[h]:>18}" for h in header))


if __name__ == "__main__":
    main()
