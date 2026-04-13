#ifndef MONITOR_IOCTL_H
#define MONITOR_IOCTL_H

/*
 * monitor_ioctl.h — Shared ioctl definitions for OS-Jackfruit
 *
 * Included by BOTH:
 *   monitor.c  (kernel module)
 *   engine.c   (user-space runtime)
 *
 * Do NOT add kernel-only or user-only headers here.
 */

#include <linux/ioctl.h>

#define MONITOR_IOC_MAGIC  'M'

/* Payload for REGISTER and UNREGISTER commands */
struct monitor_entry {
    pid_t  pid;        /* Host PID of the container process */
    long   soft_kib;   /* Soft memory limit in KiB — triggers warning */
    long   hard_kib;   /* Hard memory limit in KiB — triggers SIGKILL */
};

/* Register a PID with soft/hard memory limits */
#define MONITOR_IOC_REGISTER   _IOW(MONITOR_IOC_MAGIC, 1, struct monitor_entry)

/* Unregister a PID (call when container exits) */
#define MONITOR_IOC_UNREGISTER _IOW(MONITOR_IOC_MAGIC, 2, pid_t)

#endif /* MONITOR_IOCTL_H */
