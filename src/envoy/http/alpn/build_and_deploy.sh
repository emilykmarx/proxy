#!/usr/bin/env bash

set -e

minikube delete --all
minikube start --memory 16384 --cpus 10 & # hopefully fast enough

pushd ~/go/src/istio.io/proxy
bazel build --override_repository="envoy=/home/emily/go/src/envoyproxy.io/envoy" //src/envoy:envoy
cp bazel-bin/src/envoy/envoy ../istio/out/linux_amd64/release/envoy

cd ../istio
sudo make docker.proxyv2 TAG=tag HUB=hub
minikube image load hub/proxyv2:tag
go run ./istioctl/cmd/istioctl install --set profile=demo --set hub=hub --set tag=tag -y --set 'meshConfig.defaultConfig.proxyStatsMatcher.inclusionRegexps[0]=.*alpn.*'
kubectl label namespace default istio-injection=enabled
kubectl apply -f samples/bookinfo/platform/kube/bookinfo.yaml
kubectl apply -f ../proxy/src/envoy/http/alpn/alpn.yaml

popd
