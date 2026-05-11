#pragma once

#include "assembly/assembly_options.h"
#include "assembly/assembly_plan.h"
#include "core/csr_matrix.h"
#include "core/mesh.h"
#include "core/types.h"

#include <string>

namespace fem {

struct SymbolicArtifacts {
    CsrMatrix csr;
    AssemblyPlan plan;
    double csr_ms = 0.0;
    double plan_ms = 0.0;

    [[nodiscard]] double total_ms() const noexcept { return csr_ms + plan_ms; }
};

struct SymbolicSerialResult {
    CsrMatrix matrix;
    double numeric_ms = 0.0;
};

struct DirectNoSymbolicResult {
    CsrMatrix matrix;
    double generate_ms = 0.0;
    double sort_reduce_ms = 0.0;
    double total_ms = 0.0;
    Size transient_bytes = 0;
};

struct SymbolicEvaluationRecord {
    std::string mode;
    int assemblies_per_symbolic = 1;
    int symbolic_builds = 0;
    double symbolic_csr_ms = 0.0;
    double symbolic_plan_ms = 0.0;
    double symbolic_total_ms = 0.0;
    double numeric_ms = 0.0;
    double direct_generate_ms = 0.0;
    double direct_sort_reduce_ms = 0.0;
    double amortized_total_ms = 0.0;
    Size csr_bytes = 0;
    Size plan_bytes = 0;
    Size direct_transient_bytes = 0;
    MatrixError error;
    CsrMatrix matrix;
};

SymbolicArtifacts build_symbolic_artifacts(const Mesh& mesh);

SymbolicSerialResult assemble_symbolic_serial_once(const Mesh& mesh,
                                                   const SymbolicArtifacts& artifacts,
                                                   const AssemblyOptions& options);

DirectNoSymbolicResult assemble_direct_no_symbolic_once(const Mesh& mesh,
                                                        const AssemblyOptions& options);

SymbolicEvaluationRecord evaluate_symbolic_reuse_serial(const Mesh& mesh,
                                                        const AssemblyOptions& options,
                                                        int assemblies_per_symbolic);

SymbolicEvaluationRecord evaluate_symbolic_rebuild_serial(const Mesh& mesh,
                                                          const AssemblyOptions& options,
                                                          int assemblies_per_symbolic);

SymbolicEvaluationRecord evaluate_direct_no_symbolic_serial(const Mesh& mesh,
                                                            const AssemblyOptions& options,
                                                            int assemblies_per_symbolic);

} // namespace fem
