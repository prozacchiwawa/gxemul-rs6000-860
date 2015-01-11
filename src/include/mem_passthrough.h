#ifndef MEM_PASSTHROUGH_H_
#define MEM_PASSTHROUGH_H_

#include <stddef.h>
#include <stdint.h>

class IMemoryHolePassthrough {
public:
    virtual ~IMemoryHolePassthrough() { }
    virtual void write(uint64_t paddr, const unsigned char *data, size_t len) = 0;
    virtual void read(uint64_t paddr, unsigned char *data, size_t len) = 0;
};

IMemoryHolePassthrough *create_memory_hole_handler(const char *config);

#endif//MEM_PASSTHROUGH_H_
