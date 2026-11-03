#include <memory>
#include <string>

#include "fmt/format.h"
#include "trpc/common/trpc_app.h"
#include "trpc/log/trpc_log.h"

#include "src/trip_service.h"

namespace lab1 {

class TripServer final : public ::trpc::TrpcApp {
 public:
  int Initialize() override {
    const auto& config = ::trpc::TrpcConfig::GetInstance()->GetServerConfig();
    const std::string service_name = fmt::format("trpc.{}.{}.TripService", config.app, config.server);
    TRPC_FMT_INFO("register service={}", service_name);
    RegisterService(service_name, std::make_shared<TripServiceImpl>());
    return 0;
  }
  void Destroy() override {}
};

}  // namespace lab1

int main(int argc, char** argv) {
  lab1::TripServer server;
  server.Main(argc, argv);
  server.Wait();
  return 0;
}
