#include "src/pricing_service.h"

#include <ctime>
#include <string>

#include "trpc/common/status.h"
#include "trpc/log/trpc_log.h"

namespace lab1 {
namespace {

// The teaching fare rule, kept deterministic and fully offline. No real map,
// ride-hailing or payment API may be called from this lab.
constexpr double kBaseFare = 10.0;
constexpr double kFarePerKm = 2.4;
constexpr double kPeakMultiplier = 1.35;
constexpr double kOffPeakMultiplier = 1.0;

// Quoting time is recorded so the caller can prove which quote it received.
std::string QuotedAt() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char buffer[32] = {0};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local);
  return std::string(buffer);
}

}  // namespace

::trpc::Status PricingServiceImpl::EstimateFare(
    ::trpc::ServerContextPtr context, const ::trpc::ride::EstimateFareRequest* request,
    ::trpc::ride::EstimateFareReply* reply) {
  (void)context;

  // Only "peak" and "offpeak" carry a defined multiplier; anything else would
  // silently produce a wrong quote, so it is rejected instead.
  const bool peak = request->time_slot() == "peak";
  const bool offpeak = request->time_slot() == "offpeak";
  if (request->city().empty() || request->distance_km() <= 0.0 || (!peak && !offpeak)) {
    reply->set_ok(false);
    reply->set_message("city, a positive distance_km and time_slot peak|offpeak are required");
    TRPC_FMT_WARN("estimate fare rejected city={} distance_km={} time_slot={}", request->city(),
                  request->distance_km(), request->time_slot());
    return ::trpc::kSuccStatus;
  }

  const double distance_fare = request->distance_km() * kFarePerKm;
  const double multiplier = peak ? kPeakMultiplier : kOffPeakMultiplier;
  const double total_fare = (kBaseFare + distance_fare) * multiplier;

  reply->set_ok(true);
  reply->set_base_fare(kBaseFare);
  reply->set_distance_fare(distance_fare);
  reply->set_multiplier(multiplier);
  reply->set_total_fare(total_fare);
  reply->set_quoted_at(QuotedAt());
  reply->set_message("fare quoted by pricing-service");
  TRPC_FMT_INFO("estimate fare city={} distance_km={} time_slot={} total_fare={:.2f}",
                request->city(), request->distance_km(), request->time_slot(), total_fare);
  return ::trpc::kSuccStatus;
}

}  // namespace lab1
