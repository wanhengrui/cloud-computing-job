# The course staff creates course/trpc-cpp-dev:2026.09 with Dockerfile.base.
# Keeping the framework outside this image makes student rebuilds small.
ARG TRPC_BASE=course/trpc-cpp-dev:2026.09
FROM ${TRPC_BASE} AS build

WORKDIR /workspace
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel 2

FROM ${TRPC_BASE}
WORKDIR /app
COPY --from=build /workspace/build/pricing_server /app/pricing_server
COPY --from=build /workspace/build/trip_server /app/trip_server
COPY --from=build /workspace/build/ride_client /app/ride_client
COPY --from=build /workspace/build/dht_server /app/dht_server
COPY --from=build /workspace/build/dht_cli /app/dht_cli
COPY config /app/config
