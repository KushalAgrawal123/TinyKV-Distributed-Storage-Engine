#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "tinykv/net/tcp_client.hpp"

namespace {

struct BenchConfig {
  std::string host = "127.0.0.1";
  int port = 6380;
  int clients = 10;
  int requestsPerClient = 1000;
  double setGetRatio = 0.5;  // fraction of requests that are SET; the rest are GET
  int valueSize = 64;        // bytes of the SET value payload
  std::string csvOut;        // empty => auto-generate a timestamped path
};

BenchConfig parseArgs(int argc, char** argv) {
  BenchConfig config;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
    if (arg == "--host") {
      config.host = next();
    } else if (arg == "--port") {
      config.port = std::stoi(next());
    } else if (arg == "--clients") {
      config.clients = std::stoi(next());
    } else if (arg == "--requests-per-client") {
      config.requestsPerClient = std::stoi(next());
    } else if (arg == "--set-get-ratio") {
      config.setGetRatio = std::stod(next());
    } else if (arg == "--value-size") {
      config.valueSize = std::stoi(next());
    } else if (arg == "--csv-out") {
      config.csvOut = next();
    }
  }
  return config;
}

struct ClientResult {
  std::vector<double> latenciesMicros;
  int errors = 0;
};

// Each client thread owns one persistent connection and issues requests
// sequentially, waiting for each reply before sending the next - this
// measures real round-trip latency rather than pipelined throughput,
// matching the protocol's own no-pipelining design.
void runClient(const BenchConfig& config, int clientId, ClientResult& result) {
  tinykv::TcpClient client;
  if (!client.connect(config.host, config.port)) {
    result.errors = config.requestsPerClient;
    return;
  }

  std::string value(static_cast<size_t>(config.valueSize), 'x');
  std::mt19937 rng(static_cast<unsigned>(clientId) + 1);
  std::uniform_real_distribution<double> dist(0.0, 1.0);

  result.latenciesMicros.reserve(static_cast<size_t>(config.requestsPerClient));

  for (int i = 0; i < config.requestsPerClient; ++i) {
    // Bounded working set per client (1000 keys), rather than an
    // ever-growing key space, so GETs have a realistic chance of hitting
    // a key a prior SET in this run actually created.
    std::string key = "bench:c" + std::to_string(clientId) + ":k" + std::to_string(i % 1000);
    bool isSet = dist(rng) < config.setGetRatio;
    std::string request = isSet ? ("SET " + key + " " + value) : ("GET " + key);

    auto start = std::chrono::steady_clock::now();
    bool ok = client.sendLine(request);
    std::string response;
    if (ok) ok = client.receiveLine(response);
    auto end = std::chrono::steady_clock::now();

    if (!ok) {
      ++result.errors;
      break;  // connection is dead; no point continuing this client
    }
    result.latenciesMicros.push_back(std::chrono::duration<double, std::micro>(end - start).count());
  }
}

double percentile(const std::vector<double>& sortedLatencies, double p) {
  if (sortedLatencies.empty()) return 0.0;
  size_t idx = static_cast<size_t>(p * static_cast<double>(sortedLatencies.size() - 1));
  return sortedLatencies[idx];
}

std::string timestampForFilename() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tmBuf{};
  localtime_r(&t, &tmBuf);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tmBuf);
  return std::string(buf);
}

}  // namespace

int main(int argc, char** argv) {
  BenchConfig config = parseArgs(argc, argv);

  std::cout << "TinyKV Benchmark\n";
  std::cout << "  target:              " << config.host << ":" << config.port << "\n";
  std::cout << "  clients:             " << config.clients << "\n";
  std::cout << "  requests per client: " << config.requestsPerClient << "\n";
  std::cout << "  set/get ratio:       " << config.setGetRatio << "\n";
  std::cout << "  value size:          " << config.valueSize << " bytes\n\n";

  std::vector<std::thread> threads;
  std::vector<ClientResult> results(static_cast<size_t>(config.clients));

  auto benchStart = std::chrono::steady_clock::now();
  for (int i = 0; i < config.clients; ++i) {
    threads.emplace_back(runClient, std::cref(config), i, std::ref(results[static_cast<size_t>(i)]));
  }
  for (auto& t : threads) t.join();
  auto benchEnd = std::chrono::steady_clock::now();

  std::vector<double> allLatencies;
  int totalErrors = 0;
  for (const auto& r : results) {
    allLatencies.insert(allLatencies.end(), r.latenciesMicros.begin(), r.latenciesMicros.end());
    totalErrors += r.errors;
  }
  std::sort(allLatencies.begin(), allLatencies.end());

  double totalSeconds = std::chrono::duration<double>(benchEnd - benchStart).count();
  double opsPerSec = totalSeconds > 0 ? static_cast<double>(allLatencies.size()) / totalSeconds : 0.0;
  double avg = allLatencies.empty() ? 0.0
                                     : std::accumulate(allLatencies.begin(), allLatencies.end(), 0.0) /
                                           static_cast<double>(allLatencies.size());
  double p50 = percentile(allLatencies, 0.50);
  double p95 = percentile(allLatencies, 0.95);
  double p99 = percentile(allLatencies, 0.99);

  std::printf("===== Results =====\n");
  std::printf("Total requests:   %zu (%d errors)\n", allLatencies.size(), totalErrors);
  std::printf("Duration:         %.3f s\n", totalSeconds);
  std::printf("Throughput:       %.1f ops/sec\n", opsPerSec);
  std::printf("Latency avg:      %.1f us\n", avg);
  std::printf("Latency p50:      %.1f us\n", p50);
  std::printf("Latency p95:      %.1f us\n", p95);
  std::printf("Latency p99:      %.1f us\n", p99);

  std::string csvPath = config.csvOut;
  if (csvPath.empty()) {
    csvPath = "benchmarks/results/" + timestampForFilename() + ".csv";
  }
  std::filesystem::create_directories(std::filesystem::path(csvPath).parent_path());

  std::ofstream csv(csvPath);
  if (csv.is_open()) {
    csv << "clients,requests_per_client,total_requests,errors,duration_seconds,throughput_ops_sec,"
           "latency_avg_us,latency_p50_us,latency_p95_us,latency_p99_us\n";
    csv << config.clients << "," << config.requestsPerClient << "," << allLatencies.size() << "," << totalErrors
        << "," << totalSeconds << "," << opsPerSec << "," << avg << "," << p50 << "," << p95 << "," << p99 << "\n";
    std::cout << "Results written to " << csvPath << "\n";
  } else {
    std::cerr << "Warning: could not write CSV to " << csvPath << "\n";
  }

  return 0;
}
