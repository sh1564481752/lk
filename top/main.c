/*
 * Copyright (c) 2013-2015 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

/*
 * Main entry point to the OS. Initializes modules in order and creates
 * the default thread.
 */
#include <lk/main.h>

#include <app.h>
#include <arch.h>
#include <kernel/init.h>
#include <kernel/thread.h>
#include <lib/heap.h>
#include <lk/compiler.h>
#include <lk/debug.h>
#include <lk/init.h>
#include <platform.h>
#include <target.h>

/* saved boot arguments from whoever loaded the system */
ulong lk_boot_args[4];

extern void (*__ctor_list[])(void);
extern void (*__ctor_end[])(void);

static int bootstrap2(void *arg);

static void call_constructors(void) {
    void (**ctor)(void);

    ctor = __ctor_list;
    while (ctor != __ctor_end) {
        void (*func)(void);

        func = *ctor;

        func();
        ctor++;
    }
}

/* Main C entry point of the system, called from arch code on the boot cpu. */
void lk_main(ulong arg0, ulong arg1, ulong arg2, ulong arg3) {
    /* 保存启动参数 */
    lk_boot_args[0] = arg0;
    lk_boot_args[1] = arg1;
    lk_boot_args[2] = arg2;
    lk_boot_args[3] = arg3;

    /* 进入某种线程上下文 */
    kernel_init_early();

    /* 早期架构相关初始化 */
    lk_primary_cpu_init_level(LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_ARCH_EARLY - 1);
    arch_early_init();

    /* 超早期平台初始化 */
    lk_primary_cpu_init_level(LK_INIT_LEVEL_ARCH_EARLY, LK_INIT_LEVEL_PLATFORM_EARLY - 1);
    platform_early_init();

    /* 超早期目标板初始化 */
    lk_primary_cpu_init_level(LK_INIT_LEVEL_PLATFORM_EARLY, LK_INIT_LEVEL_TARGET_EARLY - 1);
    target_early_init();

#if WITH_SMP
    /* 多核系统欢迎信息 */
    dprintf(INFO, "\nwelcome to lk/MP\n\n");
#else
    /* 单核系统欢迎信息 */
    dprintf(INFO, "\nwelcome to lk\n\n");
#endif
    /* 打印启动参数 */
    dprintf(INFO, "boot args 0x%lx 0x%lx 0x%lx 0x%lx\n",
            lk_boot_args[0], lk_boot_args[1], lk_boot_args[2], lk_boot_args[3]);

    /* 初始化内核堆 */
    lk_primary_cpu_init_level(LK_INIT_LEVEL_TARGET_EARLY, LK_INIT_LEVEL_HEAP - 1);
    dprintf(SPEW, "initializing heap\n");
    heap_init();

    /* 处理静态构造函数 */
    dprintf(SPEW, "calling constructors\n");
    call_constructors();

    /* 初始化内核 */
    lk_primary_cpu_init_level(LK_INIT_LEVEL_HEAP, LK_INIT_LEVEL_KERNEL - 1);
    kernel_init();

    lk_primary_cpu_init_level(LK_INIT_LEVEL_KERNEL, LK_INIT_LEVEL_THREADING - 1);

    /* 创建完成系统初始化的线程 */
    dprintf(SPEW, "creating bootstrap completion thread\n");
    thread_t *t = thread_create("bootstrap2", &bootstrap2, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
    thread_set_pinned_cpu(t, 0);
    thread_detach(t);
    thread_resume(t);

    /* 成为idle线程并启用中断以启动调度器 */
    thread_become_idle();
}

static int bootstrap2(void *arg) {
    dprintf(SPEW, "top of bootstrap2()\n");

    /* If we have rust code, make sure it is linked in by forcing this function to be referenced. */
#if HAVE_RUST
    extern void must_link_rust(void);
    must_link_rust();
#endif

    lk_primary_cpu_init_level(LK_INIT_LEVEL_THREADING, LK_INIT_LEVEL_ARCH - 1);
    arch_init();

    /* initialize the rest of the platform */
    dprintf(SPEW, "initializing platform\n");
    lk_primary_cpu_init_level(LK_INIT_LEVEL_ARCH, LK_INIT_LEVEL_PLATFORM - 1);
    platform_init();

    /* initialize the target */
    dprintf(SPEW, "initializing target\n");
    lk_primary_cpu_init_level(LK_INIT_LEVEL_PLATFORM, LK_INIT_LEVEL_TARGET - 1);
    target_init();

    dprintf(SPEW, "initializing apps\n");
    lk_primary_cpu_init_level(LK_INIT_LEVEL_TARGET, LK_INIT_LEVEL_APPS - 1);
    apps_init();

    lk_primary_cpu_init_level(LK_INIT_LEVEL_APPS, LK_INIT_LEVEL_LAST);

    return 0;
}