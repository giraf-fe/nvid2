#include <cstdint>
#include <cstring> // for memcpy

#include "../aligned_alloc/aligned_alloc.h"

// For transparency, this file was generated with Gemini 3 Pro.

// Constants for ARM MMU
#define TTB_SIZE 16384       // 4096 entries * 4 bytes = 16KB
#define TTB_ALIGNMENT 16384  // Translation Table must be 16KB aligned

class MMUHijacker {
private:
    uint32_t  _original_ttbr0_reg; // Holds the original register value (Address + Flags)
    uint32_t* _shadow_table;       // Pointer to our new table in RAM

    // --- Inline Assembly Helpers for CP15 ---

    static inline uint32_t get_ttbr0() {
        uint32_t val;
        asm volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(val));
        return val;
    }

    static inline void set_ttbr0(uint32_t val) {
        asm volatile("mcr p15, 0, %0, c2, c0, 0" :: "r"(val));
    }

    static inline void tlb_invalidate() {
        asm volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0)); // Invalidate Unified TLB
        asm volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0)); // Data Synchronization Barrier
    }

    // Clean D-Cache: Pushes data from CPU Cache -> Physical RAM
    // The MMU hardware reads RAM, so if we don't do this, the MMU sees garbage.
    static inline void clean_dcache_range(uintptr_t start, uintptr_t end) {
        uintptr_t addr;
        // Iterate by cache line (assumed 32 bytes)
        for (addr = start; addr < end; addr += 32) {
            asm volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(addr));
        }
        asm volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0)); // DSB
    }

public:
    // ---------------------------------------------------------
    // Constructor: The "Hijack"
    // ---------------------------------------------------------
    MMUHijacker() : _shadow_table(nullptr) {
        // 1. Capture the current hardware state
        _original_ttbr0_reg = get_ttbr0();

        // The register contains the Physical Address (top 18 bits) AND control flags (bottom bits)
        // We mask 0xFFFFC000 to get the base address.
        uintptr_t old_table_phys = _original_ttbr0_reg & 0xFFFFC000;

        // 2. Allocate aligned memory for the shadow copy
        _shadow_table = (uint32_t*)aligned_malloc(TTB_ALIGNMENT, TTB_SIZE);
        
        if (!_shadow_table) {
            // Allocation failed. We cannot proceed. 
            // You might hang here or return early.
            return;
        }

        // 3. Create the Shadow Copy
        // We copy the exact state of the current OS table into our new buffer.
        // This prevents the OS from crashing the moment we switch tables.
        std::memcpy(_shadow_table, (void*)old_table_phys, TTB_SIZE);

        // 4. Critical: Flush the new table to RAM
        // We just wrote to _shadow_table via the CPU, so the data is in L1 Cache.
        // The MMU reads directly from RAM. We must push the data out.
        clean_dcache_range((uintptr_t)_shadow_table, (uintptr_t)_shadow_table + TTB_SIZE);

        // 5. Activate the Shadow Table
        // We combine the address of our new table with the flags (cache attributes) 
        // from the old register to maintain consistency.
        uint32_t flags = _original_ttbr0_reg & ~0xFFFFC000;
        uint32_t new_reg_val = ((uint32_t)_shadow_table) | flags;

        // Disable IRQs here if you want extra safety, though usually fine for atomic MCR
        set_ttbr0(new_reg_val);
        tlb_invalidate();
    }

    // ---------------------------------------------------------
    // Destructor: The "Restore"
    // ---------------------------------------------------------
    ~MMUHijacker() {
        // Only attempt restore if we successfully hijacked
        if (_shadow_table) {
            // 1. Point hardware back to the original OS table
            set_ttbr0(_original_ttbr0_reg);
            
            // 2. Invalidate TLB so the CPU stops using cached entries from our shadow table
            tlb_invalidate();

            // 3. Free the memory
            aligned_free(_shadow_table);
            _shadow_table = nullptr;
        }
    }

    // ---------------------------------------------------------
    // Helper: Modify a mapping
    // ---------------------------------------------------------
    // virt: The address the code tries to access (e.g., 0xC8000000)
    // phys: The address normally accessed (e.g., 0x13000000)
    // flags: Access permissions (e.g., RW = 0xC00)
    void map(uintptr_t virt, uintptr_t phys, uint32_t flags) {
        if (!_shadow_table) return;

        // 1. Calculate Index (Top 12 bits of address)
        uint32_t idx = virt >> 20;

        // 2. Create Descriptor
        // PhysBase (Top 12 bits) | Flags | SectionType (0b10)
        uint32_t descriptor = (phys & 0xFFF00000) | flags | 0b10;

        // 3. Update the Shadow Table
        _shadow_table[idx] = descriptor;

        // 4. Critical: Flush this specific entry to RAM
        // If we don't do this, the MMU might read the old mapping from RAM
        uintptr_t entry_addr = (uintptr_t)&_shadow_table[idx];
        clean_dcache_range(entry_addr, entry_addr + 4);

        // 5. Invalidate TLB to force MMU to re-read this specific address
        tlb_invalidate();
    }
};