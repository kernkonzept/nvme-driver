/*
 * Copyright (C) 2020, 2022 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Jakub Jermar <jakub.jermar@kernkonzept.com>
 *            Manuel von Oltersdorff-Kalettka <manuel.kalettka@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */

#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <vector>

#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/br_manager>
#include <l4/re/util/object_registry>
#include <l4/re/util/shared_cap>
#include <l4/vbus/vbus>
#include <l4/vbus/vbus_pci>

#include "nvme_device.h"
#include "ctl.h"

#include "debug.h" // needs to come before liblock-dev includes
#include <l4/libblock-device/block_device_mgr.h>
#include <l4/libblock-device/virtio_client.h>

static char const *usage_str =
"Usage: %s [-vq] [--client CAP --device UUID [--ds-max NUM] [--readonly]] [--nosgl]\n\n"
"Options:\n"
" -v                 Verbose mode.\n"
" -q                 Quiet mode (do not print any warnings).\n"
" --client CAP       Add a static client via the CAP capability\n"
" --device UUID      Specify the UUID of the device or partition\n"
" --ds-max NUM       Specify maximum number of dataspaces the client can register\n"
" --readonly         Only allow readonly access to the device\n"
" --nosgl            Disable support for SGLs\n"
" --register-ds CAP  Register a trusted dataspace capability\n";

using Base_device_mgr = Block_device::Device_mgr<
  Nvme::Nvme_base_device,
  Block_device::Partitionable_factory<Nvme::Nvme_base_device>>;

using Ds_vector = std::vector<L4::Cap<L4Re::Dataspace>>;
static std::shared_ptr<Ds_vector> trusted_dataspaces;

class Blk_mgr
: public Base_device_mgr,
  public L4::Epiface_t<Blk_mgr, L4::Factory>
{
public:
  Blk_mgr(L4Re::Util::Object_registry *registry)
  : Base_device_mgr(registry)
  {}

  long op_create(L4::Factory::Rights, L4::Ipc::Cap<void> &res, l4_umword_t,
                 L4::Ipc::Varg_list_ref valist)
  {
    Dbg::trace().printf("Client requests connection.\n");

    // default values
    std::string device;
    int num_ds = 2;
    bool readonly = false;

    for (L4::Ipc::Varg p: valist)
      {
        if (!p.is_of<char const *>())
          {
            Dbg::warn().printf("String parameter expected.\n");
            return -L4_EINVAL;
          }

        if (parse_string_param(p, "device=", &device))
          continue;
        if (parse_int_param(p, "ds-max=", &num_ds))
          {
            if (num_ds <= 0 || num_ds > 256) // sanity check with arbitrary limit
              {
                Dbg::warn().printf("Invalid range for parameter 'ds-max'. "
                                   "Number must be between 1 and 256.\n");
                return -L4_EINVAL;
              }
            continue;
          }
        if (strncmp(p.value<char const *>(), "read-only", p.length()) == 0)
          readonly = true;
      }

    if (device.empty())
      {
        Dbg::warn().printf("Parameter 'device=' not found. "
                           "Device UUID is required.\n");
        return -L4_EINVAL;
      }

    L4::Cap<void> cap;
    int ret = create_dynamic_client(device, -1, num_ds, &cap, readonly,
                                    [](Nvme::Nvme_base_device *) {},
                                    !trusted_dataspaces->empty(),
                                    trusted_dataspaces);
    if (ret >= 0)
      res = L4::Ipc::make_cap(cap, L4_CAP_FPAGE_RWSD);

    return (ret == -L4_ENODEV && _scan_in_progress) ? -L4_EAGAIN : ret;
  }

  void scan_finished()
  { _scan_in_progress = false; }

private:
  static bool parse_string_param(L4::Ipc::Varg const &param, char const *prefix,
                                 std::string *out)
  {
    l4_size_t headlen = strlen(prefix);

    if (param.length() < headlen)
      return false;

    char const *pstr = param.value<char const *>();

    if (strncmp(pstr, prefix, headlen) != 0)
      return false;

    *out = std::string(pstr + headlen, strnlen(pstr, param.length()) - headlen);

    return true;
  }

  static bool parse_int_param(L4::Ipc::Varg const &param, char const *prefix,
                              int *out)
  {
    l4_size_t headlen = strlen(prefix);

    if (param.length() < headlen)
      return false;

    char const *pstr = param.value<char const *>();

    if (strncmp(pstr, prefix, headlen) != 0)
      return false;

    std::string tail(pstr + headlen, param.length() - headlen);

    char *endp;
    long num = strtol(tail.c_str(), &endp, 10);

    if (num < INT_MIN || num > INT_MAX || *endp != '\0')
      {
        Dbg::warn().printf("Bad parameter '%s'. Number required.\n", prefix);
        L4Re::chksys(-L4_EINVAL);
      }

    *out = num;

    return true;
  }

  bool _scan_in_progress = true;
};

