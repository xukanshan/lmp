// Copyright 2023 The LMP Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://github.com/linuxkerneltravel/lmp/blob/develop/LICENSE
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// author: nanshuaibo811@163.com
//
// Kernel space BPF program used for monitoring data for vCPU.

#ifndef __KVM_VCPU_H
#define __KVM_VCPU_H

#include "kvm_watcher.h"
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>

struct vcpu_wakeup {
    u64 pad;
    __u64 ns;
    bool waited;
    bool vaild;
};

struct halt_poll_ns {
    u64 pad;
    bool grow;
    unsigned int vcpu_id;
    unsigned int new;
    unsigned int old;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, u64);
    __type(value, u32);
} count_dirty_map SEC(".maps");

static int trace_kvm_vcpu_wakeup(struct vcpu_wakeup *ctx, void *rb,
                                 pid_t vm_pid) {
    CHECK_PID(vm_pid) {
        u32 tid = bpf_get_current_pid_tgid();
        struct vcpu_wakeup_event *e;
        RESERVE_RINGBUF_ENTRY(rb, e);
        u64 hlt_time = bpf_ktime_get_ns();
        e->waited = ctx->waited;
        e->process.pid = pid;
        e->process.tid = tid;
        e->dur_hlt_ns = ctx->ns;
        e->hlt_time = hlt_time;
        bpf_get_current_comm(&e->process.comm, sizeof(e->process.comm));
        bpf_ringbuf_submit(e, 0);
    }
    return 0;
}

static int trace_kvm_halt_poll_ns(struct halt_poll_ns *ctx, void *rb,
                                  pid_t vm_pid) {
    CHECK_PID(vm_pid) {
        u32 tid = bpf_get_current_pid_tgid();
        struct halt_poll_ns_event *e;
        RESERVE_RINGBUF_ENTRY(rb, e);
        u64 time = bpf_ktime_get_ns();
        e->process.pid = pid;
        e->process.tid = tid;
        e->time = time;
        e->grow = ctx->grow;
        e->old = ctx->old;
        e->new = ctx->new;
        e->vcpu_id = ctx->vcpu_id;
        bpf_get_current_comm(&e->process.comm, sizeof(e->process.comm));
        bpf_ringbuf_submit(e, 0);
    }
    return 0;
}

static int trace_mark_page_dirty_in_slot(struct kvm *kvm,
                                         const struct kvm_memory_slot *memslot,
                                         gfn_t gfn, void *rb, pid_t vm_pid) {
    CHECK_PID(vm_pid) {
        u32 flags;
        struct kvm_memory_slot *slot;
        bpf_probe_read_kernel(&slot, sizeof(memslot), &memslot);
        bpf_probe_read_kernel(&flags, sizeof(memslot->flags), &memslot->flags);
        if (slot && (flags & KVM_MEM_LOG_DIRTY_PAGES)) {  // 检查memslot是否启用了脏页追踪
            gfn_t gfnum = gfn;
            u32 *count = bpf_map_lookup_elem(&count_dirty_map, &gfnum);
            if (count) {
                *count += 1;
            } else {
                u32 init_count = 1;
                bpf_map_update_elem(&count_dirty_map, &gfnum, &init_count,
                                    BPF_ANY);
            }
            u32 tid = bpf_get_current_pid_tgid();
            unsigned long base_gfn;
            struct mark_page_dirty_in_slot_event *e;
            RESERVE_RINGBUF_ENTRY(rb, e);
            u64 time = bpf_ktime_get_ns();
            e->process.pid = pid;
            e->process.tid = tid;
            e->time = time;
            e->gfn = gfn;
            bpf_probe_read_kernel(&base_gfn, sizeof(memslot->base_gfn),
                                  &memslot->base_gfn);
            e->rel_gfn = gfn - base_gfn;
            bpf_probe_read_kernel(&e->npages, sizeof(memslot->npages),
                                  &memslot->npages);
            bpf_probe_read_kernel(&e->userspace_addr,
                                  sizeof(memslot->userspace_addr),
                                  &memslot->userspace_addr);
            bpf_probe_read_kernel(&e->slot_id, sizeof(memslot->id),
                                  &memslot->id);
            bpf_get_current_comm(&e->process.comm, sizeof(e->process.comm));
            bpf_ringbuf_submit(e, 0);
        }
    }
    return 0;
}
#endif /* __KVM_VCPU_H */
