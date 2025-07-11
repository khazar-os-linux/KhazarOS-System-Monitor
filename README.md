## ⚠️ ATTENTION!

This application is under development. Errors and bugs may occour.

# KhazarOS System Monitor

A lightweight system monitoring application for Linux that provides real-time information about your system's performance.

## Features

- **CPU Monitoring**: Track CPU usage with real-time graphs  
- **Memory Monitoring**: Monitor RAM usage and swap space  
- **Disk Monitoring**: Track disk usage and I/O performance  
- **Network Monitoring**: Monitor network traffic with download/upload speeds  
- **GPU Monitoring**: Track GPU and Memory usage on these supported GPUs:
  - NVIDIA GPUs (via `nvidia-smi`)
  - AMD GPUs (via `sysfs`) (not tested)
  - Intel GPUs (via `intel_gpu_top` and system information)(not tested on Intel GPU. App tested only intel İntegrated GPU)
  - Multiple GPU support with separate tabs for each detected GPU (expremental)(not tested)

## Dependencies

- GTK3  
- GLib  
- For NVIDIA GPU monitoring: `nvidia-smi` (not tested on OSS NVIDIA kernel modules)
- For Intel GPU monitoring: `intel-gpu-tools` (optional, for better monitoring)

## Building

```bash
make
````

## Installing

```bash
sudo make install
```

## Uninstalling

```bash
sudo make uninstall
```

## Running

```bash
./system-monitor
```

## Detailed Features

### GPU Monitoring

* Displays GPU usage percentage with a filled graph
* Displays VRAM usage percentage with a separate filled graph
* Shows GPU specifications including:

  * GPU name (shortened for better display)
  * Vendor
  * Driver version
  * VRAM usage and total
* Supports multiple GPUs with separate tabs for each GPU

### Network Monitoring

* Displays download and upload speeds in Mbps
* Shows network adapter specifications
* Tracks total data transferred

## License

This project is licensed under the **GPL-3.0** license.
