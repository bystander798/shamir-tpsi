# Shamir TPSI

本仓库是一个阈值私有集合交集计算（Threshold Private Set Intersection，简称 TPSI）原型，
包含 C++ 原生实现、精简版 HTTP API 演示服务，以及基于 Vue 3 + Vite 的前端。
当前聚焦端到端流程：数据集入库、阈值协商、发送方确认、管理员审批与执行指标采集。

## 目录结构
- `include/`, `src/` – 原生 TPSI 实现（C++20，静态库 `tpsi`）。
- `apps/` – 双进程演示 CLI：`tpsi_receiver`、`tpsi_sender`。
- `docs/` – 补充文档（如 REST API 协议）。
- `benchmarks/` – 可复现实验的协议基准工具。
- `tests/` – 单元/集成测试（CTEST）。
- `frontend/` – Vue 3 控制台，用于驱动演示服务。
- `third_party/` – 演示服务使用的单头文件依赖（`httplib.h`、`nlohmann_json.hpp`）。

## 环境依赖
- CMake ≥ 3.16，C++20 编译器（gcc ≥ 11 或 clang ≥ 14）。
- OpenSSL（Crypto）：安装 `libssl-dev` 或提供 `openssl` 头文件。
- NTL（Number Theory Library）：`libntl-dev` / `ntl`。
- 可选 FLINT（多项式加速）：`libflint-dev`。
- Node.js ≥ 18 与 npm（用于前端）。
- 安装命令如下：
```bash
sudo apt update
sudo apt install cmake ninja-build g++ libssl-dev libntl-dev libflint-dev
```
> Windows 不是主要目标（测试/基准依赖 fork/exec）。建议在 Linux 或 macOS 上开发与跑基准。

## 原生构建（C++）
基础构建（静态库 + CLI + 测试 + 基准 + 演示服务）：
```bash
rm -rf build
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release -DSHAIR_TPSI_USE_FLINT=ON
cmake --build build -j
```

常用 CMake 选项：
- `-DSHAIR_TPSI_USE_FLINT=ON|OFF` – 若系统存在 FLINT，则启用 FLINT 多项式后端（自动探测；缺失则回退到 NTL）。


构建产物（位于 `build/`）：
- `libtpsi.a` – 静态库。
- `tpsi_receiver`、`tpsi_sender` – 双进程演示。
- `benchmarks/tpsi_bench` – 基准程序。
- `tpsi_server` – HTTP API 演示服务。
- `tests/tpsi_tests` – 单元/集成测试。

## HTTP API 演示服务
如需单独构建到独立目录：
```bash
rm -rf build-server
cmake -S . -B build-server -DSHAIR_TPSI_BUILD_SERVER=ON
cmake --build build-server -j
```

运行：
```bash
TPSI_SERVER_HOST=0.0.0.0 TPSI_SERVER_PORT=8080 ./build-server/tpsi_server
```

- 默认 Host：`0.0.0.0`；默认端口：`8080`。
- 数据尝试持久化到 `data/state.json`（仅用于演示）。
- 内置演示账号：`receiver/password123`、`sender/password123`、`admin/password123`。
- API 参考：`docs/api_server.md`。

服务器支持的数据集上传格式：
- JSON 数组：`[1, 2, 3]`
- 含 `elements` 字段的 JSON 对象：`{ "elements": [1,2,3], "modulus": 104857601 }`
- 纯文本/CSV/TSV：自动提取其中的数字，并按模数规则归一化。

模数处理说明：
- 若 `modulus > 0`，按该模数取模（允许负数，会做模归一）。
- 若 `modulus == 0`，要求值非负，按原值保存。

## 前端（Vue 3 + Vite）
```bash
cd frontend
npm install           # 首次安装依赖
npm run dev           # 本地地址 http://localhost:5173 （代理到后端 8080）
```

`vite.config.ts` 中将 `/api` 代理到 `http://localhost:8080`。开发前端时请保持后端服务运行。

快速流程：
- 在登录页选择 `receiver` / `sender` / `admin` 角色。
- `receiver` 上传数据集，发起求交申请。
- `sender` 在「协同请求」页确认或拒绝。
- `admin` 在审批中心同意执行，任务页同步指标与日志。

前端构建与测试：
- `npm run build` – 类型检查 + 生产构建。
- `npm run test` – 使用 Vitest（当前未添加专门用例，可按需扩展）。

## 双进程 CLI 演示
两个 CLI 通过本地 TCP 通道通信，并读取同一份会话配置文件。

示例配置（`session.cfg`）：
```
field_modulus 1152921504204193793
ring_degree 1048576
ring_modulus 1152921504204193793
pcg_sparse_weight 64
pcg_verify 1
threshold 500
pseudo_count 0
receiver_count 4
receiver 1
receiver 2
receiver 3
receiver 4
sender_count 4
sender 3
sender 4
sender 5
sender 6
```

先启动接收方，再启动发送方：
```bash
./build/tpsi_receiver --config session.cfg --output report.txt --port 49000
./build/tpsi_sender   --config session.cfg --port 49000
```

接收方输出文件（`report.txt`）包含：阈值是否达成、交集大小、收/发字节数、
各阶段耗时（掷币/离线/在线）、RSS 统计，以及总通信字节数。

## 基准测试（Benchmarks）
平衡模型下运行：
```bash
./build/benchmarks/tpsi_bench <set_size_exponent> <threshold_ratio>
# 例如：集合规模 = 2^16，阈值 = 0.9 * 集合规模
./build/benchmarks/tpsi_bench 16 0.9
```
非平衡模型下运行：
```bash
./build/benchmarks/tpsi_bench <receiver_set_size_exponent> <sender_set_size_exponent>
# 例如：接收方集合规模 = 100，发送方集合规模 = 1000
./build/benchmarks/tpsi_bench 100 1000
```
该基准程序会在内部启动 `tpsi_receiver` 与 `tpsi_sender`，
按阶段采集耗时与通信量，并输出简明汇总。

## 单元测试

```bash
./build/tests/tpsi_tests <index>
```
index取值如下：
- index=1：验证预计算功能。
- index=2：验证rOLE功能。
- index=3：验证RSS重构功能。
- index=4：验证整体协议功能。
## Front-End Highlights
- Dataset 管理 – CSV/JSON 上传、客户端解析预览（元素数量/去重/样本片段），同步保存到后端。
- 求交申请（接收方） – 选择自有/对端数据集、查看摘要、配置阈值与模数，提交后可在审批中心与任务页追踪状态。
- 协同确认（发送方） – 审阅接收方请求、查看双方数据集详情、确认或拒绝。
- 审批中心（管理员） – 审核数据摘要、打开详情，通过后自动触发 TPSI 会话并回填指标。
- 任务总览 – 展示阈值是否达成、阶段耗时、通信量、执行日志；支持重跑。
- TPSI 后端联动 – HTTP API 直接调度 native TPSI 协议（进程内收发通道），输出真实阈值判定、通信统计与交集明细。
- 个人中心 – 维护基础信息、通知偏好与安全设置（示例数据）。

## Troubleshooting
- CMake 找不到 NTL/FLINT：请安装 `libntl-dev`、`libflint-dev`（或通过 Homebrew 安装 `ntl`/`flint`）。未找到 FLINT 时会自动回退到 NTL 实现。
- OpenSSL 头文件缺失：安装 `libssl-dev` 或对应平台的开发包。
- Windows 平台：测试与基准依赖 `fork/exec/waitpid`，建议在 WSL2 或 Linux/macOS 环境运行。

