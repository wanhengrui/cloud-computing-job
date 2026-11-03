#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "gflags/gflags.h"
#include "trpc/client/make_client_context.h"
#include "trpc/client/trpc_client.h"
#include "trpc/common/runtime_manager.h"

#include "proto/dht.trpc.pb.h"
#include "src/dht_common.h"

DEFINE_string(client_config, "config/dht-client.yaml", "tRPC client yaml path");
DEFINE_string(op, "put", "operation: put or get");
DEFINE_string(key, "", "key to store or read");
DEFINE_string(value, "", "value for put (plain text)");
DEFINE_string(entry, "node-a", "entry node name from the node inventory, e.g. node-a");

namespace {

// The value is opaque bytes by contract. Trip orders are protobuf blobs, so
// only print them as text when they really are text.
std::string DescribeValue(const std::string& value) {
  for (unsigned char byte : value) {
    if (!std::isprint(byte) && !std::isspace(byte)) {
      std::ostringstream hex;
      hex << "<" << value.size() << " bytes>";
      return hex.str();
    }
  }
  return value;
}

int Run() {
  const std::string service_name = lab1::DhtServiceName(FLAGS_entry);
  const auto proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::dht::DhtServiceServiceProxy>(
      service_name);
  if (proxy == nullptr) {
    std::cerr << "cannot create proxy for " << service_name
              << "; check that " << FLAGS_client_config << " declares it" << std::endl;
    return 2;
  }
  const auto context = ::trpc::MakeClientContext(proxy);

  std::cout << "entry=" << FLAGS_entry << " service=" << service_name
            << " key=" << FLAGS_key << " hash=" << lab1::Fnv1a32(FLAGS_key) << std::endl;

  if (FLAGS_op == "put") {
    if (FLAGS_key.empty()) {
      std::cerr << "--key is required for put" << std::endl;
      return 2;
    }
    ::trpc::dht::PutRequest request;
    request.set_key(FLAGS_key);
    request.set_value(FLAGS_value);
    request.set_hops(0);
    ::trpc::dht::PutReply reply;
    const auto status = proxy->Put(context, request, &reply);
    if (!status.OK()) {
      std::cerr << "DHT Put RPC failed: " << status.ErrorMessage() << std::endl;
      return 1;
    }
    std::cout << "put_ok=" << reply.ok() << " owner=" << reply.owner() << " hops=" << reply.hops()
              << " message=" << reply.message() << std::endl;
    return reply.ok() ? 0 : 1;
  }

  if (FLAGS_op == "get") {
    if (FLAGS_key.empty()) {
      std::cerr << "--key is required for get" << std::endl;
      return 2;
    }
    ::trpc::dht::GetRequest request;
    request.set_key(FLAGS_key);
    request.set_hops(0);
    ::trpc::dht::GetReply reply;
    const auto status = proxy->Get(context, request, &reply);
    if (!status.OK()) {
      std::cerr << "DHT Get RPC failed: " << status.ErrorMessage() << std::endl;
      return 1;
    }
    std::cout << "found=" << reply.found() << " owner=" << reply.owner()
              << " hops=" << reply.hops() << " message=" << reply.message() << std::endl;
    if (reply.found()) {
      std::cout << "value_bytes=" << reply.value().size()
                << " value=" << DescribeValue(reply.value()) << std::endl;
    }
    return reply.found() ? 0 : 1;
  }

  std::cerr << "--op must be put or get" << std::endl;
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  if (::trpc::TrpcConfig::GetInstance()->Init(FLAGS_client_config) != 0) {
    std::cerr << "cannot load client config: " << FLAGS_client_config << std::endl;
    return 2;
  }
  return ::trpc::RunInTrpcRuntime([] { return Run(); });
}
