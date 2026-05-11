#include "assembly/symbolic_numeric_eval.h"
#include "core/csr_matrix.h"
#include "core/mesh.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace fem;

namespace {

void require_close(const char* label, const CsrMatrix& reference, const CsrMatrix& candidate) {
    const auto err = compare_values(reference, candidate);
    std::cout << label << " rel_l2=" << err.relative_l2
              << " max_abs=" << err.max_abs << "\n";
    if (!err.same_structure || err.relative_l2 > 1.0e-8 || !std::isfinite(err.relative_l2)) {
        throw std::runtime_error(std::string(label) + " does not match symbolic reference");
    }
}

void require_positive(const char* label, double value) {
    if (!(value >= 0.0) || !std::isfinite(value)) {
        throw std::runtime_error(std::string(label) + " is not a finite non-negative duration");
    }
}

} // namespace

int main() {
    try {
        Mesh mesh = Mesh::make_cube_tet4(2, 2, 2);
        AssemblyOptions options;
        options.kernel = KernelType::PhysicsTet4;

        auto artifacts = build_symbolic_artifacts(mesh);
        auto symbolic_once = assemble_symbolic_serial_once(mesh, artifacts, options);
        auto direct_once = assemble_direct_no_symbolic_once(mesh, options);
        require_close("direct_no_symbolic", symbolic_once.matrix, direct_once.matrix);

        const int assemblies = 3;
        auto reuse = evaluate_symbolic_reuse_serial(mesh, options, assemblies);
        auto rebuild = evaluate_symbolic_rebuild_serial(mesh, options, assemblies);
        auto direct = evaluate_direct_no_symbolic_serial(mesh, options, assemblies);

        require_close("symbolic_reuse_serial", symbolic_once.matrix, reuse.matrix);
        require_close("symbolic_rebuild_serial", symbolic_once.matrix, rebuild.matrix);
        require_close("direct_no_symbolic_serial", symbolic_once.matrix, direct.matrix);

        if (reuse.mode != "symbolic_reuse_serial" ||
            rebuild.mode != "symbolic_rebuild_serial" ||
            direct.mode != "direct_no_symbolic_serial") {
            throw std::runtime_error("Unexpected symbolic evaluation mode labels");
        }
        if (reuse.assemblies_per_symbolic != assemblies ||
            rebuild.assemblies_per_symbolic != assemblies ||
            direct.assemblies_per_symbolic != assemblies) {
            throw std::runtime_error("Unexpected assemblies_per_symbolic values");
        }

        require_positive("reuse symbolic_total_ms", reuse.symbolic_total_ms);
        require_positive("reuse numeric_ms", reuse.numeric_ms);
        require_positive("reuse amortized_total_ms", reuse.amortized_total_ms);
        require_positive("direct generate_ms", direct.direct_generate_ms);
        require_positive("direct sort_reduce_ms", direct.direct_sort_reduce_ms);
        require_positive("direct amortized_total_ms", direct.amortized_total_ms);

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "verify_symbolic_numeric_eval failed: " << ex.what() << "\n";
        return 1;
    }
}
