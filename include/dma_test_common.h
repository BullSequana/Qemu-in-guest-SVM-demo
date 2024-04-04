#ifndef __DMA_TEST_COMMON__
#define __DMA_TEST_COMMON__

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdlib.h>
#endif

struct kmem_alloc_request {
    void* src;
    size_t size;
    void* res;
};

/* Non overlapping bits */
enum opt {
    OPT_NONE = 0,
    OPT_PRIV = 1,
};

#define IOCTL_SET_ADDR      _IOW('I', 0x40, uint64_t*)
#define IOCTL_SET_CRC       _IOW('I', 0x41, uint64_t*)
#define IOCTL_SET_SIZE      _IOW('I', 0x42, uint64_t*)
#define IOCTL_START_WRITE   _IO('I', 0x43)
#define IOCTL_START_READ    _IO('I', 0x44)
#define IOCTL_ALLOC_KMEM    _IO('I', 0x45)
#define IOCTL_SET_OPT      _IOW('I', 0x46, uint64_t*)

#endif /* __DMA_TEST_COMMON__ */