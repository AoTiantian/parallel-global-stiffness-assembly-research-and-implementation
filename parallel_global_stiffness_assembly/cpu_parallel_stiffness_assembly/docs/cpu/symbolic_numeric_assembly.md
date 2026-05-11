# 符号组装与数值组装说明

## 结论

当前 C++ 主线已经实现了 mentor 所说的“先符号组装，再数值/物理组装”的核心技术路线，只是此前没有在文档、CLI 输出和报告中显式使用这套术语。

在当前项目中：

- 符号组装：根据网格拓扑和 DOF 映射建立 CSR 稀疏结构，并预计算每个单元写入全局矩阵的 scatter 位置。
- 数值组装/物理组装：计算 `physics_tet4` 单元刚度矩阵 `Ke`，复用符号阶段结果填充全局刚度矩阵。
- 无符号直接组装：不复用 CSR pattern 或 scatter plan，每次从单元 DOF 直接生成 `(row, col, value)` 贡献，再排序归并为全局矩阵。

## Mentor 示例与当前 C++ 的对应关系

| Mentor MATLAB 示例 | 当前 C++ 主线 | 关系 |
| --- | --- | --- |
| `build_mesh_topology` | `Mesh` 中的节点/单元连接关系 | C++ 已有 3D Tet4/Hex8 网格与 `.inp` 解析，不重写为 MATLAB 拓扑结构体 |
| `build_section` | `element_dofs()` 的节点 DOF 规则 | 当前主线每节点 3 DOF，未显式抽象 `section` |
| `get_cell_closure` | 单元连接节点集合 | 当前主线只需要节点 DOF，暂不显式枚举边/单元 closure |
| `build_cell_dofs` | `element_dofs()` 与 `AssemblyPlan::dofs` | 等价于生成并缓存每个单元的全局 DOF |
| `build_symbolic_pattern` | `CsrMatrix::build_sparsity()` | 等价于用单元 DOF 外积建立全局稀疏结构 |
| `allocate_global_matrix` | `CsrMatrix` 结构复用并清零 `values` | 等价于预分配结构、数值阶段只填值 |
| `assemble_numeric` | `cpu_serial` / `cpu_atomic` / `cpu_private_csr` 等 `assemble()` | 等价于计算 `Ke` 并写入全局矩阵 |
| `cellDofsCache` | `AssemblyPlan::dofs` | 直接对应 |
| 无直接对应项 | `AssemblyPlan::scatter` | C++ 比示例多缓存了 `Ke(i,j)` 到 CSR value 位置的映射，数值阶段不再查找 |

## 异同

共同点：

- 都是两阶段组装。
- 符号阶段只处理拓扑、DOF 和稀疏模式，不计算 `Ke`。
- 数值阶段复用符号阶段缓存，不重复做拓扑/DOF 结构分析。
- 多次组装时，符号阶段结果可以复用，从而摊销预处理成本。

差异：

- MATLAB 示例是 2D Tri3/CST 教学原型；当前 C++ 主线是 3D Tet4/Hex8 工程 benchmark。
- MATLAB 使用 1-based sparse；C++ 使用 0-based CSR。
- MATLAB 显式展示 PETSc-style `section/closure`；C++ 当前采用每节点固定 3 DOF 的直接映射。
- C++ 的 `AssemblyPlan::scatter` 更接近高性能实现，提前保存全局 CSR 写入位置。

## 是否参考 mentor 示例

参考，但不直接移植。

首阶段采用方式：

- 吸收 mentor 示例中的术语：符号组装、数值组装、`cellDofsCache`、预分配。
- 吸收 mentor 示例中的讲解结构：先解释拓扑/DOF/稀疏模式，再解释 `Ke` 计算和写入。
- 保留当前 C++ 的 CSR/scatter plan 设计，不引入 MATLAB 式 `section/closure` 重构。

不直接移植的原因：

- 示例是 2D、MATLAB、Tri3/CST，与当前 3D `physics_tet4` 主线不一致。
- 当前项目已经有可复用的 C++ CSR 和 scatter plan，实现层面更适合性能评估。
- 首阶段目标是回答效率问题，不是重构 DOF 抽象。

后续如果研究高阶单元、边 DOF、单元 DOF 或更接近 PETSc DMPlex 的通用拓扑抽象，再考虑引入显式 `Section` / `Closure` 层。

## 新增评估模式

新增独立程序 `symbolic_numeric_eval`，固定用于回答“符号组装是否带来效率收益”。

三种模式：

- `symbolic_reuse_serial`：构建一次 CSR/scatter plan，多次复用数值组装。
- `symbolic_rebuild_serial`：每次都重建 CSR/scatter plan，再数值组装。
- `direct_no_symbolic_serial`：不复用 CSR/scatter plan，每次生成 `(row,col,value)` 贡献并排序归并。

关键输出字段：

- `symbolic_csr_ms`
- `symbolic_plan_ms`
- `symbolic_total_ms`
- `numeric_ms`
- `direct_generate_ms`
- `direct_sort_reduce_ms`
- `amortized_total_ms`

## 推荐运行方式

小网格 smoke：

```bash
./build/cpu-release/bin/symbolic_numeric_eval \
  --mesh cube --element tet4 --nx 3 --ny 3 --nz 3 \
  --kernel physics_tet4 \
  --assemblies-list 1,3 \
  --csv /tmp/symbolic_numeric_smoke.csv
```

真实工程网格评估：

```bash
python3 scripts/run_symbolic_numeric_eval.py
```

默认真实评估固定为：

- `3d-WindTurbineHub.inp`
- `physics_tet4`
- `assemblies_per_symbolic = 1,3,10,30`

Windows Intel 平台使用同一脚本和同一 CSV/JSON/Markdown schema 复跑。跨平台报告必须区分平台差异与算法差异，不能把 Apple Silicon 和 Intel x86_64 的性能差异直接写成算法优劣。
