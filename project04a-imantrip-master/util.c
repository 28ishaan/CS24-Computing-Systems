#include "util.h"

extern "C" {
    void memory_foreach(x86_64_pagetable *table, uintptr_t upper_bound, iteration_func_t iter) {
        for (vmiter it(table, 0); it.va() < upper_bound; it += PAGESIZE) {
            bool ret = iter(table, it.va());
            if (!ret) {
                break;
            }
        }
    }

    int memory_map(x86_64_pagetable *table, uintptr_t virtual_address, uintptr_t physical_address, int permissions) {
        return vmiter(table, virtual_address).try_map(physical_address, permissions);
    }

    void * memory_virtual_to_physical(x86_64_pagetable *table, uintptr_t virtual_address) {
        return (void *)vmiter(table, virtual_address).pa();
    }

    uintptr_t memory_permissions(x86_64_pagetable *table, uintptr_t virtual_address) {
        return vmiter(table, virtual_address).perm();
    }


    bool map_to_nobody(x86_64_pagetable *table, uintptr_t va) {
       memory_map(table, va, va, 0); 
       return true;
    }

    bool map_to_kernel_space(x86_64_pagetable *table, uintptr_t va) {
       memory_map(table, va, va, PTE_P | PTE_W); 
       return true;
    }

    bool map_to_user_space(x86_64_pagetable *table, uintptr_t va) {
       memory_map(table, va, va, PTE_P | PTE_W | PTE_U);
       return true;
    }
}
