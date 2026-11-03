#include "src/trip_service.h"

#include <iomanip>
#include <sstream>
#include <utility>

#include "trpc/client/make_client_context.h"
#include "trpc/client/trpc_client.h"
#include "trpc/common/status.h"
#include "trpc/log/trpc_log.h"

#include "src/dht_common.h"

namespace lab1 {

TripServiceImpl::TripServiceImpl() {
  pricing_proxy_ = ::trpc::GetTrpcClient()->GetProxy<::trpc::ride::PricingServiceServiceProxy>(
      "trpc.ride.pricing.PricingService");
  // Lab1B: orders are persisted in the DHT. The entry node is configurable so
  // the same binary can demonstrate "same key, different entry node" without
  // rebuilding; node-a is the course sanctioned default.
  dht_entry_ = EnvOr("DHT_ENTRY", "node-a");
  dht_proxy_ = ::trpc::GetTrpcClient()->GetProxy<::trpc::dht::DhtServiceServiceProxy>(
      DhtServiceName(dht_entry_));
  TRPC_FMT_INFO("trip-service dht entry={} service={}", dht_entry_, DhtServiceName(dht_entry_));
}

std::string TripServiceImpl::NextTripId() {
  std::lock_guard<std::mutex> lock(id_mutex_);
  std::ostringstream stream;
  stream << "trip-" << std::setfill('0') << std::setw(6) << next_trip_number_++;
  return stream.str();
}

::trpc::Status TripServiceImpl::CreateRide(::trpc::ServerContextPtr context,
                                           const ::trpc::ride::CreateRideRequest* request,
                                           ::trpc::ride::CreateRideReply* reply) {
  if (request->passenger_id().empty() || request->origin().empty() || request->destination().empty() ||
      request->distance_km() <= 0.0) {
    reply->set_ok(false);
    reply->set_message("passenger, origin, destination and a positive distance are required");
    return ::trpc::kSuccStatus;
  }

  // Step 1: pricing stays the single owner of the fare policy.
  ::trpc::ride::EstimateFareRequest pricing_request;
  pricing_request.set_city(request->city());
  pricing_request.set_distance_km(request->distance_km());
  pricing_request.set_time_slot(request->time_slot());
  ::trpc::ride::EstimateFareReply pricing_reply;
  const auto pricing_context = ::trpc::MakeClientContext(context, pricing_proxy_);
  const ::trpc::Status pricing_status =
      pricing_proxy_->EstimateFare(pricing_context, pricing_request, &pricing_reply);
  if (!pricing_status.OK() || !pricing_reply.ok()) {
    reply->set_ok(false);
    reply->set_message(pricing_status.OK() ? pricing_reply.message()
                                           : "pricing-service RPC failed: " + pricing_status.ErrorMessage());
    TRPC_FMT_WARN("create ride passenger_id={} downstream=pricing-service result={}",
                  request->passenger_id(), reply->message());
    return ::trpc::kSuccStatus;
  }

  ::trpc::ride::Trip trip;
  trip.set_trip_id(NextTripId());
  trip.set_passenger_id(request->passenger_id());
  trip.set_city(request->city());
  trip.set_origin(request->origin());
  trip.set_destination(request->destination());
  trip.set_distance_km(request->distance_km());
  trip.set_time_slot(request->time_slot());
  trip.set_estimated_fare(pricing_reply.total_fare());
  trip.set_status("WAITING");

  // Step 2: 1B replaces the local unordered_map with a DHT entry. The order is
  // serialised as opaque bytes under "trip:<trip_id>"; the DHT never parses it.
  ::trpc::dht::PutRequest put_request;
  put_request.set_key(TripKey(trip.trip_id()));
  trip.SerializeToString(put_request.mutable_value());
  put_request.set_hops(0);
  ::trpc::dht::PutReply put_reply;
  const auto dht_context = ::trpc::MakeClientContext(context, dht_proxy_);
  const ::trpc::Status put_status = dht_proxy_->Put(dht_context, put_request, &put_reply);
  if (!put_status.OK() || !put_reply.ok()) {
    reply->set_ok(false);
    reply->set_message(put_status.OK() ? "dht put rejected: " + put_reply.message()
                                       : "dht Put RPC failed: " + put_status.ErrorMessage());
    TRPC_FMT_WARN("create ride trip_id={} entry={} downstream=dht result={}", trip.trip_id(), dht_entry_,
                  reply->message());
    return ::trpc::kSuccStatus;
  }

  *reply->mutable_trip() = trip;
  reply->set_ok(true);
  reply->set_message("trip created after pricing-service quote; stored in dht at " + put_reply.owner() +
                     " (entry " + dht_entry_ + ", hops " + std::to_string(put_reply.hops()) + ")");
  TRPC_FMT_INFO(
      "trip_id={} passenger_id={} downstream=pricing-service estimated_fare={} dht_key={} "
      "dht_owner={} dht_hops={}",
      trip.trip_id(), trip.passenger_id(), trip.estimated_fare(), TripKey(trip.trip_id()),
      put_reply.owner(), put_reply.hops());
  return ::trpc::kSuccStatus;
}

::trpc::Status TripServiceImpl::GetTrip(::trpc::ServerContextPtr context,
                                        const ::trpc::ride::GetTripRequest* request,
                                        ::trpc::ride::GetTripReply* reply) {
  ::trpc::dht::GetRequest get_request;
  get_request.set_key(TripKey(request->trip_id()));
  get_request.set_hops(0);
  ::trpc::dht::GetReply get_reply;
  const auto dht_context = ::trpc::MakeClientContext(context, dht_proxy_);
  const ::trpc::Status get_status = dht_proxy_->Get(dht_context, get_request, &get_reply);
  if (!get_status.OK()) {
    reply->set_found(false);
    reply->set_message("dht Get RPC failed: " + get_status.ErrorMessage());
    TRPC_FMT_WARN("get trip trip_id={} entry={} downstream=dht result={}", request->trip_id(),
                  dht_entry_, reply->message());
    return ::trpc::kSuccStatus;
  }
  if (!get_reply.found()) {
    reply->set_found(false);
    reply->set_message("trip not found in dht (" + get_reply.message() + ")");
    TRPC_FMT_INFO("trip_id={} found=false dht_key={} dht_owner={} dht_hops={}", request->trip_id(),
                  TripKey(request->trip_id()), get_reply.owner(), get_reply.hops());
    return ::trpc::kSuccStatus;
  }

  ::trpc::ride::Trip trip;
  if (!trip.ParseFromString(get_reply.value())) {
    reply->set_found(false);
    reply->set_message("dht returned bytes that are not a Trip for " + TripKey(request->trip_id()));
    TRPC_FMT_WARN("trip_id={} dht decode failed owner={}", request->trip_id(), get_reply.owner());
    return ::trpc::kSuccStatus;
  }
  reply->set_found(true);
  *reply->mutable_trip() = trip;
  reply->set_message("trip read from dht owner=" + get_reply.owner() + " hops=" +
                     std::to_string(get_reply.hops()));
  TRPC_FMT_INFO("trip_id={} found=true dht_key={} dht_owner={} dht_hops={}", request->trip_id(),
                TripKey(request->trip_id()), get_reply.owner(), get_reply.hops());
  return ::trpc::kSuccStatus;
}

}  // namespace lab1
