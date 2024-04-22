# In-guest SVM Support in Qemu with Intel IOMMU

This repository and its submodule contain a patched version of Qemu with added
support for SVM (Shared Virtual Memory) based on the Intel IOMMU (VT-d).
Additionally, it includes a demo driver and a userspace program to demonstrate
the feature.

## Overview

SVM (or SVA in the Linux kernel), is a mechanism that allows a PCIe device to
share a region of memory with a userspace program, enabling efficient data
sharing and operation offloading.

More details about SVA/SVM on Linux can be found here :
https://www.kernel.org/doc/html/v5.10/x86/sva.html

## Features

- SVM support in Qemu: The patched version of Qemu supports in-guest SVM
  functionality with Intel IOMMU. The following features are implemented :
    - PASID-based device IOTLB invalidation
    - Generic ATC (Address Translation Cache) for PCIe devices
    - ATS
    - PRI
    - PCI-level API for ATS and PRI
    - Demo device that supports basic SVM operations
- Demo driver: A sample device driver is provided to operate the demonstration device
- Userspace program: A userspace program is included to showcase the utilization of in-guest SVM

## How to run

- Compile and install the patched version of Qemu (submodule _qemu_) as you
  would with an official upstream release. Please refer to the official Qemu
  instructions for this step.
- Start a Linux VM with the following configuration
    - Kernel command line parameter : `intel_iommu=on,sm_on`
    - Intel IOMMU attached with scalable mode, flts, 48 bits addresses,
      device-iotlb, pasid and DMA translation (refer to the virsh XML
      configuration example in the **VM configuration** section below)
    - Demo SVM device attached
- Clone this repo into your VM
- run `make run`

_Note : if the huge page allocation keeps failing, you should consider adding
`hugepagesz=1048576k hugepages=1` to your kernel command line to reserve a 1G
huge page at boot time._

## VM configuration

To run our tests, we used a Debian 12 VM with a kernel updated to 6.7.4.
Here is the virsh configuration

```xml
<qemu:commandline>
    <qemu:arg value='-device'/>
    <qemu:arg value='svm'/>
    <qemu:arg value='-device'/>
    <qemu:arg value='intel-iommu,aw-bits=48,x-scalable-mode=on,x-flts=on,svm=true,device-iotlb=true,x-pasid-mode=true,dma-translation=true'/>
</qemu:commandline>
```