struct Client_opts
{
  bool add_client(Blk_mgr *blk_mgr)
  {
    if (capname)
      {
        if (!device)
          {
            Err().printf("No device for client '%s' given. "
                         "Please specify a device.\n", capname);
            return false;
          }

        auto cap = L4Re::Env::env()->get_cap<L4::Rcv_endpoint>(capname);
        if (!cap.is_valid())
          {
            Err().printf("Client capability '%s' no found.\n", capname);
            return false;
          }

        blk_mgr->add_static_client(cap, device, -1, ds_max, readonly,
                                   [](Nvme::Nvme_base_device *) {},
                                   !trusted_dataspaces->empty(),
                                   trusted_dataspaces);
      }

    return true;
  }

  const char *capname = nullptr;
  const char *device = nullptr;
  int ds_max = 2;
  bool readonly = false;
};

static Block_device::Errand::Errand_server server;
static Blk_mgr drv(server.registry());
std::vector<cxx::unique_ptr<Nvme::Ctl>> _ctls;
unsigned static devices_in_scan = 0;

static int
parse_args(int argc, char *const *argv)
{
  int debug_level = 1;

  enum
  {
    OPT_CLIENT,
    OPT_DEVICE,
    OPT_DS_MAX,
    OPT_READONLY,
    OPT_NOSGL,
  };

  struct option const loptions[] =
  {
    { "verbose",       no_argument,       NULL, 'v' },
    { "quiet",         no_argument,       NULL, 'q' },
    { "client",        required_argument, NULL,  OPT_CLIENT },
    { "device",        required_argument, NULL,  OPT_DEVICE },
    { "ds-max",        required_argument, NULL,  OPT_DS_MAX },
    { "readonly",      no_argument,       NULL,  OPT_READONLY },
    { "nosgl",         no_argument,       NULL,  OPT_NOSGL },
    { "register-ds",   required_argument, NULL, 'd'},
  };

  Client_opts opts;
  for (;;)
    {
      int opt = getopt_long(argc, argv, "vqd:", loptions, NULL);
      if (opt == -1)
        break;

      switch (opt)
        {
        case 'v':
          debug_level <<= 1;
          ++debug_level;
          break;
        case 'q':
          debug_level = 0;
          break;
        case OPT_CLIENT:
          if (!opts.add_client(&drv))
            return 1;
          opts = Client_opts();
          opts.capname = optarg;
          break;
        case OPT_DEVICE:
          opts.device = optarg;
          break;
        case OPT_DS_MAX:
          opts.ds_max = atoi(optarg);
          break;
        case OPT_READONLY:
          opts.readonly = true;
          break;
        case OPT_NOSGL:
          Nvme::Ctl::use_sgls = false;
          break;
        case 'd':
          {
            L4::Cap<L4Re::Dataspace> ds =
              L4Re::chkcap(L4Re::Env::env()->get_cap<L4Re::Dataspace>(optarg),
                           "Find a dataspace capability.\n");
            trusted_dataspaces->push_back(ds);
            break;
          }
        default:
          Dbg::warn().printf(usage_str, argv[0]);
          return -1;
        }
    }

  if (!opts.add_client(&drv))
    return 1;

  Dbg::set_level(debug_level);
  return optind;
}

