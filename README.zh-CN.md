# Phy-nspire

[English](README.md)

Phy-nspire 是运行在 TI-Nspire CX II CAS 计算器上的**原生符号物理笔记本**:
一个用 Ndless SDK 构建的 ARM C/C++ 程序,把精确符号计算、张量微分几何、
广义相对论曲率管线、Yang–Mills 规范场和有界的 QFT 前端,装进一台口袋里的
计算器,并以二维数学排版 + Markdown 笔记的形式呈现。

- **目标设备**:TI-Nspire CX II CAS,OS 6.4.0.74,Ndless r2022
- **程序体积**:1,154,912 字节(约 1.10 MiB，6 MiB 上限的 18.4%)
- **实现语言**:C11 内核 + C++17 公式排版桥;同一份可移植内核同时构建
  主机测试二进制与设备 ARM 程序
- **许可证**:GPL-3.0

## 亮点

- **精确到像素的有理算术。** 小整数走 int64 快路径，溢出后自动提升到
  受步数、limb 与内存预算约束的原生任意精度整数/有理数；没有浮点、
  没有容差、没有采样。`2^200` 会精确折叠，`RicciScalar == 0` 是被
  *证明*的，而不是“数值上接近 0”。
- **类型化的物理对象。** 流形、微分形式、张量、李代数、规范联络、曲率丛
  是评估器里的一等值,不是字符串约定。`ExteriorD[ExteriorD[a]]` 返回 0,
  `Wedge[a,a]` 判零,`Einstein[c]` 的分量逐个可证。
- **判零决策程序。** 四步归约(化简 → 三角基 → 有理正规形 → cos 幂消去)
  加上 LCD 合并与已知因子消去,把"这个曲率分量是否为零"变成精确判定。
  Schwarzschild、Reissner–Nordström 的全部标量不变量在设备上闭合出精确
  有理式。
- **纸张一样的笔记本。** Markdown 正文按词与行内公式流式排版、逐行动态
  行高;二维公式渲染(分式、上下标、根式、希腊字母)来自内置的 nMarkdown
  排版器;过宽的结果先降字号再水平平移。
- **有界且可恢复。** 每次求值都受步数/节点/字节预算约束,可以中断;
  一个把内存填满的重表达式会被自动隔离,笔记本上下文原地重建,其余
  cell 继续可用——不会因为一次爆炸而丢掉整个会话。

## 屏幕上能做什么

打开 `examples/phy-nspire-cas-tour.tns`(123 个源 cell、112 个已验证输入,
随版本持续再生成)可以完整走一遍当前能力:

| 领域 | 可执行的输入 |
| --- | --- |
| 标量 CAS | 精确表达式、赋值、`Simplify`、`FullSimplify`、`Expand`、`Together`、`Cancel`、`Factor`、`Apart`、`Series`、`Normal`、有限/单侧/无穷远 `Limit`、`Numerator`、`Denominator`、`D`、`Integrate` |
| 张量/流形 | `Manifold`、`ComponentTensor`(0–4 阶、全 Up/Down 价态)、`Metric`、`VectorField`、`Component`、`Rank`、`Dimension` |
| 外微分几何 | `DifferentialForm`、`Wedge`、`ExteriorD`、`InteriorProduct`、`LieDerivative`(Cartan 公式)、`HodgeStar`、`Volume`、`Degree` |
| 李代数 / Yang–Mills | `LieGroup[SU2]`、`LieAlgebra`、`Generator`、`LieBracket`、`StructureConstant`、`Killing`、`GaugeConnection`、`FieldStrength`、`CovariantD`、`GaugeVariation`、`Bianchi`、`YangMillsLagrangian` |
| 广义相对论 | `Curvature`、`InverseMetric`、`Christoffel`、`RiemannMixed`、`Riemann`、`Ricci`、`RicciScalar`、`Einstein`、`Kretschmann`、`Weyl`、`WeylSquared`、`GeodesicAcceleration`、`CovariantDerivative` |
| 标量 QFT / Dirac | `Phi4Lagrangian`、`Phi4EOM`、`Phi4Diagrams`、`Phi4Renormalization`、`Phi4Counterterm`、`DiracTrace`(至 8 个 γ)、`MandelstamReduce` |
| SU(N) 颜色代数 | `SUNDelta`、`SUNF`、`SUND`、`SUNT`、`SUNTrace`、`SUNCommutator`、`SUNCF`、`SUNCA`、`SUNExpandCasimirs` 等 |
| 决策与资源 | `ZeroQ`、`EquivalentQ`、`MemoryStatus`、`Clear`、`ClearAll` |

