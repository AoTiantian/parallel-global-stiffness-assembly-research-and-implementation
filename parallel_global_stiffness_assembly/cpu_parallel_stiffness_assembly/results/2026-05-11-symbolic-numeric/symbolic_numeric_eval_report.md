# 符号/数值组装效率评估报告

## 固定术语

- 符号组装：拓扑、DOF、CSR 稀疏结构和 scatter 写入位置预计算，不计算 `Ke`。
- 数值组装/物理组装：计算 `physics_tet4` 单元刚度 `Ke`，并填充全局矩阵。
- 无符号直接组装：不复用 CSR pattern 或 scatter plan，每次直接生成 `(row,col,value)` 贡献并排序归并。

## Mentor 示例 vs 当前 C++ 实现

| Mentor MATLAB 示例 | 当前 C++ 主线 | 采用策略 |
| --- | --- | --- |
| `build_symbolic_pattern` 生成稀疏模式 | `CsrMatrix::build_sparsity` 生成 CSR pattern | 保留 C++ 实现，文档显式命名为符号组装 |
| `cellDofsCache` 缓存单元 DOF | `AssemblyPlan::dofs` 缓存单元 DOF | 直接对应 |
| `allocate_global_matrix` 预分配稀疏矩阵 | `CsrMatrix` 结构复用并清零 values | 直接对应 |
| `assemble_numeric` 计算 `Ke` 并块插入 | `cpu_serial` 等 assembler 计算 `Ke` 并按 scatter 写入 | C++ 额外缓存 CSR scatter 位置，数值阶段更工程化 |
| PETSc-style `section/closure` 教学结构 | 当前节点 DOF 直接映射 | 首阶段不重构，作为未来高阶 DOF 扩展参考 |

## 实验设置

- case: `3d-WindTurbineHub`
- mesh: nodes=228384, elements=1113684, dofs=685152
- kernel: `physics_tet4`
- platform: `macOS;arm64;Clang 21.0.0 (clang-2100.0.123.102);OpenMP 202011`
- CPU: `Apple M4 Max`, physical_cores=14, logical_cores=14

## 结果

| 模式 | 组装次数 | 符号构建次数 | 符号总耗时 ms | 数值 ms | 直接生成 ms | 直接排序归并 ms | 摊销总耗时 ms | 相对无符号收益 | rel_l2 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `symbolic_reuse_serial` | 1 | 1 | 3200.968 | 617.818 | 0.000 | 0.000 | 3818.786 | 1.644 | 0.000e+00 |
| `symbolic_rebuild_serial` | 1 | 1 | 3219.246 | 594.023 | 0.000 | 0.000 | 3813.269 | 1.646 | 0.000e+00 |
| `direct_no_symbolic_serial` | 1 | 0 | 0.000 | 0.000 | 770.372 | 5505.827 | 6276.199 | 1.000 | 1.615e-16 |
| `symbolic_reuse_serial` | 3 | 1 | 3195.067 | 568.143 | 0.000 | 0.000 | 1633.165 | 3.565 | 0.000e+00 |
| `symbolic_rebuild_serial` | 3 | 3 | 3147.042 | 588.176 | 0.000 | 0.000 | 3735.218 | 1.559 | 0.000e+00 |
| `direct_no_symbolic_serial` | 3 | 0 | 0.000 | 0.000 | 593.735 | 5228.634 | 5822.369 | 1.000 | 1.615e-16 |
| `symbolic_reuse_serial` | 10 | 1 | 3256.337 | 575.691 | 0.000 | 0.000 | 901.325 | 6.320 | 0.000e+00 |
| `symbolic_rebuild_serial` | 10 | 10 | 3145.832 | 598.086 | 0.000 | 0.000 | 3743.918 | 1.522 | 0.000e+00 |
| `direct_no_symbolic_serial` | 10 | 0 | 0.000 | 0.000 | 549.004 | 5147.784 | 5696.788 | 1.000 | 1.615e-16 |
| `symbolic_reuse_serial` | 30 | 1 | 3252.907 | 578.315 | 0.000 | 0.000 | 686.745 | 8.086 | 0.000e+00 |
| `symbolic_rebuild_serial` | 30 | 30 | 2881.703 | 512.877 | 0.000 | 0.000 | 3394.580 | 1.636 | 0.000e+00 |
| `direct_no_symbolic_serial` | 30 | 0 | 0.000 | 0.000 | 516.919 | 5035.935 | 5552.854 | 1.000 | 1.615e-16 |
