#!/usr/bin/env python3
"""
Assumes app is already deployed.
- Send a bunch of normal requests
- Trace one of the failed ones
- Gather the "bits", i.e. scoped pod names and interesting log statements
- Plot
"""

import argparse
import subprocess
import networkx as nx
import matplotlib.pyplot as plt
import random
import re

G = nx.MultiDiGraph()

def bookinfo_curl_productpage(is_trace, request_id):
  cmd = 'kubectl exec "$(kubectl get pod -l app=ratings -o jsonpath=\'{.items[0].metadata.name}\')" -c ratings -- curl -sS productpage:9080/productpage'
  cmd += f' -H \'x-request-id: {"WTFTRACE-" if is_trace else ""}{request_id}\''

  stdout, stderr, ret = run_command(cmd)

  if not is_trace:
    if '<html>' not in stdout:
      # e.g. productpage sends 503
      print(f'Request {request_id} failed: no html')
      return False
    for line in stdout:
      # e.g. details sends 503
      if 'details are currently unavailable' in line:
        print(f'Failed details request: {request_id}')
        return False

  return True

def run_command(command):
    proc = subprocess.run([command], shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    stdout = proc.stdout.strip().split('\n')
    stderr = proc.stderr.strip().split('\n')

    return stdout, stderr, proc.returncode

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
    # Hack to position nodes nicely (can rerun if turns out ugly)
    if 'ratings' in pod:
      pos = (random.uniform(0.34,0.68),random.uniform(0,0.25))
    elif 'productpage' in pod:
      # Looks ok when productpage has 10 replicas
      pos = (random.uniform(0,0.25),random.uniform(0.34, 0.68))
    elif 'details' in pod:
      pos = (random.uniform(0.34,0.68),random.uniform(0.85, 1))
    elif 'reviews' in pod:
      pos = (random.uniform(0.34,1),random.uniform(0.34, 0.68))

    G.add_node(ip, pod=pod, pos=pos)

  scoped_nodes = set()
  no_history_nodes = set()
  unhealthy_nodes = set()

  for ip, pod in all_pods().items():
    log, _, _, = run_command(f'kubectl logs -c=istio-proxy {pod}')
    log_it = iter(log)
    for line in log_it:
      # TODO change parsing to search for any bad http code; if sent, set own bit. If recvd, set neighbor
      if 'upstream_reset_before_response_started' in line:
        upstream_ip = re.findall(r'"(.+?)"', line)[-1].split(':')[0]
        # TODO try istio's json log output option - may be more parsable
        unhealthy_nodes.add(ip)
        G.add_edge(ip, upstream_ip, rad=0)

      if not f'[WTFTRACE] {request_id}' in line:
        continue
      line = next(log_it)
      if 'Sending' in line:
        next(log_it)
        upstream_ip = next(log_it).split(':')[1].strip()
        G.add_edge(ip, upstream_ip, rad=0.1)
        scoped_nodes.add(ip)
        scoped_nodes.add(upstream_ip)
      # Not doing anything with received traces for now
      elif 'no history' in line:
        no_history_nodes.add(ip)

  labels = nx.get_node_attributes(G, 'pod')
  colors = ['tab:red' if node in unhealthy_nodes else 'gray' for node in G.nodes()]
  sizes = [300 if node in scoped_nodes else 30 for node in G.nodes()]
  for node_id, label in labels.items():
    # Only in-scope nodes are labeled
    label = f'{node_id}\n{label}' if node_id in scoped_nodes else ''
    if node_id in no_history_nodes:
      label += '\nNo history'
    labels[node_id] = label

  pos=nx.get_node_attributes(G, 'pos')
  plt.title(f'Trace {request_id}')
  nx.draw_networkx_nodes(G, pos, node_color=colors, node_size=sizes)
  nx.draw_networkx_labels(G, pos, labels=labels)
  for edge in G.edges(data=True):
    edge_color = 'tab:red' if edge[2]["rad"] == 0 else 'tab:blue'
    nx.draw_networkx_edges(G, pos, edgelist=[(edge[0],edge[1])], connectionstyle=f'arc3, rad = {edge[2]["rad"]}',
                           edge_color=edge_color)

  #plt.savefig('trace_graph.png')
  plt.show()

def main():
  parser = argparse.ArgumentParser()
  args = parser.parse_args()

  # 1. Do a bunch of normal requests
  NREQUESTS = 100
  failed_requests = []
  for i in range(NREQUESTS):
    request_id = f'test-request-{i}'
    if not bookinfo_curl_productpage(False, request_id):
      failed_requests.append(request_id)

  if len(failed_requests) == 0:
    print('No requests failed - nothing to trace')

  # 2. Trace one of the failed requests
  trace_request = failed_requests[-1]
  bookinfo_curl_productpage(True, trace_request)
  print_trace(trace_request)


if __name__ == "__main__":
  main()