张量结果的显示是分层的:0–2 阶展开为标量/向量/矩阵;3–4 阶(Christoffel、
Riemann)列出**非零分量方程**,指标用坐标名标注,如
`Γ(θ,φ,φ) = −cos(θ)sin(θ)`;流形、李群、曲率丛这类无展开的对象显示
描述行。

### 按键

| 键 | 作用 |
| --- | --- |
| 触摸板 | 指针移动;点卡片选中/编辑,点 `RUN` 角标运行该输入 |
| `ENTER` | 运行选中的输入 cell |
| `ESC` | 退出编辑;再按经保存路径退出文档 |
| `MENU` | 文件菜单(Open / Save / 新建)与编辑期模板插入 |
| `TAB` | Markdown 标题 ↔ 正文切换 |
| 方向键 | 移动选择;编辑 Markdown 正文时按行移动;选中过宽结果时左右平移 |
| 底栏 `+MD` / `+Math` | 插入 Markdown / 数学输入 cell |

## 安装与构建

### 直接安装(设备)

1. 计算器安装 [Ndless](https://ndless.me/)(r2022,OS 6.4.0.74)。
2. 把 `dist/phy-nspire.tns` 和 `examples/phy-nspire-cas-tour.tns` 用任意
   传输工具(TI-Nspire Computer Link、本仓库 `tools/nlinkctl` 等)拷入
   计算器,打开 `phy-nspire.tns` 即可。

### 从源码构建

主机端(测试与工具,Linux/WSL/Windows 均可):

```sh
cmake -S . -B build-review -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-review -j
ctest --test-dir build-review          # 当前 Windows 34 个套件,213,505 条断言
```

设备端(需要 Ndless SDK 与 arm-none-eabi 工具链,当前在 WSL 下验证):

```sh
export PATH="$HOME/.phy-nspire/Ndless/ndless-sdk/bin:$HOME/.phy-nspire/arm-gnu-toolchain-14.3.rel1-x86_64-arm-none-eabi/bin:$PATH"
make -j 8                              # 产物:dist/phy-nspire.tns
```

另有 ASan/UBSan/Leak 全开的 `build-asan` 配置。当前合入门槛是
Windows 严格配置 34/34、WSL ASan/UBSan/Leak 配置 36/36。

## 架构

```
include/phy/          公共 API(每层一个头)
src/core              状态码、平台无关基础
src/ir                驻留式(interning)表达式 IR:结构相等 == 引用相等
src/cas               标量 CAS:正规形、判零、微分、有界积分、有理归约
src/tensor            稠密分量张量(0–4 阶)、对称性、缩并、升降指标
src/geom              流形、图册、微分形式、外微分、Hodge 对偶
src/gr                Levi-Civita 曲率管线与标量不变量
src/lie, src/qft      李代数/规范场;φ⁴、Dirac 迹、Mandelstam、SU(N)
src/eval              类型化求值器:把读者语法分派到各原生后端
src/notebook          cell 模型、解析器、文档编解码、屏幕布局
src/render            类型化 IR → 二维数学排版(经 nMarkdown)
src/app               设备/主机入口、事件循环、工作区
```

设计上的关键决定:IR 全量驻留,所以表达式相等是 O(1) 的引用比较,
正规形天然规范;代价是节点不回收,由笔记本的预算与自动重建机制兜底。
所有 CAS 入口都带步数与内存预算,超限返回类型化错误而不是拖死设备。

## CAS 现状与主流 CAS 的差距

下面的对照基于对当前代码的逐项实测(2026-07),对比对象是
Mathematica / Maple / SymPy / TI 自带 CAS 这一档的通用系统。这份清单
既是诚实的边界声明,也是路线图。

### 已经做到的

- 精确有理算术与规范正规形(收集、折叠、幂规则、奇偶性);
- `Together` / `Cancel` 先做 LCD 与可见因子精确除法,再用有界
  `Q[x]` 欧几里得 GCD(最高 48 次幂)消去隐藏的一元多项式公因子:
  `(x²−1)/(x²−2x+1) → (x+1)/(x−1)`;
- `Factor` 先做模导数 GCD/CRT 与 Yun 平方自由分解，再经有限域
  Berlekamp、Hensel 提升和精确 Zassenhaus 重组完成有界高次分解；
  有理根只是快路径，任何候选都要通过 `Q[x]` 精确整除；
- `Apart` 对唯一变量的 `Q[x]` 有理函数先做多项式除法，再复用同一
  因式分解内核，并用精确高斯消元求出不可约因子各次幂上的部分分式；
  发布前逐系数验证原方程组；
- `FullSimplify` 接入判零级三角基:`cos²x − sin²x − cos2x → 0`、
  `tan x·cos x − sin x → 0`、`sin²+cos² → 1`(后者在普通正规形中即成立);
- `D` 覆盖多项式、初等/反三角/双曲函数及
  `Gamma`、`LogGamma`、`Erf`、`Erfc` 的首批精确规则;
- `Integrate` 除线性内层类外,已覆盖
  `1/(1±u²)`、`1/√(1±u²)`、高斯 `exp(-u²)` 与 `Erf/Erfc`;
  每条新规则均以“再求导得到原式”验收,类外返回未求值的 `Integrate`;
- `Pi/E/I/EulerGamma/Infinity` 为受保护常量;支持常用三角特殊角、
  `Sqrt[72] → 6√2`、`Gamma[n]`、`Gamma[1/2]` 与误差函数零值;
- 判零/等价决策:多生成元有理函数域上的精确判定,带资源预算与
  增量降级策略;
- 假设系统雏形:非零与符号假设参与判零(`PHY_ERR_ASSUMPTION` 路径);
- 规则替换 `phy_cas_substitute`(结构匹配,无通配模式)。

### 尚未做到的(按痛感排序)

| 能力 | 现状 | 主流 CAS 的做法 |
| --- | --- | --- |
| 多项式因式分解 / GCD | `Cancel` 有最高 48 次的 `Q[x]` GCD 与带精确重构的有界多元消因子；`Factor` 已接通模导数 GCD/CRT、Berlekamp、Hensel 和精确 Zassenhaus 重组；`Apart` 已支持同一有界一元 `Q[x]` 域；完整稀疏多元 GCD 尚缺 | 更快的 van-Hoeij/LLL 重组、Brown/Zippel/子结果式多元 GCD、多元部分分式 |
| 方程求解 | `Solve` 未实现 | 多项式求根、有理化、Gröbner 基、超越方程分支 |
| 极限与级数 | 已有精确有界 Taylor/Laurent `Series`/`Normal`，以及有限点、显式单侧和有理无穷远 `Limit`；振荡、分支敏感和超出现有系数域的情形诚实返回类型化错误 | 更完整的渐近级数环、Gruntz 类比较与分支/条件系统 |
| 积分覆盖面 | 已覆盖反三角核、双曲线性核和高斯/误差函数;分部积分、一般有理函数/Risch 尚缺 | Risch 结构定理、Meijer-G 表驱动 |
| 根式化简 | 小型正有理根式可抽平方因子;`Sqrt[x²]` 仍保守保留(缺 `Abs`) | 根式正规形 + 分母有理化 + 假设驱动的 `|x|` |
| 特殊值/常数表 | 常量和常见角已实现,尚无大规模恒等式/解析延拓表 | π/e 常数语义 + 大型特殊值表 |
| 反三角/双曲函数 | 导数、奇偶性、零值与四个积分核已实现;完整恒等式族尚缺 | 完整导数/恒等式/特殊值表 |
| 对称参数三角恒等式 | `sin(x+y)` 不展开(倍角 `sin(kx)` 已覆盖) | 完整 TrigExpand/TrigReduce 重写族 |
| 复数 | `I` 只是符号,`I² ≠ −1`;无 `Conjugate/Re/Im/Abs` | 高斯有理域 + 复域假设 |
| 数值层 | 完全没有浮点:`N[]` 不求值,小数字面量精确化为有理数 | 任意精度球算术 / 机器浮点双轨 |
| 大整数 | 原生受限任意精度整数/有理数已贯通 IR、CAS、序列化和二维排版；尚无快速乘法与代数扩域 | GMP/FLINT 的渐近快速算法与成熟代数数域 |
| 模式匹配语言 | 仅结构替换,无 `x_` 通配/条件规则 | 完整规则重写语言 |
| 特殊函数 | 首批 Γ/LogGamma/erf/erfc 已实现;Bessel/多对数与数值算法尚缺 | 大规模特殊函数库 |
| Kerr 级表达式膨胀 | 稠密 Riemann 展开超设备预算一个量级(实测 190 万节点/144 MiB),Kerr 曲率暂缓 | 不透明标量(Σ、Δ)+ 边关系的定向归约;见 `docs/references/GENERAL_RELATIVITY.md` |

设计立场需要说明:**没有浮点是刻意的**(精确性是整个判零体系的地基),
但一个"求个数值看看"的受控数值层(区间或定点)在路线图上;类外 `Factor` 和 `Apart`
返回 UNSUPPORTED 而不是伪装成不透明函数,也是刻意的——宁可诚实失败,
不做看起来成功的空操作。

## 已知限制

- 流形单图册;张量最高 4 阶、维数最高 4;`DiracTrace` 最多 8 个 γ;
- 倍角归约上限 `|k| ≤ 64`;
- IR 节点驻留不回收:重求值把上下文填满时,笔记本自动重建并隔离
  肇事 cell(绑定丢失、输出标记 stale,重跑即可);
- 文档格式 `PHYNB001` 只存 cell 源文本与规范 IR 文本,不存对象——
  打开文档等价于按序重放全部输入。

## 测试与验收

- Windows 严格配置 34/34，WSL ASan/UBSan/Leak 配置 36/36，
  断言型测试合计 213,505 条检查;
- GR 金标语料(`research/corpus/gr_golden.json`)由 SymPy 独立生成,
  设备管线的每个曲率分量与之精确判等;
- 像素级回归:笔记本首帧渲染有 64 位指纹固定;
- 每次发版:`phy-make-cas-tour` 重新生成并完整重放示例笔记本,
  任何解析/求值/序列化/重放失败都会使生成中止。

## 文档地图

| 文档 | 内容 |
| --- | --- |
| `docs/ARCHITECTURE.md` | 分层与依赖规则 |
| `docs/CAS.md` | 标量 CAS:正规形与判零的完整设计 |
| `docs/CAS_FOUNDATION.md` | CAS 地基加固:依赖顺序、语义合同与验收矩阵 |
| `docs/EVALUATOR.md` | 类型化求值器与全部读者语法 |
| `docs/SOURCE_LANGUAGE.md` | 输入语言与命令表 |
| `docs/NOTEBOOK.md` | 笔记本 UI、文档格式、恢复机制 |
| `docs/GR.md`、`docs/references/GENERAL_RELATIVITY.md` | 曲率管线、约定、Kerr 测量记录 |
| `docs/CAS_ACCEPTANCE.md` | 验收边界:什么有可执行证据 |

## 致谢与许可

以 GPL-3.0 发布。二维数学排版基于内置的 nMarkdown 排版器
(见 `THIRD_PARTY_NOTICES.md`)。感谢 Ndless 社区让原生 Nspire
开发成为可能。
