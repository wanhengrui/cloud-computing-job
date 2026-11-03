#pragma once

#include "proto/pricing.trpc.pb.h"

namespace lab1 {

// Pricing owns the fare policy (base fare, per-kilometre fare and the peak
// multiplier). No other component may re-implement these rules: trip-service
// has to ask this service for a quote, which keeps a single source of truth
// and gives an independent log line as evidence that the RPC happened.
class PricingServiceImpl final : public ::trpc::ride::PricingService {
 public:
  ::trpc::Status EstimateFare(::trpc::ServerContextPtr context,
                              const ::trpc::ride::EstimateFareRequest* request,
                              ::trpc::ride::EstimateFareReply* reply) override;
};

}  // namespace lab1
