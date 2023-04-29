#!/usr/bin/env python3
"""
For now, this is a wrapper around a single "trace" request,
assuming app is already deployed.
- Send the original request
- Send the corresponding trace
- Gather the "bits", i.e. scoped pod names and interesting log statements
"""

import argparse
import subprocess
import networkx as nx
import matplotlib.pyplot as plt
import random

"""
TODO
- test on trace w/o history
"""

G = nx.DiGraph()

def bookinfo_curl_productpage(is_trace, request_id=None):
  cmd = 'kubectl exec "$(kubectl get pod -l app=ratings -o jsonpath=\'{.items[0].metadata.name}\')" -c ratings -- curl -sS productpage:9080/productpage'
  if request_id is not None:
    cmd += f' -H \'x-request-id: {"WTFTRACE-" if is_trace else ""}{request_id}\''

  stdout, stderr, ret = run_command(cmd)

  if not is_trace and '<html>' not in stdout: # Using presence of <html> as indicator of successful response
    print(f'Request {request_id} failed:\n {stdout}\n {stderr}\n {ret}')

def run_command(command):
    proc = subprocess.run([command], shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    stdout = proc.stdout.strip().split('\n')
    stderr = proc.stderr.strip().split('\n')

    return stdout, stderr, proc.returncode

def get_bits():
  # Proxy, app, network
  # warn, error (note default level for stuff from filter is error)
  # HTTP codes
  # LEFT OFF: get bits, plot unhealthy components
  pass

def all_pods():
  cmd = 'kubectl get pods --no-headers -o custom-columns=":metadata.name,:status.podIP"'
  out, _,  _ = run_command(cmd)
  pods = {}
  for line in out:
    pod, ip = line.split()
    pods[ip] = pod

  return pods

def print_trace(request_id):
  # TODO handle pods changing IP (using as key for now): crashes and autoscaling
  # Thoughts: Can get own IP from `proxy role` (note may be a different IP than sender intended), and `id` looks useful. Maybe path header of request?
    # Know whether pod was involved in orig request by if it has history (if doesn't, note that in output)
  for ip, pod in all_pods().items():
    # Hack to position nodes nicely
    if 'ratings' in pod:
      pos = (1,0)
    elif 'productpage' in pod:
      # Looks ok when productpage has 10 replicas
      pos = (random.uniform(0,0.5),random.uniform(0,1.75))
    elif 'details' in pod:
      pos = (0.5,2)
    elif 'reviews-v1' in pod:
      pos = (1,1.8)
    elif 'reviews-v2' in pod:
      pos = (1,1.5)
    elif 'reviews-v3' in pod:
      pos = (1,1.2)

    G.add_node(ip, pod=pod, pos=pos)

  scoped_nodes = set()

  for ip, pod in all_pods().items():
    log, _, _, = run_command(f'kubectl logs -c=istio-proxy {pod}')
    log_it = iter(log)
    for line in log_it:
      if '[WTFTRACE] Sending' in line and request_id in next(log_it):
        next(log_it)
        upstream_ip = next(log_it).split(':')[1].strip()
        G.add_edge(ip, upstream_ip)
      if '[WTFTRACE] Received' in line and request_id in next(log_it):
        scoped_nodes.add(ip)

  # Only in-scope nodes are colored and labeled
  colors = ['royalblue' if node in scoped_nodes else 'gray' for node in G.nodes()]
  labels = nx.get_node_attributes(G, 'pod')
  for node_id, label in labels.items():
    labels[node_id] = f'{node_id}\n{label}' if node_id in scoped_nodes else ''

  pos=nx.get_node_attributes(G, 'pos')
  nx.draw(G, node_color=colors, labels=labels, pos=pos, font_size=10)
  #plt.savefig('trace_graph.png')
  plt.show()

def main():
  parser = argparse.ArgumentParser()
  args = parser.parse_args()

  request_id = 'fake-request-id'
  bookinfo_curl_productpage(False, request_id)
  bookinfo_curl_productpage(True, request_id)
  print_trace(request_id)

if __name__ == "__main__":
  main()