static void
device_scan_finished()
{
  if (--devices_in_scan > 0)
    return;

  drv.scan_finished();
  if (!server.registry()->register_obj(&drv, "svr").is_valid())
    Dbg::warn().printf("Capability 'svr' not found. No dynamic clients accepted.\n");
  else
    Dbg::trace().printf("Device now accepts new clients.\n");
}

static void
device_discovery(L4::Cap<L4vbus::Vbus> bus, L4::Cap<L4::Icu> icu,
                 L4Re::Util::Shared_cap<L4Re::Dma_space> const &dma)
{
  Dbg::info().printf("Starting device discovery.\n");

  L4vbus::Pci_dev child;

  l4vbus_device_t di;
  auto root = bus->root();

  // make sure that we don't finish device scan before the while loop is done
  ++devices_in_scan;

  while (root.next_device(&child, L4VBUS_MAX_DEPTH, &di) == L4_EOK)
    {
      Dbg::trace().printf("Scanning child 0x%lx.\n", child.dev_handle());
      if (Nvme::Ctl::is_nvme_ctl(child, di))
        {
          try
            {
              auto ctl = cxx::make_unique<Nvme::Ctl>(child, dma);
              ctl->register_interrupt_handler(icu, server.registry());
              _ctls.push_back(cxx::move(ctl));
            }
          catch (L4::Runtime_error const &e)
            {
              Err().printf("%s: %s\n", e.str(), e.extra_str());
              continue;
            }

          ++devices_in_scan;

          auto ct = _ctls.back().get();
          ct->identify(
            [=](cxx::unique_ptr<Nvme::Namespace> ns)
              {
                printf("Making NSID %u visible to clients\n", ns->nsid());
                drv.add_disk(cxx::make_ref_obj<Nvme::Nvme_device>(ns.get()),
                             device_scan_finished);
                ct->add_ns(cxx::move(ns));
              });
        }
    }

  // marks the end of the device detection loop
  device_scan_finished();

  Dbg::info().printf("All devices scanned.\n");
}

static void
setup_hardware()
{
  auto vbus = L4Re::chkcap(L4Re::Env::env()->get_cap<L4vbus::Vbus>("vbus"),
                           "Get 'vbus' capability.", -L4_ENOENT);

  L4vbus::Icu icudev;
  L4Re::chksys(vbus->root().device_by_hid(&icudev, "L40009"),
               "Look for ICU device.");
  auto icu = L4Re::chkcap(L4Re::Util::cap_alloc.alloc<L4::Icu>(),
                          "Allocate ICU capability.");
  L4Re::chksys(icudev.vicu(icu), "Request ICU capability.");

  Dbg::trace().printf("Creating DMA domain for VBUS.\n");

  auto dma = L4Re::chkcap(L4Re::Util::make_shared_cap<L4Re::Dma_space>(),
                          "Allocate capability for DMA space.");
  L4Re::chksys(L4Re::Env::env()->user_factory()->create(dma.get()),
               "Create DMA space.");

  L4Re::chksys(
      l4vbus_assign_dma_domain(vbus.cap(), -1U,
                               L4VBUS_DMAD_BIND | L4VBUS_DMAD_L4RE_DMA_SPACE,
                               dma.get().cap()),
      "Assignment of DMA domain.");

  device_discovery(vbus, icu, dma);
}

static int
run(int argc, char *const *argv)
{
  Dbg::set_level(3);

  int arg_idx = parse_args(argc, argv);
  if (arg_idx < 0)
    return arg_idx;

  Dbg::info().printf("NVMe driver says hello.\n");

  Block_device::Errand::set_server_iface(&server);
  setup_hardware();

  Dbg::trace().printf("Beginning server loop...\n");
  server.loop();

  return 0;
}


int
main(int argc, char *const *argv)
{
  trusted_dataspaces = std::make_shared<Ds_vector>();
  try
    {
      return run(argc, argv);
    }
  catch (L4::Runtime_error const &e)
    {
      Err().printf("%s: %s\n", e.str(), e.extra_str());
    }
  catch (L4::Base_exception const &e)
    {
      Err().printf("Error: %s\n", e.str());
    }
  catch (std::exception const &e)
    {
      Err().printf("Error: %s\n", e.what());
    }

  return -1;
}
