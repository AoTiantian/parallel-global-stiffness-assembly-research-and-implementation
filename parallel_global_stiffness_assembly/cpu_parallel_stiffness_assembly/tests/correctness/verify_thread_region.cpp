#include "core/platform.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require_region(const char* label,
                    int threads,
                    const fem::CpuTopologyInfo& cpu,
                    const std::string& expected) {
    const std::string actual = fem::classify_thread_region(threads, cpu);
    std::cout << label << " threads=" << threads << " region=" << actual << "\n";
    if (actual != expected) {
        throw std::runtime_error(std::string(label) + " expected " + expected + ", got " + actual);
    }
}

} // namespace

int main() {
    try {
        fem::CpuTopologyInfo smt_cpu;
        smt_cpu.model = "synthetic SMT CPU";
        smt_cpu.physical_cores = 8;
        smt_cpu.logical_cores = 16;
        require_region("physical lower bound", 1, smt_cpu, "physical_core_region");
        require_region("physical upper bound", 8, smt_cpu, "physical_core_region");
        require_region("logical lower bound", 9, smt_cpu, "logical_core_region");
        require_region("logical upper bound", 16, smt_cpu, "logical_core_region");
        require_region("oversubscribed", 17, smt_cpu, "oversubscription_region");

        fem::CpuTopologyInfo no_smt_cpu;
        no_smt_cpu.model = "synthetic no SMT CPU";
        no_smt_cpu.physical_cores = 14;
        no_smt_cpu.logical_cores = 14;
        require_region("no smt physical", 14, no_smt_cpu, "physical_core_region");
        require_region("no smt oversubscribed", 15, no_smt_cpu, "oversubscription_region");

        fem::CpuTopologyInfo unknown_cpu;
        unknown_cpu.model = "unknown topology CPU";
        unknown_cpu.physical_cores = 0;
        unknown_cpu.logical_cores = 4;
        require_region("unknown physical fallback", 4, unknown_cpu, "physical_core_region");
        require_region("unknown oversubscribed", 5, unknown_cpu, "oversubscription_region");
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "verify_thread_region failed: " << ex.what() << "\n";
        return 1;
    }
}
