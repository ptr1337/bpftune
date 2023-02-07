/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2021, Oracle and/or its affiliates. */

#define __KERNEL__
#if defined(__x86__64)
#include "vmlinux_x86_64.h"
#elif defined(__aarch64__)
#include "vmlinux_aarch64.h"
#endif

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#include "bpftune.h"
#include "corr.h"

struct {
        __uint(type, BPF_MAP_TYPE_RINGBUF);
        __uint(max_entries, 64 * 1024);
} ring_buffer_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, struct corr_key);
	__type(value, struct corr);
} corr_map SEC(".maps");

unsigned int tuner_id;

/* TCP buffer tuning */
#ifndef SO_SNDBUF
#define SO_SNDBUF       	7
#endif
#ifndef SO_RCVBUF
#define SO_RCVBUF       	8
#endif

#ifndef SOCK_SNDBUF_LOCK
#define SOCK_SNDBUF_LOCK	1
#endif
#ifndef SOCK_RCVBUF_LOCK
#define SOCK_RCVBUF_LOCK	2
#endif

#ifndef SK_MEM_QUANTUM
#define SK_MEM_QUANTUM		4096
#endif
#ifndef SK_MEM_QUANTUM_SHIFT
#define SK_MEM_QUANTUM_SHIFT	ilog2(SK_MEM_QUANTUM)
#endif

#ifndef SOL_TCP
#define SOL_TCP        		6
#endif

#ifndef TCP_CONGESTION
#define TCP_CONGESTION		13
#endif

#ifndef AF_INET
#define AF_INET			2
#endif
#ifndef AF_INET6
#define AF_INET6		10
#endif

#define sk_family		__sk_common.skc_family
#define sk_rmem_alloc		sk_backlog.rmem_alloc
#define sk_state		__sk_common.skc_state
#define sk_daddr		__sk_common.skc_daddr
#define sk_v6_daddr		__sk_common.skc_v6_daddr
#define sk_net			__sk_common.skc_net
#define sk_prot			__sk_common.skc_prot

#ifndef s6_addr32
#define s6_addr32		in6_u.u6_addr32
#endif

/* TCP congestion algorithm tuning */
#ifndef TCP_CA_NAME_MAX
#define TCP_CA_NAME_MAX		16
#endif

/* neigh table tuning */
#ifndef NUD_PERMANENT
#define NUD_PERMANENT	0x80
#endif
#ifndef NTF_EXT_LEARNED
#define NTF_EXT_LEARNED	0x10
#endif

#define EINVAL		22

bool debug;

#define bpftune_log(...)	if (debug) __bpf_printk(__VA_ARGS__)

static __always_inline long get_netns_cookie(struct net *net)
{
	return net ? net->net_cookie : 0;
}
 
#define last_event_key(nscookie, tuner, event)	\
	((__u64)nscookie | ((__u64)event << 32) |((__u64)tuner <<48))

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 65536);
	__type(key, __u64);
	__type(value, __u64);
} last_event_map SEC(".maps");

static __always_inline void send_sysctl_event(struct sock *sk,
					      int scenario_id, int event_id,
					      long *old, long *new,
					      struct bpftune_event *event)
{
	struct net *net = sk ? sk->sk_net.net : NULL;
	__u64 now = bpf_ktime_get_ns();
	__u64 event_key = 0;
	long nscookie = 0;
	__u64 *last_timep = NULL;
	int ret = 0;

	nscookie = get_netns_cookie(net);

	event_key = last_event_key(nscookie, tuner_id, event_id);
	/* avoid sending same event for same tuner+netns in < 25msec */
	last_timep = bpf_map_lookup_elem(&last_event_map, &event_key);
	if (last_timep) {
		if ((now - *last_timep) < (25 * MSEC))
			return;
		*last_timep = now;
	} else {
		bpf_map_update_elem(&last_event_map, &event_key, &now, 0);
	}

	event->tuner_id = tuner_id;
	event->scenario_id = scenario_id;
	event->netns_cookie = nscookie;
	event->update[0].id = event_id;
	event->update[0].old[0] = old[0];
	event->update[0].old[1] = old[1];
	event->update[0].old[2] = old[2];
	event->update[0].new[0] = new[0];
	event->update[0].new[1] = new[1];
	event->update[0].new[2] = new[2];
	ret = bpf_ringbuf_output(&ring_buffer_map, event, sizeof(*event), 0);
	bpftune_log("tuner [%d] scenario [%d]: event send: %d ",
		    tuner_id, scenario_id, ret);
	bpftune_log("\told '%d %d %d'\n", old[0], old[1], old[2]);
	bpftune_log("\tnew '%d %d %d'\n", new[0], new[1], new[2]);
}

static inline void corr_update_bpf(__u64 id, __u64 netns_cookie,
				   __u64 x, __u64 y)
{
	struct corr_key key = { .id = id, .netns_cookie = netns_cookie };
	struct corr *corrp = bpf_map_lookup_elem(&corr_map, &key);

	if (!corrp) {
		struct corr corr = {};

		bpf_map_update_elem(&corr_map, &key, &corr, 0);

		corrp = bpf_map_lookup_elem(&corr_map, &key);
		if (!corrp)
			return;
	}
	corr_update(corrp, x, y);
}

char _license[] SEC("license") = "Dual BSD/GPL";
