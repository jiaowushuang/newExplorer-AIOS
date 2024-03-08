# newExplorer-AIOS

张量资源管理，建立统一模型来描述和管理调度时间，内存地址空间，中断触发概率分布，网络带宽，磁盘分区/带宽，大量外设，通信（IPC/event）阻塞分布区域以及通信缓存带宽，同步阻塞分布区域以及事务性内存分布区域等

Tensor resource management, building a unified model to describe and manage scheduling time, memory address space, interrupt trigger probability distribution, network bandwidth, disk partition/bandwidth, a large number of peripherals, communication (IPC/event) blocking distribution and communication cache bandwidth, synchronization blocking distribution and transactional memory distribution

机器学习调优，输入不同维度的张量数据（以上描述的），根据业务/用户约束，输出理论最优/近似最优的系统级决策。例如，在输入各类资源向量，且保证实时约束的前提下，输出任务调度策略，内存分配/释放/置换策略，中断分布策略，网络流量管控策略，磁盘IO管控策略，外设分配策略，通信分布策略/带宽管控策略，同步分布策略等

Machine learning tuning, input tensor data of different dimensions (described above), output theoretically optimal/near-optimal system-level decisions based on business/user constraints. For example, under the premise of input various resource vectors and ensuring real-time constraints, output task scheduling policies, memory allocation/free/replacement policies, interrupt distribution policies, network traffic management policies, disk I/O management policies, peripheral allocation policies, communication distribution policies/bandwidth management policies, synchronization distribution policies, etc

AIOS，机器学习算法驱动的设备资源管理器，做它的原因有，内核代码指数级增长的担忧，减少内核漏洞的人力成本的剧增（即使在seL4开发中，形式化验证脚本的成本远超内核设计），复杂的用例需求对内核的畸形改动，内核的多维度启发式参数以“小作坊”的方式进行优化等；而以上所有的担忧，通过机器学习的经验增长来解决

AIOS, a machine learning algorithm-driven device resource manager, is built for the exponential growth of kernel code, the dramatic increase in the human cost of reducing kernel vulnerabilities (even in seL4 development, the cost of formalizing validation scripts far exceeds the cost of kernel design), the need for complex use cases to make abnormal changes to the kernel, the multi-dimensional heuristic parameters of the kernel are optimized in a "small workshop" way. All of these concerns are being addressed by the growing experience of machine learning

# 'Nested + Combined' Model
嵌套，即可以将某个资源一层层分解下去，亦可以一层层封装上去；组合，即可以将某些资源以任意的输入请求进行组合。该模型在调度时间和内存地址空间等资源的抽象上表现良好。
例如，->作为分解符号，那么可以将系统的调度时间抽象为：CPU集群 -> SMP/AMP CPU -> 进程组时间元 -> 进程时间元 -> 线程时间元 -> 子进程时间元 -> 子线程时间元等。
++ 作为组合符号，那么可以将某一事务的调度时间抽象为：事务 = 某几个子进程时间元 ++ 某几个进程时间元 ++ 某几个CPU时间元 ++ ...

内存也是如此，分解过程：NUMA节点 -> UMA节点 -> 区域 -> 空闲链表 -> 页 -> 对象，组合过程：事务 = 某几个内存对象 ++ 某些页 ++ 某几个区域 ++ 某几个节点 ++ ...

Nesting, that is, a resource can be decomposed layer by layer, or can be encapsulated layer by layer; Composition, that is, some resources can be combined with arbitrary input requests. The model performs well in the abstraction of resources such as scheduling time and memory address space.
For example, -> As a decomposition symbol, then you can abstract the scheduling time of the system as: CPU cluster -> SMP/AMP CPU -> Process group time unit -> Process time unit -> thread time unit -> child process time unit -> child thread time unit.
++ as a combination symbol, then the scheduling time of a transaction can be abstracted as: transaction = several sub-process time units ++ several process time units ++ several CPU time units ++...

Memory is the same, decomposition process: NUMA node -> UMA node -> Region -> Free linked list -> page -> object, combination process: transaction = some memory objects ++ some pages ++ some areas ++ some nodes ++...

# install

# issue

# your ideas
1. 当前使用C语言编写'Nested + Combined' Model样例
2. 后续改用Rust优化'Nested + Combined' Model样例
3. 当前使用假设环境（CPU，内存，通信，同步等）进行模拟运行
4. 后续移植到Qemu/评估板硬件上进行验证