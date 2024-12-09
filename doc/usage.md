# NVMe server {#l4re_servers_nvme_driver}

[comment]: # (This is a generated file. Do not change it.)
[comment]: # (Instead, change capdb.yml.)

The NVMe server is a driver for PCI Express NVMe controllers.

The NVMe server is capable of exposing entire disks (i.e. NVMe namespaces) (by
serial number and namespace identifier) or individual partitions (by their
partition UUID) of a hard drive to clients via the Virtio block interface.

The server consists of two parts. The first one is the hardware driver itself
that takes care of the communication with the underlying hardware and interrupt
handling. The second part implements a virtual block device and is responsible
to communicate with clients. The virtual block device translates commands it
receives into NVMe requests and issues them to the hardware driver.

The NVMe server allows both statically and dynamically configured clients. A
static configuration is given priority over dynamically connecting clients and
configured while the service starts. Dynamic clients can connect and disconnect
during runtime of the NVMe server.

## Building and Configuration

The NVMe server can be built using the L4Re build system. Just place
this project into your `pkg` directory. The resulting binary is called
`nvme-drv`

## Starting the service

The NVMe server can be started with Lua like this:

```lua
local nvme_bus = L4.default_loader:new_channel();
L4.default_loader:start({
  caps = {
    vbus = vbus_nvme,
    svr = nvme_bus:svr(),
  },
},
"rom/nvme-drv");
```

First an IPC gate (`nvme_bus`) is created which is used between the NVMe server
and a client to request access to a particular disk or partition. The
server-side is assigned to the mandatory `svr` capability of the NVMe server.
See the section below on how to configure access to a disk or partition.

The NVMe server needs access to a virtual bus capability (`vbus`). On the
virtual bus the NVMe server searches for NVMe compliant storage controllers.
Please see io's documentation about how to setup a virtual bus.


## Capabilities

* `vbus`

  Virtual bus capability

  Mandatory capability.

* `dataspace`

  Trusted dataspaces

  Multiple capability names can be provided by the `--register-ds` command line parameter.

* `client`

  Static client

  Multiple capability names can be provided by the `--client` command line parameter.

* `svr`

  Server Capability of application. Endpoint for IPC calls

  Mandatory capability.


## Command Line Options

In the example above the NVMe server is started in its default configuration.
To customize the configuration of the NVMe-server it accepts the following
command line options:

* `-v`, `--verbose`

  Enable verbose mode. You can repeat this option to increase verbosity up
  to trace level.

  Can be used up to 3 times.

  Flag. True if provided.

* `-q`, `--quiet`

  This option enables the quiet mode. All output is silenced.

  Flag. True if provided.

* `--client <cap_name>`

  Connect a static client.

  Can be used multiple times.

  Name of a provided capability with server rights that adheres to the ipc protocol.

  This parameter opens a scope for the following subparameters:

  * `--device <UUID | <SN>:n<NAMESPACE_ID>>`

    This option denotes the partition UUID or serial number of the preceding
    `client` option followed by a colon, letter 'n' and the identifier of the
    requested NVMe namespace.

    String value.

  * `--ds-max <max>`

    This option sets the upper limit of the number of dataspaces the client is
    able to register with the NVMe server for virtio DMA.

    Numerical value.

  * `--readonly`

    This option sets the access to disks or partitions to read only for the
    preceding `client` option.

    Flag. True if provided.

* `--nosgl`

  This option disables support for SGLs.

  Flag. True if provided.

* `--nomsi`

  This option disables support for MSI interrupts.

  Flag. True if provided.

* `--nomsix`

  This option disables support for MSI-X interrupts.

  Flag. True if provided.

* `-d <cap_name>`, `--register-ds <cap_name>`

  This option registers a trusted dataspace capability. If this option gets
  used, it is not possible to communicate to the driver via dataspaces other
  than the registered ones. Can be used multiple times for multiple dataspaces.

  The option's parameter is the name of a dataspace capability.

  Can be used multiple times.

  Name of a provided capability that adheres to the dataspace protocol.

## Virtio block host

Prior to connecting a client to a virtual block session it has to be created
using the following Lua function. It has to be called on the client side of the
IPC gate capability whose server side is bound to the NVMe server.

Call:   `create(0, "device=<UUID | <SN>:n<NAMESPACE_ID>>" [, "ds-max=<max>", "read-only"])`

* `"device=<UUID | <SN>:n<NAMESPACE_ID>>"`

  This string denotes either a partition UUID, or a disk serial number the
  client wants to be exported via the Virtio block interface followed by a
  colon, letter 'n' and the identifier of the requested NVMe namespace.

  String value.

* `"ds-max=<max>"`

  Specifies the upper limit of the number of dataspaces the client is allowed
  to register with the NVMe server for virtio DMA.

  Numerical value.

  Default: `2`

* `"read-only"`

  This string sets the access to disks or partitions to read only for the
  client.

  Flag. True if provided.

If the `create()` call is successful a new capability which references an NVMe
virtio device is returned. A client uses this capability to communicate with
the NVMe server using the Virtio block protocol.



## Examples

A couple of examples on how to request different disks or partitions are listed
below.

* Request a partition with the given UUID

```lua
vda1 = nvme_bus:create(0, "ds-max=5", "device=88E59675-4DC8-469A-98E4-B7B021DC7FBE")
```

* Request complete namespace with the given serial number

```lua
vda = nvme_bus:create(0, "ds-max=4", "device=1234:n1")
```

* A more elaborate example with a static client. The client uses the client
  side of the `nvme_cl1` capability to communicate with the NVMe server.

  ```
  local nvme_cl1 = L4.default_loader:new_channel();
  local nvme_bus = L4.default_loader:new_channel();
  L4.default_loader:start({
    caps = {
      vbus = vbus_nvme,
      svr = nvme_bus:svr(),
      cl1 = nvme_cl1:svr(),
    },
  },
  "rom/nvme-drv --client cl1 --device 88E59675-4DC8-469A-98E4-B7B021DC7FBE --ds-max 5");
  ```

