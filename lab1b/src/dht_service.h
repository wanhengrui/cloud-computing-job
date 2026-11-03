#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "proto/dht.trpc.pb.h"
#include "src/dht_common.h"

namespace lab1 {

// One DHT node = one process. The ring is static (three entries, read from the
// node inventory at startup), so routing is a single successor hop per receive
// and never needs a finger table.
class DhtServiceImpl final : public ::trpc::dht::DhtService {
 public:
  static constexpr std::uint32_t kMaxHops = 3;

  DhtServiceImpl(std::vector<DhtNodeInfo> ring, std::size_t self_index);

  ::trpc::Status Put(::trpc::ServerContextPtr context,
                     const ::trpc::dht::PutRequest* request,
                     ::trpc::dht::PutReply* reply) override;

  ::trpc::Status Get(::trpc::ServerContextPtr context,
                     const ::trpc::dht::GetRequest* request,
                     ::trpc::dht::GetReply* reply) override;

 private:
  const DhtNodeInfo& Self() const { return ring_[self_index_]; }
  const DhtNodeInfo& Predecessor() const {
    return ring_[(self_index_ + ring_.size() - 1) % ring_.size()];
  }
  const DhtNodeInfo& Successor() const { return ring_[(self_index_ + 1) % ring_.size()]; }

  bool Owns(std::uint32_t hash) const { return OwnsHash(hash, Self().id, Predecessor().id); }

  // Appends this node to the path so the reply can echo the full route.
  static std::string ExtendPath(const std::string& path, const std::string& self_name) {
    return path.empty() ? self_name : path + "->" + self_name;
  }

  std::vector<DhtNodeInfo> ring_;  // ascending node_id
  std::size_t self_index_{0};

  std::unordered_map<std::string, std::string> store_;
  std::mutex store_mutex_;

  std::shared_ptr<::trpc::dht::DhtServiceServiceProxy> successor_proxy_;
};

}  // namespace lab1
