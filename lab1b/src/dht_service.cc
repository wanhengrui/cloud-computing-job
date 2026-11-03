#include "src/dht_service.h"

#include <utility>

#include "trpc/client/make_client_context.h"
#include "trpc/client/trpc_client.h"
#include "trpc/common/status.h"
#include "trpc/log/trpc_log.h"

namespace lab1 {

DhtServiceImpl::DhtServiceImpl(std::vector<DhtNodeInfo> ring, std::size_t self_index)
    : ring_(std::move(ring)), self_index_(self_index) {
  // Only the successor is ever contacted: the ring is static and has no
  // finger table, so one long connection covers every route this node needs.
  successor_proxy_ = ::trpc::GetTrpcClient()->GetProxy<::trpc::dht::DhtServiceServiceProxy>(
      DhtServiceName(Successor().name));
}

::trpc::Status DhtServiceImpl::Put(::trpc::ServerContextPtr context,
                                   const ::trpc::dht::PutRequest* request,
                                   ::trpc::dht::PutReply* reply) {
  const std::uint32_t hash = Fnv1a32(request->key());
  const std::uint32_t hops = request->hops();
  const std::string path = ExtendPath(request->path(), Self().name);

  if (Owns(hash)) {
    {
      std::lock_guard<std::mutex> lock(store_mutex_);
      store_[request->key()] = request->value();
    }
    reply->set_ok(true);
    reply->set_owner(Self().name);
    reply->set_hops(hops);
    reply->set_message("value stored on " + Self().name + " (path " + path + ", hops " +
                       std::to_string(hops) + ")");
    TRPC_FMT_INFO("dht put key={} hash={} node={} action=serve owner={} hops={} path={}",
                  request->key(), hash, Self().name, Self().name, hops, path);
    return ::trpc::kSuccStatus;
  }

  // The static ring gives at most two forwarding steps, so hitting the limit
  // means the configuration is broken. It is a guard rail, not a fault
  // tolerance mechanism.
  if (hops >= kMaxHops) {
    reply->set_ok(false);
    reply->set_owner(Self().name);
    reply->set_hops(hops);
    reply->set_message("hops limit " + std::to_string(kMaxHops) + " reached at " + Self().name +
                       " (path " + path + "): check predecessor/successor wiring");
    TRPC_FMT_WARN("dht put key={} hash={} node={} action=hops-limit hops={} path={}",
                  request->key(), hash, Self().name, hops, path);
    return ::trpc::kSuccStatus;
  }

  ::trpc::dht::PutRequest forwarded(*request);
  forwarded.set_hops(hops + 1);
  forwarded.set_path(path);
  ::trpc::dht::PutReply downstream;
  const auto client_context = ::trpc::MakeClientContext(context, successor_proxy_);
  const ::trpc::Status status = successor_proxy_->Put(client_context, forwarded, &downstream);
  TRPC_FMT_INFO("dht put key={} hash={} node={} action=forward successor={} hops={} path={} ret={}",
                request->key(), hash, Self().name, Successor().name, hops + 1, path,
                status.GetFrameworkRetCode());
  if (!status.OK()) {
    reply->set_ok(false);
    reply->set_owner(Self().name);
    reply->set_hops(hops);
    reply->set_message("forward to successor " + Successor().name + " failed: " +
                       status.ErrorMessage());
    return ::trpc::kSuccStatus;
  }
  *reply = downstream;
  return ::trpc::kSuccStatus;
}

::trpc::Status DhtServiceImpl::Get(::trpc::ServerContextPtr context,
                                   const ::trpc::dht::GetRequest* request,
                                   ::trpc::dht::GetReply* reply) {
  const std::uint32_t hash = Fnv1a32(request->key());
  const std::uint32_t hops = request->hops();
  const std::string path = ExtendPath(request->path(), Self().name);

  if (Owns(hash)) {
    std::string value;
    bool found = false;
    {
      std::lock_guard<std::mutex> lock(store_mutex_);
      const auto it = store_.find(request->key());
      found = it != store_.end();
      if (found) {
        value = it->second;
      }
    }
    reply->set_found(found);
    reply->set_owner(Self().name);
    reply->set_hops(hops);
    if (found) {
      reply->set_value(std::move(value));
      reply->set_message("value read from " + Self().name + " (path " + path + ", hops " +
                         std::to_string(hops) + ")");
    } else {
      reply->set_message("key not found on owner " + Self().name + " (path " + path + ", hops " +
                         std::to_string(hops) + ")");
    }
    TRPC_FMT_INFO("dht get key={} hash={} node={} action=serve owner={} found={} hops={} path={}",
                  request->key(), hash, Self().name, Self().name, found, hops, path);
    return ::trpc::kSuccStatus;
  }

  if (hops >= kMaxHops) {
    reply->set_found(false);
    reply->set_owner(Self().name);
    reply->set_hops(hops);
    reply->set_message("hops limit " + std::to_string(kMaxHops) + " reached at " + Self().name +
                       " (path " + path + "): check predecessor/successor wiring");
    TRPC_FMT_WARN("dht get key={} hash={} node={} action=hops-limit hops={} path={}",
                  request->key(), hash, Self().name, hops, path);
    return ::trpc::kSuccStatus;
  }

  ::trpc::dht::GetRequest forwarded(*request);
  forwarded.set_hops(hops + 1);
  forwarded.set_path(path);
  ::trpc::dht::GetReply downstream;
  const auto client_context = ::trpc::MakeClientContext(context, successor_proxy_);
  const ::trpc::Status status = successor_proxy_->Get(client_context, forwarded, &downstream);
  TRPC_FMT_INFO("dht get key={} hash={} node={} action=forward successor={} hops={} path={} ret={}",
                request->key(), hash, Self().name, Successor().name, hops + 1, path,
                status.GetFrameworkRetCode());
  if (!status.OK()) {
    reply->set_found(false);
    reply->set_owner(Self().name);
    reply->set_hops(hops);
    reply->set_message("forward to successor " + Successor().name + " failed: " +
                       status.ErrorMessage());
    return ::trpc::kSuccStatus;
  }
  *reply = downstream;
  return ::trpc::kSuccStatus;
}

}  // namespace lab1
