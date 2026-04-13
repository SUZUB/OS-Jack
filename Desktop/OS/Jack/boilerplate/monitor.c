/*
 * monitor.c — OS-Jackfruit Linux Kernel Module
 *
 * Tracks container PIDs and enforces soft/hard memory limits via ioctl.
 *
 * Device: /dev/container_monitor
 * ioctl commands: MONITOR_IOC_REGISTER, MONITOR_IOC_UNREGISTER
 *
 * Build: make kmod
 * Load:  sudo insmod monitor.ko
 * Check: dmesg | tail
 * Unload: sudo rmmod monitor
 *
 * WARNING: Kernel module code runs in kernel space.
 *          Bugs here can kernel panic your VM.
 *          Always test on a VM snapshot.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/pid.h>

#include "monitor_ioctl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS-Jackfruit Team");
MODULE_DESCRIPTION("Container memory monitor — tracks PIDs, enforces memory limits");
MODULE_VERSION("1.0");

/* ── Tracked process entry ────────────────────────────────────────────────── */
struct tracked_proc {
    pid_t            pid;
    long             soft_kib;
    long             hard_kib;
    bool             soft_warned;
    struct list_head list;
};

/* ── Module globals ───────────────────────────────────────────────────────── */
static LIST_HEAD(proc_list);
static DEFINE_MUTEX(proc_list_mutex);
static struct task_struct *monitor_thread;

/* ── RSS polling thread ───────────────────────────────────────────────────── */
static int monitor_thread_fn(void *data)
{
    (void)data;

    pr_info("container_monitor: polling thread started\n");

    while (!kthread_should_stop()) {
        struct tracked_proc *entry, *tmp;

        mutex_lock(&proc_list_mutex);
        list_for_each_entry_safe(entry, tmp, &proc_list, list) {
            struct task_struct *task;
            long rss_kib = 0;

            rcu_read_lock();
            task = pid_task(find_vpid(entry->pid), PIDTYPE_PID);
            if (task && task->mm) {
                /* get_mm_rss returns pages; convert to KiB */
                rss_kib = (long)get_mm_rss(task->mm) * (PAGE_SIZE / 1024);
            }
            rcu_read_unlock();

            if (!task) {
                /* Process no longer exists — remove from list */
                pr_info("container_monitor: PID %d no longer exists, removing\n",
                        entry->pid);
                list_del(&entry->list);
                kfree(entry);
                continue;
            }

            /* Soft limit check */
            if (rss_kib > entry->soft_kib && !entry->soft_warned) {
                pr_warn("container_monitor: PID %d exceeded soft limit "
                        "(%ld KiB > %ld KiB)\n",
                        entry->pid, rss_kib, entry->soft_kib);
                entry->soft_warned = true;
            }

            /* Hard limit check */
            if (rss_kib > entry->hard_kib) {
                pr_warn("container_monitor: PID %d exceeded hard limit "
                        "(%ld KiB > %ld KiB) — sending SIGKILL\n",
                        entry->pid, rss_kib, entry->hard_kib);

                rcu_read_lock();
                task = pid_task(find_vpid(entry->pid), PIDTYPE_PID);
                if (task)
                    send_sig(SIGKILL, task, 1);
                rcu_read_unlock();

                list_del(&entry->list);
                kfree(entry);
            }
        }
        mutex_unlock(&proc_list_mutex);

        msleep(1000);  /* poll every 1 second */
    }

    pr_info("container_monitor: polling thread stopped\n");
    return 0;
}

/* ── ioctl handler ────────────────────────────────────────────────────────── */
static long monitor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    (void)file;

    switch (cmd) {

    case MONITOR_IOC_REGISTER: {
        struct monitor_entry entry;
        struct tracked_proc  *proc;

        if (copy_from_user(&entry, (void __user *)arg, sizeof(entry)))
            return -EFAULT;

        proc = kmalloc(sizeof(*proc), GFP_KERNEL);
        if (!proc)
            return -ENOMEM;

        proc->pid        = entry.pid;
        proc->soft_kib   = entry.soft_kib;
        proc->hard_kib   = entry.hard_kib;
        proc->soft_warned = false;
        INIT_LIST_HEAD(&proc->list);

        mutex_lock(&proc_list_mutex);
        list_add_tail(&proc->list, &proc_list);
        mutex_unlock(&proc_list_mutex);

        pr_info("container_monitor: registered PID %d (soft=%ld KiB, hard=%ld KiB)\n",
                entry.pid, entry.soft_kib, entry.hard_kib);
        return 0;
    }

    case MONITOR_IOC_UNREGISTER: {
        pid_t target_pid;
        struct tracked_proc *entry, *tmp;

        if (copy_from_user(&target_pid, (void __user *)arg, sizeof(target_pid)))
            return -EFAULT;

        mutex_lock(&proc_list_mutex);
        list_for_each_entry_safe(entry, tmp, &proc_list, list) {
            if (entry->pid == target_pid) {
                list_del(&entry->list);
                kfree(entry);
                pr_info("container_monitor: unregistered PID %d\n", target_pid);
                mutex_unlock(&proc_list_mutex);
                return 0;
            }
        }
        mutex_unlock(&proc_list_mutex);

        pr_warn("container_monitor: PID %d not found for unregister\n", target_pid);
        return -ENOENT;
    }

    default:
        return -ENOTTY;
    }
}

/* ── File operations ──────────────────────────────────────────────────────── */
static int monitor_open(struct inode *inode, struct file *file)
{
    (void)inode; (void)file;
    return 0;
}

static int monitor_release(struct inode *inode, struct file *file)
{
    (void)inode; (void)file;
    return 0;
}

static const struct file_operations monitor_fops = {
    .owner          = THIS_MODULE,
    .open           = monitor_open,
    .release        = monitor_release,
    .unlocked_ioctl = monitor_ioctl,
};

/* ── Misc device ──────────────────────────────────────────────────────────── */
static struct miscdevice monitor_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "container_monitor",
    .fops  = &monitor_fops,
};

/* ── Module init ──────────────────────────────────────────────────────────── */
static int __init monitor_init(void)
{
    int ret;

    ret = misc_register(&monitor_dev);
    if (ret) {
        pr_err("container_monitor: failed to register misc device (%d)\n", ret);
        return ret;
    }

    monitor_thread = kthread_run(monitor_thread_fn, NULL, "container_monitor");
    if (IS_ERR(monitor_thread)) {
        pr_err("container_monitor: failed to start polling thread\n");
        misc_deregister(&monitor_dev);
        return PTR_ERR(monitor_thread);
    }

    pr_info("container_monitor: module loaded — /dev/container_monitor ready\n");
    return 0;
}

/* ── Module exit ──────────────────────────────────────────────────────────── */
static void __exit monitor_exit(void)
{
    struct tracked_proc *entry, *tmp;

    /* Stop polling thread first */
    kthread_stop(monitor_thread);

    /* Free all tracked entries */
    mutex_lock(&proc_list_mutex);
    list_for_each_entry_safe(entry, tmp, &proc_list, list) {
        list_del(&entry->list);
        kfree(entry);
    }
    mutex_unlock(&proc_list_mutex);

    misc_deregister(&monitor_dev);
    pr_info("container_monitor: module unloaded\n");
}

module_init(monitor_init);
module_exit(monitor_exit);
