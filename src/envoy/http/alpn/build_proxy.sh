#!/usr/bin/env bash

# Builds the proxyv2 image, which pulls in local changes to envoy and proxy (i.e. builds the filter into istio's proxy).
set -e

pushd ~/go/src/istio.io/proxy
bazel build --override_repository="envoy=/home/emily/go/src/envoyproxy.io/envoy" //src/envoy:envoy
cp bazel-bin/src/envoy/envoy ../istio/out/linux_amd64/release/envoy

cd ../istio
# Note this needs to be called proxyv2 when loaded into the cluster.
# Tag and hub are arbitrary, but need to match the ones passed when installing istio.
sudo make docker.proxyv2 TAG=tag HUB=hub

popd
