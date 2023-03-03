/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2023, Oracle and/or its affiliates. */

#include "bpftune.bpf.h"
#include "netns_tuner.h"

#ifdef BPFTUNE_LEGACY

BPF_MAP_DEF(setup_net_map, BPF_MAP_TYPE_HASH, __u64, __u64, 65536);

SEC("kprobe/setup_net")
int BPF_KPROBE(bpftune_setup_net, struct net *net, struct user_namespace *user_ns)
{
	__u64 current = bpf_get_current_task();

	if (!current)
		return 0;
	bpf_map_update_elem(&setup_net_map, &current, &net, 0);
	return 0;
}

SEC("kretprobe/setup_net")
int BPF_KRETPROBE(bpftune_setup_net_return, int ret)
{
	struct bpftune_event event = {};
	__u64 current, *netnsp;
	struct net *netns;
	
	if (ret != 0)
		return 0;
	current = bpf_get_current_task();

	netnsp = bpf_map_lookup_elem(&setup_net_map, &current);
	if (!netnsp)
		return 0;

	netns = (struct net *)*netnsp;
	event.tuner_id = tuner_id;
	event.pid = bpf_get_current_pid_tgid() >> 32;
	event.scenario_id = NETNS_SCENARIO_CREATE;
	event.netns_cookie = get_netns_cookie(netns);
	if (event.netns_cookie >= 0)
		bpf_ringbuf_output(&ring_buffer_map, &event, sizeof(event), 0);

	return 0;
}

#else
SEC("fexit/setup_net")
int BPF_PROG(bpftune_setup_net, struct net *net, struct user_namespace *user_ns,
	     int ret)
{
	struct bpftune_event event = {};

	if (ret != 0 || net == NULL || net == &init_net)
		return 0;

	event.tuner_id = tuner_id;
	event.pid = bpf_get_current_pid_tgid() >> 32;
	event.scenario_id = NETNS_SCENARIO_CREATE;
	event.netns_cookie = get_netns_cookie(net);
	if (event.netns_cookie >= 0)
		bpf_ringbuf_output(&ring_buffer_map, &event, sizeof(event), 0);

	return 0;
}
#endif

BPF_FENTRY(net_free, struct net *net)
{
	struct bpftune_event event = {};

	if (!net)
		return 0;

	event.tuner_id = tuner_id;
	event.scenario_id = NETNS_SCENARIO_DESTROY;
	event.netns_cookie = get_netns_cookie(net);
	if (event.netns_cookie >= 0)
		bpf_ringbuf_output(&ring_buffer_map, &event, sizeof(event), 0);

	return 0;
}
