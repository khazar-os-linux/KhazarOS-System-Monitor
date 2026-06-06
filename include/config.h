#ifndef CONFIG_H
#define CONFIG_H

/* -------------------------------------------------------------------
 *  Global configuration constants
 * ------------------------------------------------------------------*/

/** Number of data points kept for each history graph */
#define MAX_POINTS 60

/** Maximum number of CPU cores the monitor will track */
#define MAX_CPU_CORES 64

/** Maximum number of disks exposed simultaneously */
#define MAX_DISKS 8

/** Maximum number of network interfaces tracked */
#define MAX_INTERFACES 8

/* GPU kept the same name for backward compat */
#ifndef GPU_MAX_POINTS
#define GPU_MAX_POINTS MAX_POINTS
#endif

#endif /* CONFIG_H */