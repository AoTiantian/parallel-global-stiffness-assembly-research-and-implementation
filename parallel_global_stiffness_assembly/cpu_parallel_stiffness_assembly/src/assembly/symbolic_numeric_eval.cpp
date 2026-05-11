#include "assembly/symbolic_numeric_eval.h"

#include "assembly/assembler_factory.h"
#include "assembly/element_kernels.h"
#include "core/platform.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fem {
namespace {

struct DirectContribution {
    Index row = 0;
    Index col = 0;
    Real value = 0.0;
};

double ms_since(const std::chrono::steady_clock::time_point& begin,
                const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

Size count_element_entries(const Mesh& mesh) {
    Size entries = 0;
    for (const auto& elem : mesh.elements) {
        const Size edofs = static_cast<Size>(elem.node_count * constants::DOFS_PER_NODE);
        const Size elem_entries = edofs * edofs;
        if (entries > std::numeric_limits<Size>::max() - elem_entries) {
            throw std::runtime_error("Direct no-symbolic contribution count overflows Size");
        }
        entries += elem_entries;
    }
    return entries;
}

void ensure_direct_memory_allowed(const Mesh& mesh, const AssemblyOptions& options) {
    const Size entries = count_element_entries(mesh);
    if (entries > std::numeric_limits<Size>::max() / sizeof(DirectContribution)) {
        throw std::runtime_error("Direct no-symbolic contribution memory estimate overflows Size");
    }
    const Size required = entries * sizeof(DirectContribution);
    if (required > options.max_transient_bytes) {
        std::ostringstream os;
        os << "direct_no_symbolic requires about " << memory_string(required)
           << " transient memory, above limit " << memory_string(options.max_transient_bytes);
        throw std::runtime_error(os.str());
    }
}

CsrMatrix reduce_direct_contributions(std::vector<DirectContribution>& contributions, Index ndofs) {
    std::sort(contributions.begin(), contributions.end(), [](const auto& a, const auto& b) {
        if (a.row != b.row) return a.row < b.row;
        return a.col < b.col;
    });

    std::vector<Index> row_offsets(static_cast<Size>(ndofs) + 1, 0);
    std::vector<Index> col_indices;
    std::vector<Real> values;
    col_indices.reserve(contributions.size());
    values.reserve(contributions.size());

    Index current_row = 0;
    Size p = 0;
    while (p < contributions.size()) {
        const Index row = contributions[p].row;
        const Index col = contributions[p].col;
        if (row < 0 || row >= ndofs || col < 0 || col >= ndofs) {
            throw std::runtime_error("Direct no-symbolic contribution index out of range");
        }
        while (current_row <= row) {
            row_offsets[static_cast<Size>(current_row)] = static_cast<Index>(col_indices.size());
            ++current_row;
        }
        Real sum = 0.0;
        do {
            sum += contributions[p].value;
            ++p;
        } while (p < contributions.size() && contributions[p].row == row && contributions[p].col == col);
        col_indices.push_back(col);
        values.push_back(sum);
        if (col_indices.size() > static_cast<Size>(std::numeric_limits<Index>::max())) {
            throw std::runtime_error("Direct no-symbolic result exceeds 32-bit Index capacity");
        }
    }

    while (current_row <= ndofs) {
        row_offsets[static_cast<Size>(current_row)] = static_cast<Index>(col_indices.size());
        ++current_row;
    }

    CsrMatrix matrix(ndofs, ndofs, std::move(row_offsets), std::move(col_indices));
    matrix.values = std::move(values);
    return matrix;
}

void populate_common_record(SymbolicEvaluationRecord& record,
                            const SymbolicArtifacts& artifacts,
                            int assemblies_per_symbolic) {
    record.assemblies_per_symbolic = assemblies_per_symbolic;
    record.symbolic_csr_ms = artifacts.csr_ms;
    record.symbolic_plan_ms = artifacts.plan_ms;
    record.symbolic_total_ms = artifacts.total_ms();
    record.csr_bytes = artifacts.csr.bytes();
    record.plan_bytes = artifacts.plan.bytes();
}

} // namespace

SymbolicArtifacts build_symbolic_artifacts(const Mesh& mesh) {
    const auto csr0 = std::chrono::steady_clock::now();
    CsrMatrix csr = CsrMatrix::build_sparsity(mesh);
    const auto csr1 = std::chrono::steady_clock::now();
    AssemblyPlan plan = build_assembly_plan(mesh, csr);
    const auto plan1 = std::chrono::steady_clock::now();

    SymbolicArtifacts artifacts;
    artifacts.csr = std::move(csr);
    artifacts.plan = std::move(plan);
    artifacts.csr_ms = ms_since(csr0, csr1);
    artifacts.plan_ms = ms_since(csr1, plan1);
    return artifacts;
}

SymbolicSerialResult assemble_symbolic_serial_once(const Mesh& mesh,
                                                   const SymbolicArtifacts& artifacts,
                                                   const AssemblyOptions& options) {
    auto assembler = AssemblerFactory::create(AlgorithmType::CpuSerial, options);
    assembler->set_problem(mesh, artifacts.csr, artifacts.plan);
    assembler->prepare();
    assembler->assemble();

    SymbolicSerialResult result;
    result.matrix = assembler->get_result();
    result.numeric_ms = assembler->get_stats().assembly_time_ms;
    return result;
}

DirectNoSymbolicResult assemble_direct_no_symbolic_once(const Mesh& mesh,
                                                        const AssemblyOptions& options) {
    const Size entries = count_element_entries(mesh);
    ensure_direct_memory_allowed(mesh, options);
    std::vector<DirectContribution> contributions;
    contributions.reserve(entries);
    std::vector<Real> ke;

    const auto t0 = std::chrono::steady_clock::now();
    for (Size e = 0; e < mesh.num_elements(); ++e) {
        const auto dofs = element_dofs(mesh.elements[e]);
        compute_element_matrix(mesh, e, options, ke);
        const int edofs = static_cast<int>(dofs.size());
        for (int i = 0; i < edofs; ++i) {
            for (int j = 0; j < edofs; ++j) {
                contributions.push_back(DirectContribution{
                    dofs[static_cast<Size>(i)],
                    dofs[static_cast<Size>(j)],
                    ke[static_cast<Size>(i) * edofs + j]});
            }
        }
    }
    const auto t_generate = std::chrono::steady_clock::now();

    if (mesh.num_dofs() > static_cast<Size>(std::numeric_limits<Index>::max())) {
        throw std::runtime_error("Too many DOFs for 32-bit Index in direct no-symbolic assembly");
    }
    CsrMatrix matrix = reduce_direct_contributions(contributions, static_cast<Index>(mesh.num_dofs()));
    const auto t1 = std::chrono::steady_clock::now();

    DirectNoSymbolicResult result;
    result.matrix = std::move(matrix);
    result.generate_ms = ms_since(t0, t_generate);
    result.sort_reduce_ms = ms_since(t_generate, t1);
    result.total_ms = ms_since(t0, t1);
    result.transient_bytes = entries * sizeof(DirectContribution);
    return result;
}

SymbolicEvaluationRecord evaluate_symbolic_reuse_serial(const Mesh& mesh,
                                                        const AssemblyOptions& options,
                                                        int assemblies_per_symbolic) {
    if (assemblies_per_symbolic <= 0) throw std::invalid_argument("assemblies_per_symbolic must be positive");

    SymbolicEvaluationRecord record;
    record.mode = "symbolic_reuse_serial";
    SymbolicArtifacts artifacts = build_symbolic_artifacts(mesh);
    populate_common_record(record, artifacts, assemblies_per_symbolic);
    record.symbolic_builds = 1;

    auto assembler = AssemblerFactory::create(AlgorithmType::CpuSerial, options);
    assembler->set_problem(mesh, artifacts.csr, artifacts.plan);
    assembler->prepare();

    double numeric_sum = 0.0;
    for (int i = 0; i < assemblies_per_symbolic; ++i) {
        assembler->assemble();
        numeric_sum += assembler->get_stats().assembly_time_ms;
    }
    record.numeric_ms = numeric_sum / static_cast<double>(assemblies_per_symbolic);
    record.amortized_total_ms =
        (record.symbolic_total_ms + numeric_sum) / static_cast<double>(assemblies_per_symbolic);
    record.matrix = assembler->get_result();
    return record;
}

SymbolicEvaluationRecord evaluate_symbolic_rebuild_serial(const Mesh& mesh,
                                                          const AssemblyOptions& options,
                                                          int assemblies_per_symbolic) {
    if (assemblies_per_symbolic <= 0) throw std::invalid_argument("assemblies_per_symbolic must be positive");

    SymbolicEvaluationRecord record;
    record.mode = "symbolic_rebuild_serial";
    record.assemblies_per_symbolic = assemblies_per_symbolic;
    record.symbolic_builds = assemblies_per_symbolic;

    double csr_sum = 0.0;
    double plan_sum = 0.0;
    double numeric_sum = 0.0;
    Size csr_bytes = 0;
    Size plan_bytes = 0;
    for (int i = 0; i < assemblies_per_symbolic; ++i) {
        SymbolicArtifacts artifacts = build_symbolic_artifacts(mesh);
        csr_sum += artifacts.csr_ms;
        plan_sum += artifacts.plan_ms;
        csr_bytes = artifacts.csr.bytes();
        plan_bytes = artifacts.plan.bytes();
        auto once = assemble_symbolic_serial_once(mesh, artifacts, options);
        numeric_sum += once.numeric_ms;
        record.matrix = std::move(once.matrix);
    }

    record.symbolic_csr_ms = csr_sum / static_cast<double>(assemblies_per_symbolic);
    record.symbolic_plan_ms = plan_sum / static_cast<double>(assemblies_per_symbolic);
    record.symbolic_total_ms = record.symbolic_csr_ms + record.symbolic_plan_ms;
    record.numeric_ms = numeric_sum / static_cast<double>(assemblies_per_symbolic);
    record.amortized_total_ms =
        (csr_sum + plan_sum + numeric_sum) / static_cast<double>(assemblies_per_symbolic);
    record.csr_bytes = csr_bytes;
    record.plan_bytes = plan_bytes;
    return record;
}

SymbolicEvaluationRecord evaluate_direct_no_symbolic_serial(const Mesh& mesh,
                                                            const AssemblyOptions& options,
                                                            int assemblies_per_symbolic) {
    if (assemblies_per_symbolic <= 0) throw std::invalid_argument("assemblies_per_symbolic must be positive");

    SymbolicEvaluationRecord record;
    record.mode = "direct_no_symbolic_serial";
    record.assemblies_per_symbolic = assemblies_per_symbolic;
    record.symbolic_builds = 0;

    double generate_sum = 0.0;
    double sort_reduce_sum = 0.0;
    double total_sum = 0.0;
    Size transient_bytes = 0;
    for (int i = 0; i < assemblies_per_symbolic; ++i) {
        auto once = assemble_direct_no_symbolic_once(mesh, options);
        generate_sum += once.generate_ms;
        sort_reduce_sum += once.sort_reduce_ms;
        total_sum += once.total_ms;
        transient_bytes = once.transient_bytes;
        record.matrix = std::move(once.matrix);
    }

    record.direct_generate_ms = generate_sum / static_cast<double>(assemblies_per_symbolic);
    record.direct_sort_reduce_ms = sort_reduce_sum / static_cast<double>(assemblies_per_symbolic);
    record.amortized_total_ms = total_sum / static_cast<double>(assemblies_per_symbolic);
    record.direct_transient_bytes = transient_bytes;
    return record;
}

} // namespace fem
