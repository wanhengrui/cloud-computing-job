#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "proto/dht.trpc.pb.h"
#include "proto/pricing.trpc.pb.h"
#include "proto/trip.trpc.pb.h"

namespace lab1 {

// Same CreateRide / GetTrip contract as Lab1A. The only thing that changed in
// 1B is where the order lives: the in-memory map is gone and every order is a
// DHT entry under the key "trip:<trip_id>".
class TripServiceImpl final : public ::trpc::ride::TripService {
 public:
  TripServiceImpl();

  ::trpc::Status CreateRide(::trpc::ServerContextPtr context,
                            const ::trpc::ride::CreateRideRequest* request,
                            ::trpc::ride::CreateRideReply* reply) override;
  ::trpc::Status GetTrip(::trpc::ServerContextPtr context,
                         const ::trpc::ride::GetTripRequest* request,
                         ::trpc::ride::GetTripReply* reply) override;

 private:
  std::string NextTripId();
  static std::string TripKey(const std::string& trip_id) { return "trip:" + trip_id; }

  std::shared_ptr<::trpc::ride::PricingServiceServiceProxy> pricing_proxy_;
  std::shared_ptr<::trpc::dht::DhtServiceServiceProxy> dht_proxy_;
  std::string dht_entry_;

  std::mutex id_mutex_;
  std::uint64_t next_trip_number_{1};
};

}  // namespace lab1
