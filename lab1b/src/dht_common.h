#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

// Shared, dependency-free pieces of the Lab1B ring: the hash, the interval
// test and the node inventory loader. Keeping them in one header means the
// server process and the command line client can never disagree about which
// node owns a key.

namespace lab1 {

// FNV-1a, 32 bit. The course only requires "a deterministic hash", and this
// one is short enough to re-implement during the defence.
inline std::uint32_t Fnv1a32(const std::string& text) {
  std::uint32_t hash = 2166136261u;
  for (unsigned char byte : text) {
    hash ^= static_cast<std::uint32_t>(byte);
    hash *= 16777619u;  // 32 bit natural overflow is the modulo 2^32 in the spec.
  }
  return hash;
}

struct DhtNodeInfo {
  std::string name;   // node-a / node-b / node-c
  std::string host;   // container name locally, private IP on the cloud VMs
  std::uint16_t port{0};
  std::uint32_t id{0};
};

// Node N owns (predecessor_id, N.id]; the interval wraps when N is the
// smallest id on the ring, which is the one case people get wrong.
inline bool OwnsHash(std::uint32_t hash, std::uint32_t self_id, std::uint32_t pred_id) {
  if (pred_id < self_id) {
    return hash > pred_id && hash <= self_id;
  }
  if (pred_id > self_id) {
    return hash > pred_id || hash <= self_id;
  }
  return true;  // A ring with one node trivially owns everything.
}

inline std::string EnvOr(const std::string& name, const std::string& fallback = "") {
  const char* value = std::getenv(name.c_str());
  return value == nullptr ? fallback : std::string(value);
}

// Reads NODE_A_* / NODE_B_* / NODE_C_* exactly like the course inventory file
// does, then sorts by node_id so the caller can take predecessor/successor by
// index. Returns false with a readable message instead of throwing.
inline bool LoadRingFromEnv(std::vector<DhtNodeInfo>* ring, std::string* error) {
  static const char* kPrefixes[] = {"NODE_A_", "NODE_B_", "NODE_C_"};
  ring->clear();
  try {
    for (const char* prefix : kPrefixes) {
      const std::string base(prefix);
      DhtNodeInfo node;
      node.name = EnvOr(base + "NAME");
      if (node.name.empty()) {
        continue;  // A missing slot is fine as long as three are present.
      }
      node.host = EnvOr(base + "HOST");
      node.port = static_cast<std::uint16_t>(std::stoul(EnvOr(base + "PORT", "19001")));
      const std::string id_text = EnvOr(base + "ID");
      if (node.host.empty() || id_text.empty()) {
        *error = "node inventory entry is incomplete for " + node.name +
                 " (need _NAME, _HOST, _PORT, _ID)";
        return false;
      }
      node.id = static_cast<std::uint32_t>(std::stoul(id_text, nullptr, 0));
      ring->push_back(node);
    }
  } catch (const std::exception& ex) {
    *error = std::string("cannot parse node inventory: ") + ex.what();
    return false;
  }
  if (ring->size() != 3) {
    *error = "expected 3 nodes in the node inventory, found " + std::to_string(ring->size());
    return false;
  }
  std::sort(ring->begin(), ring->end(),
            [](const DhtNodeInfo& lhs, const DhtNodeInfo& rhs) { return lhs.id < rhs.id; });
  for (std::size_t i = 1; i < ring->size(); ++i) {
    if ((*ring)[i - 1].id == (*ring)[i].id) {
      *error = "node ids must be unique, found a duplicate";
      return false;
    }
  }
  return true;
}

// Every node registers its own tRPC service name (trpc.dht.a.DhtService for
// node-a). Keeping them distinct means a node can hold a proxy for its
// successor without the framework having to disambiguate two services that
// share one name.
inline std::string DhtServiceName(const std::string& node_name) {
  const std::string prefix = "node-";
  const std::string letter =
      node_name.rfind(prefix, 0) == 0 ? node_name.substr(prefix.size()) : node_name;
  return "trpc.dht." + letter + ".DhtService";
}

inline std::size_t IndexOfName(const std::vector<DhtNodeInfo>& ring, const std::string& name) {  for (std::size_t i = 0; i < ring.size(); ++i) {
    if (ring[i].name == name) {
      return i;
    }
  }
  return ring.size();
}

}  // namespace lab1
