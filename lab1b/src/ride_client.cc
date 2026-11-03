#include <iomanip>
#include <iostream>
#include <string>

#include "gflags/gflags.h"
#include "trpc/client/make_client_context.h"
#include "trpc/client/trpc_client.h"
#include "trpc/common/runtime_manager.h"

#include "proto/trip.trpc.pb.h"

DEFINE_string(client_config, "config/ride-client.yaml", "tRPC client yaml path");
DEFINE_string(op, "create", "operation: create or get");
DEFINE_string(passenger_id, "alice", "passenger id for create");
DEFINE_string(city, "wuhan", "city for create");
DEFINE_string(origin, "campus", "origin for create");
DEFINE_string(destination, "railway-station", "destination for create");
DEFINE_double(distance_km, 8.5, "distance in kilometres for create");
DEFINE_string(time_slot, "peak", "peak or offpeak for create");
DEFINE_string(trip_id, "", "trip id for get");

namespace {

void PrintTrip(const ::trpc::ride::Trip& trip) {
  std::cout << "trip_id=" << trip.trip_id() << " passenger_id=" << trip.passenger_id()
            << " route=" << trip.origin() << "->" << trip.destination()
            << " distance_km=" << trip.distance_km() << " time_slot=" << trip.time_slot()
            << " estimated_fare=" << std::fixed << std::setprecision(2) << trip.estimated_fare()
            << " status=" << trip.status() << std::endl;
}

int Run() {
  const auto proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::ride::TripServiceServiceProxy>(
      "trpc.ride.trip.TripService");
  const auto context = ::trpc::MakeClientContext(proxy);

  if (FLAGS_op == "create") {
    ::trpc::ride::CreateRideRequest request;
    request.set_passenger_id(FLAGS_passenger_id);
    request.set_city(FLAGS_city);
    request.set_origin(FLAGS_origin);
    request.set_destination(FLAGS_destination);
    request.set_distance_km(FLAGS_distance_km);
    request.set_time_slot(FLAGS_time_slot);
    ::trpc::ride::CreateRideReply reply;
    const auto status = proxy->CreateRide(context, request, &reply);
    if (!status.OK()) {
      std::cerr << "CreateRide RPC failed: " << status.ErrorMessage() << std::endl;
      return 1;
    }
    std::cout << "create_ok=" << reply.ok() << " message=" << reply.message() << std::endl;
    if (reply.ok()) PrintTrip(reply.trip());
    return reply.ok() ? 0 : 1;
  }

  if (FLAGS_op == "get") {
    if (FLAGS_trip_id.empty()) {
      std::cerr << "--trip_id is required for get" << std::endl;
      return 2;
    }
    ::trpc::ride::GetTripRequest request;
    request.set_trip_id(FLAGS_trip_id);
    ::trpc::ride::GetTripReply reply;
    const auto status = proxy->GetTrip(context, request, &reply);
    if (!status.OK()) {
      std::cerr << "GetTrip RPC failed: " << status.ErrorMessage() << std::endl;
      return 1;
    }
    std::cout << "found=" << reply.found() << " message=" << reply.message() << std::endl;
    if (reply.found()) PrintTrip(reply.trip());
    return reply.found() ? 0 : 1;
  }

  std::cerr << "--op must be create or get" << std::endl;
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
