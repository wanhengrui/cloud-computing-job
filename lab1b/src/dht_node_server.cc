#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "fmt/format.h"
#include "trpc/common/trpc_app.h"
#include "trpc/common/trpc_plugin.h"
#include "trpc/log/trpc_log.h"

#include "src/dht_common.h"
#include "src/dht_service.h"

namespace lab1 {

// Everything that makes node-a different from node-b lives in the node
// inventory environment (SELF + NODE_A/B/C_*), so the same binary and the same
// image run on all three machines.
class DhtNodeServer final : public ::trpc::TrpcApp {
 public:
  int Initialize() override {
    std::vector<DhtNodeInfo> ring;
    std::string error;
    if (!LoadRingFromEnv(&ring, &error)) {
      std::fprintf(stderr, "dht node: %s\n", error.c_str());
      return -1;
    }
    const std::string self_name = EnvOr("SELF");
    const std::size_t self_index = IndexOfName(ring, self_name);
    if (self_index >= ring.size()) {
      std::fprintf(stderr, "dht node: SELF=%s is not in the node inventory\n", self_name.c_str());
      return -1;
    }

    const auto& config = ::trpc::TrpcConfig::GetInstance()->GetServerConfig();
    const std::string service_name = fmt::format("trpc.{}.{}.DhtService", config.app, config.server);
    TRPC_FMT_INFO("register service={}", service_name);
    RegisterService(service_name, std::make_shared<DhtServiceImpl>(ring, self_index));

    const DhtNodeInfo& self = ring[self_index];
    const DhtNodeInfo& pred = ring[(self_index + ring.size() - 1) % ring.size()];
    const DhtNodeInfo& succ = ring[(self_index + 1) % ring.size()];
    TRPC_FMT_INFO("dht ring node={} id={} host={} port={}", self.name, self.id, self.host, self.port);
    TRPC_FMT_INFO("dht ring predecessor={} predecessor_id={} successor={} successor_id={}",
                  pred.name, pred.id, succ.name, succ.id);
    for (const auto& node : ring) {
      TRPC_FMT_INFO("dht ring member name={} id={} host={} port={}", node.name, node.id, node.host,
                    node.port);
    }
    return 0;
  }
  void Destroy() override {}
};

}  // namespace lab1

int main(int argc, char** argv) {
  lab1::DhtNodeServer server;
  server.Main(argc, argv);
  server.Wait();
  return 0;
}
