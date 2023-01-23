# L4Re NVMe server

nvme-driver is an L4Re server that consists of two parts. On one side there is
the hardware driver itself that communicates with the NVMe controller and
provides the interrupt handling. On the other side a libblock-device interface
has been implemented for interfacing with L4virtio clients.

# Documentation

This package is part of the L4Re Operating System Framework. For documentation
and build instructions see the [L4Re
wiki](https://kernkonzept.com/L4Re/guides/l4re).

# Contributions

We welcome contributions. Please see our contributors guide on
[how to contribute](https://kernkonzept.com/L4Re/contributing/l4re).

# License

Detailed licensing and copyright information can be found in
the [LICENSE](LICENSE.spdx) file.
