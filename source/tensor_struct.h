#pragma once

#include <types.h>
#include <stdlib.h>
#include <stdio.h>
#include <list.h>
#include <err.h>

// ---------------------------------------------------
/* physical memory range set - global and shared */
#define NODE_FIXED_RANGE_SIZE 0x40000000 // TODO:
#define NODE_FIXED_RANGE_SIZE_SHIFT (fls(NODE_FIXED_RANGE_SIZE) - 1)
#define NODE_FIXED_RANGE_BASE 0x80000000 // TODO:

/* virtual memory range set - monopolize all virtual memory */
#define VNODE_FIXED_RANGE_SIZE BIT(64)
#define VNODE_FIXED_RANGE_SIZE_SHIFT 64
#define VNODE_FIXED_RANGE_BASE 0

/* item of UNMOVABLE and LRU_CACHE-NODE cache types do not participate in migration
 */
#define LRU_CACHE_MAX_ITEMS 1024
#define UNMOV_MAX_ITEMS 1024

// ---------------------------------------------------
/* physical time range set */
#define NODE_SUPER_PERIOD_WINODW_BASE	0 // window start
#define NODE_SUPER_PERIOD_WINODW_SIZE	 0x40000000

/* virtual time range set */
#define VNODE_SUPER_PERIOD_WINODW_BASE	0 // window start
#define VNODE_SUPER_PERIOD_WINODW_SIZE	 0x40000000

// ---------------------------------------------------
#define NODE_RESERVED_RANGE_BASE 0 // maybe image end symbol value or window align time
#define NODE_RESERVED_RANGE_SIZE 0x100000

#define FIXED_ITEM_SIZE BIT(FIXED_ITEM_SIZE_SHIFT)
#define FIXED_ITEM_SIZE_SHIFT 12

#define NODE_LVL_NUM_SHIFT 4
#define NODE_DIM_NUM_SHIFT 4
#define NODE_LVL_NUM BIT(NODE_LVL_NUM_SHIFT)
#define NODE_DIM_NUM BIT(NODE_DIM_NUM_SHIFT)


/* node_id 
 * 			dim-0(DIM_TIME)			dim-1				MAX:BIT(NODE_DIM_NUM)
 * physical(per-node)	pnode-0, pnode-n, ...		pnode-0, pnode-n, ...		MAX:BIT(NODE_LVL_NUM)
 * virtual(per-task)	vnode-0, vnode-n, ...		vnode-0, vnode-n, ...		MAX:BIT(NODE_LVL_NUM)
 *
 */
struct node_idx {
	union {
		unsigned long pnode_idx : NODE_LVL_NUM;
		unsigned long vnode_idx : NODE_LVL_NUM;
		// TBD, 1-level transform, pnode_idx<->vnode_idx
	};
	unsigned long dim_idx : NODE_DIM_NUM;
};

#define PNODE_NUM BIT(NODE_DIM_NUM+NODE_LVL_NUM)
#define VNODE_NUM BIT(NODE_DIM_NUM+NODE_LVL_NUM)

/* marco */
#define BIT(x) (1ULL << (x))
#define MASK(x) (BIT(x) - 1)
/* bit manipulation */
#define BM(base, count, val) (((val) & (((1ULL) << (count)) - 1)) << (base))
#define AM(base, count) ((((1ULL) << (count)) - 1) << (base))

#define SU(me, val) ((me) |= (val))
#define CU(me, val) ((me) &= (~(val)))
#define GU(me, mask) ((me) & (mask))
#define MU(me, mask, val) ((me) = ((me) & (~GU(me, mask))) | (val))

#define GUB(me, mask, base) (((me) & (mask)) >> (base))
#define GUI(me, mask, base, val) (GUB(me, mask, base) + (val))
#define GUD(me, mask, base, val) (GUB(me, mask, base) - (val))

#define SR(me, base, count, val) (SU(me, BM(base, count, val)))
#define GR(me, base, count) (GU(me, AM(base, count)) >> (base))
#define CR(me, base, count, val) (CU(me, BM(base, count, val)))
#define ZR(me, base, count) (CU(me, AM(base, count)))
#define MR(me, base, count, val) (MU(me, AM(base, count), BM(base, count, val)))

#define IR(me, base, count, val)                                               \
	(MU(me, AM(base, count),                                               \
	    BM(base, count, GUI(me, AM(base, count), base, val))))
#define DR(me, base, count, val)                                               \
	(MU(me, AM(base, count),                                               \
	    BM(base, count, GUD(me, AM(base, count), base, val))))

#define SB(me, base) ((me) |= BIT(base))
#define GB(me, base) (((me)&BIT(base)) >> (base))
#define CB(me, base) ((me) &= ~BIT(base))

/* for translation */
#define GROUP_LVL_NUM 4
#define GROUP_ORDER_NUM_SHIFT 12 // (1<<12, 4k)，addr order as group freelist idx
#define GROUP_RANGE_NUM_SHIFT 12 // (1<<12, 4k)，addr range as group freelist idx
#define GROUP_PRIOR_NUM_SHIFT 3

#define MAX_ITEM_ORDER (FUNCTION_ITEM(GROUP_ORDER_NUM_SHIFT) - 1)
#define MAX_ITEM_RANGE (FUNCTION_ITEM(GROUP_RANGE_NUM_SHIFT) - 1)
#define MAX_ITEM_PRIOR (BIT(GROUP_PRIOR_NUM_SHIFT) - 1) 

#define GROUP_IDX_NUM_SHIFT 12 // max(GROUP_ORDER_NUM_SHIFT, GROUP_RANGE_NUM_SHIFT) 
#define MAX_IDX_NUM (FUNCTION_ITEM(GROUP_IDX_NUM_SHIFT) - 1)
#define MAX_GROUP_LVLS (GROUP_LVL_NUM - 1)

#define FUNCTION_ITEM_LVL_PRIOR(item_shift, level) \
	(GROUP_LVL_NUM - level - 1) * (item_shift)
#define FUNCTION_ITEM_LVL(item_shift, level)                                             \
	((GROUP_LVL_NUM - (level)) * ((item_shift)-3) + 3 - (item_shift))
#define FUNCTION_RANGE_LVL(item_shift, level)                                             \
		((GROUP_LVL_NUM - (level)) * ((item_shift)-3) + 3)
#define FUNCTION_ITEM(item_shift) ((item_shift)-3) // 迭代的尺寸，例如12对应9
// such as 4K: level=0,39(512G). 1,30(1G). 2,21(2M).

#define GROUP_CELL(area, migration) ((area)->free_group[migration].free_cell)
#define GROUP_CELLCOUNT(area, migration)                                        \
	((area)->free_group[migration].free_count)
#define LRU_CACHE_CELL(node) ((node)->lru_cache.free_cell[arch_curr_cpu_num()])
#define LRU_CACHE_CELLCOUNT(node) ((node)->lru_cache.free_count[arch_curr_cpu_num()])

/* item flags set */
enum item_flags {
	BASE_ITEM_PRESENT = 0,
	BASE_ITEM_ORDER = 1,
	OFFSET_ITEM_ORDER = 4,
	BASE_ITEM_STATE = 5,
	OFFSET_ITEM_STATE = 4,
	BASE_ITEM_TABLE = 9,
	OFFSET_ITEM_TABLE = 5,
	BASE_ITEM_NODE = 14,
	OFFSET_ITEM_NODE = 5,
	BASE_ITEM_SECTION = 19,
	OFFSET_ITEM_SECTION = 5,
	BASE_ITEM_DIM = 24,
	OFFSET_ITEM_DIM = 5,	
	// special
	BASE_ITEM_FUNC = 29,
	OFFSET_ITEM_FUNC = 3,
	BASE_ITEM_FREEIDX = 32,
	OFFSET_ITEM_FREEIDX = 32, // the number of next item array that free idx
 				  // init = BIT(MAX_ITEM_RANGE+1)
	NR_ITEM_FLAGS_BITS = 63,
};

/* state flags */
enum item_state {
	STATE_ITEM_MOVABLE_NORMAL,
	STATE_ITEM_MOVABLE_COMP,
	STATE_ITEM_MOVABLE_NODE,    // from any node, include recent node
	STATE_ITEM_SLUB_TYPES,
	STATE_ITEM_SPARSE_TYPES,
	STATE_ITEM_SPARSE_PRIOR,
	STATE_ITEM_SPARSE_VIRTUAL_STREAM,
	STATE_ITEM_SPARSE_PHYSICAL_STREAM,
	STATE_ITEM_NOMOVABLE_TYPES, // only recent node
	STATE_ITEM_RECLAIM_TYPES,   // TBD
	STATE_ITEM_LRU_CACHE_TYPES,
	STATE_ITEM_COUNT
};

// ITEM_FLAGS: FUNC
enum item_func {
	// virtual
	FUNC_VIRTUAL = 0,
	FUNC_VIRTUAL_GRAPS,
	FUNC_VIRTUAL_NORMAL,
	FUNC_VIRTUAL_DMA,
	FUNC_VIRTUAL_MMIO,
	FUNC_VIRTUAL_MMIO_EMUL,
	FUNC_VIRTUAL_SHARE,
	FUNC_VIRTUAL_CACHE,
	// virtual stream
	FUNC_VIRTUAL_STREAM,

	// physical
	FUNC_PHYSICAL,
	FUNC_PHYSICAL_UNDEFINED,
	// must aligned by block_size of mmu, disable is_combined
	FUNC_PHYSICAL_CONT,
	FUNC_PHYSICAL_DCONT,
	FUNC_PHYSICAL_MMU,
	FUNC_PHYSICAL_OBJECT,
	FUNC_PHYSICAL_DEBUG,
	FUNC_PHYSICAL_DEVICE, 
	FUNC_PHYSICAL_RESERVED,
	// physical stream
	FUNC_PHYSICAL_STREAM,

	// entity
	FUNC_ENTITY,

	FUNC_ITEM_COUNT
};


struct item_struct_list {
	struct list_node item_list;
	word_t item_count;
};

#define SLICE_STRUCT_MIN_BSIZE (FIXED_ITEM_SIZE / BITS_PER_LONG)

struct slice_struct {
	struct item_struct_list free;
	struct item_struct_list full;
	struct item_struct_list empty;
	size_t bsize;
	unsigned int threshold_freeitems;
};


/* 				node			next 			prev 
 *	allocat/usage(free)	√			×			×
 *	allocat(sparse-t)	(nr_freeidxs!=0)?√:×	√(item array)		√(parent item)
 *	allocat(sparse-b)	×			×			√(parent item)
 *	usage(slice)		√(free/full/empty)	√(head(align 2)/stat)	√(slice-ref)		
 *	usage(compound)		√(node+hnode)		√(head+hhead)		×
 */

// task, time, memory
struct item_struct {
	// 2word
	union {
		word_t word0[2];
		struct list_node node;
		struct {
			struct item_struct *compound_node; // current node, point to next 'compound_node' or as 'compound_head'
			struct item_struct *compound_hnode; // other node, point to next 'compound_head' or as 'compound_hhead'
		};
	};
	// 1word
	word_t flags;
	// 2word
	union {
		word_t word1[2];
		struct {
			struct item_struct *next;
			struct item_struct *prev;
		};
		struct {
			union {
				struct {
					struct slice_struct *slice_head;
					word_t slice_stat : 2;
					// Slack stealing or Partial mapping .etc
				};
				struct {
					word_t compound_head:1; 
					// Discontinuous memory mapping or Task/Memory splitting + load balancing .etc
				};
			};
			union {
				word_t slice_ref;
				// slice bitmap array (max BITS_PER_LONG)
			};			
		};
		struct {
			struct assets_struct *assets;
		};
	};
	// 3word
	union {
		word_t word2[3];
		union {
		// To record resource usage and aging
		// An aging resource may need to be replaced with a truly needed execution stream	
		union {
			struct item_struct *v_aging_head; // Items other than DIM_TIME item
			word_t v_lazy_allocat; // only DIM_TIME item
			word_t p_switch_time;
		};

		// Aging with replacement
		// The next item stores the previous item
		union {
			word_t v_ownership;
			word_t p_yield_time;
			struct item_struct *p_backup_head;
		};
		};
		union {
			word_t v_deadline_arrive; // deadline time
			word_t v_ref_count; // share user count
			word_t p_last_arrive; // last-arrive-time
			struct item_struct *p_rmap; // paddr to vaddr map
		};
		union {
			word_t v_sporadic_period; // task period time		
			word_t v_map_right; // memory usage map_right
			word_t p_msp; // max schedule prior
			// sword_t p_recent_usage; // task usage time, usage==BIT(order), free item			
			// word_t p_page_flags; // page flags
		};
	};
};

// Physical resource type
enum assets_dim_type {
	DIM_TIME,
	DIM_MEMORY,
	DIM_IRQ,
	DIM_BLOCK,
	DIM_NET,
	DIM_NUM,
};

struct group_struct;
struct node_struct;

// layered buddy, relative address page
struct group_list {
	struct list_node free_cell[MAX_IDX_NUM+1];
	word_t free_count[MAX_IDX_NUM+1];
};

struct node_list {
	struct list_node free_cell[NODE_LVL_NUM];
	word_t free_count[NODE_LVL_NUM];
};

struct group_struct_vtable {
	status_t (*compound)(struct item_struct **item, word_t nitems,
			     int migration_type, struct alloc_para *para, int dim);
	status_t (*decompose)(struct item_struct *item, bool is_virtual);
	status_t (*dump_area)(struct group_struct *const self, int migration_type);
};

struct node_struct_vtable {
	status_t (*migrate)(struct node_struct *const self,
			    struct item_struct **item, int order, int lvl, int type,
			    bool is_combined);
	status_t (*dispatch)(struct node_struct *const self,
			     struct item_struct **item, int lvl, int migration_type,
			     int order, struct alloc_para *para);	
	status_t (*init)(struct node_struct *const self, range_base_t range, size_t size);
	status_t (*lend)(struct item_struct **item, int migration_type, int lvl,
			 int lvl_order, bool is_combined);
	status_t (*alloc)(struct node_struct *pnode, range_base_t *range_base,
			  word_t nitems, int flags, struct in_paras *paras); // owner or borrower
	status_t (*free)(range_base_t range_base, bool is_virtual, int dim); // owner or borrower
	status_t (*dump_node)(struct node_struct *const self, bool is_virtual);
	status_t (*sched)(struct node_struct *const self, struct in_paras *in, struct in_paras *out);
	status_t (*map)(struct node_struct *const self, range_base_t in, range_base_t *out);
	status_t (*rmap)(struct node_struct *const self, range_base_t out, range_base_t *in);
	status_t (*slice_alloc)(struct slice_struct *slice, range_base_t *range_base);
	status_t (*slice_free)(range_base_t range_base);

};

enum group_status {
	GROUP_IDLE,
	GROUP_FORWARD,
	GROUP_ONLY_FORWARD,
	GROUP_BACKWARD,
	GROUP_STATUS,
};

struct group_attr {
	int dim;
	/* For memory nodes (and new ones), you need to keep range and item monotonically increasing
	 * For the time node (and the new cpu), the range value is the same, 
	 * but you need to keep the item monotonically increasing
	 *
	 * offset requires a preset interval: windows align time or reserved_space
	 */
	range_base_t offset; 	
	size_t size;   // windows size or memory size - not const value
	word_t status; // forward/backward/idle
	word_t nr_freeitems; // predicted value, only record the free number of migration-item
};

/* migrate change ← event-driven */
enum item_migration_type { // Lifecycle constraints(lcc)
	MIGRATION_MOVABLE = 0,
	MIGRATION_SPARSE_MOVABLE = 0, // Response action, event-driven
	MIGRATION_UNMOVABLE = 1,	// Dead, event-driven
	MIGRATION_RECLAIMABLE = 2, // Blocking wait, event-driven
	MIGRATION_LRUTYPE = 3,		// Frequent use, event-driven

	MIGRATION_TYPES
};


struct group_struct {
	union {
	struct {
		word_t group_id;
		struct group_struct_vtable *vtbl;
	};
	};
	
	struct group_list free_group[MIGRATION_TYPES];
	struct node_struct *node;
	spin_lock_t group_struct_lock;
};

struct node_struct {
	// The current node, as the structure of the serialization (single node)/parallelization (multi-node) map
	// 1) Sequential execution(single node)
	// 2) Concurrent execution(multi-node)
	struct group_struct *group;
	struct item_struct *sparse_item_root;	// sparse: ptr, non-sparse: nullptr
	struct slice_struct sparse_itemarray;
	// stream: value; other:0
	word_t sequeue_stream_id;		// absolutely id (item idx), minsize = FIXED_ITEM_SIZE
	
	union {
	struct {
		unsigned long node_id;
		struct group_attr attr;
		struct list_node node; // for entity_node_struct
		struct slice_struct sparse_grouparray;
		
		
		struct node_list lru_cache;		// node cache for Frequent use
		
		struct node_struct_vtable *vtbl;
		struct group_struct_vtable *group_vtbl;
	};
	
	};

	spin_lock_t node_struct_lock;
};

#define NODE_SLICE_STRUCT_ITEM(node) (&((node)->sparse_itemarray))
#define NODE_SLICE_STRUCT_GROUP(node) (&((node)->sparse_grouparray))


// Collection of resources
// A subset of the physical resources obtained by the execution flow
// Execution entity(task_struct): Process Group -> Process -> Child Process -> Thread

/* Execution Step:
 * 				A→F
 *				F←A
 * 1) Process Group(Virtual, Free)     2) Process/Thread(Virtual, Usage) →   ②<delay>|
 * ①<rightnow>f(v,u→p,f) →							     |
 *										     |
 * TS(traverse in sparse)←→ML(migrate at list)←→MM(migrate at migrate type)	     |		
 * 3) Node(Phyiscal, Free)+Process/Thread(Virtual, Usage) 			     |
 * 										     |
 *				A→F						     |
 *				F←A						     |
 * 4) Node(Physical, Free)	       2) Process/Thread(Physical, Usage) <----------
 *
 */
 
/* level define 
 *
 * 1/2/4) size, range ->  A hierarchical unit of measurement
 * 3) workload+prior -> Hierarchical structure, 
 *	such as Process -> Child Process -> Thread
 *
 */
// Input parameters + constraints ----> Output parameters, as scheduling decisions, are event-driven conversion processes
// As a black box, the internal resource allocation mechanism converts parameters to perform resource allocation
struct alloc_para {
	bool is_combined; 
	bool is_virtual; 
	bool is_stream;
	bool is_entry;
	range_base_t max_prior;
	range_base_t affinity; 	
};

struct in_paras {
	union {
		struct {
			union {
				struct {
					// 1) Process Group(Virtual, Free) 
					range_base_t sporadic_period;
					range_base_t delay_time;
					range_base_t deadline_time;
					range_base_t allocat_size;
				};
				struct {
					// 4) Node(Physical, Free)
					range_base_t switch_time; // the task switched time
					range_base_t arrive_time;
					range_base_t yield_time;
				};
				struct {
					// 3) f(v,u→p,f)
					range_base_t base_prior; // such as allocat_size
					range_base_t max_prior;
					range_base_t affinity; // node affinity
					// TODO:
					range_base_t ptimestamp; // special time
				};
			};

			// A task can be executed until the end when it holds the cpu
			// Task preempted of switch out --> lifetime change --> 
			// Task preemptee of switch in

		};
		struct {
			union {
				struct {
					// 1) Process Group(Virtual, Free) 
					range_base_t map_prot; // rwx
					range_base_t memory_attr; // physical memory attr
					range_base_t ownership; // na/ea/la/sa
					range_base_t allocat_size;
				};
				struct {
					// 4) Node(Physical, Free)
					// range_base_t page_flags; // atomic/cond/dcond/lazy/fixed/...
				};
				struct {
					// 3) f(v,u→p,f)
					range_base_t affinity;
					// TODO:
					range_base_t pbase_addr; // special addr
				};
			};

			// Once a task holds memory, it can be used to free it
			// Page replacement of free --> lifetime change -->
			// Page reuse of alloc
		};
	};
};

struct out_state {

};

struct process_group_struct {
	struct node_struct *node[VNODE_NUM];
};

struct assets_attr {
	/* The intersection of multiple dimension item attributes in a scene, 
	 * which is either a linear optimization point or a linear synchronization point 
	 */
	int weight[DIM_NUM];
	/* QoS indicators of different dimensions */
	int radio[DIM_NUM];
	
};

struct assets_struct {
	struct list_node *vnode[VNODE_NUM]; // Each task holds an asset that is considered a collection of virtual resources
	struct list_node *pnode[PNODE_NUM]; // Usage, after allocat, add this!!!
	struct assets_attr attr;
};

#define PROCESS_GROUP_NODE_NU(nu) ((get_current_process_group())->node[(nu)])
#define PROCESS_GROUP_GROUP_NU(lvl, nu)  (&((get_current_process_group())->node[(nu)]->group[(lvl)]))
#define PROCESS_GROUP_GROUP_ROOT_NU(nu)  ((get_current_process_group())->node[(nu)]->sparse_item_root)

#define PROCESS_GROUP_NODE(dim) ((get_current_process_group())->node[get_current_vnode_num(dim)])
#define PROCESS_GROUP_GROUP(lvl, dim) (&((get_current_process_group())->node[get_current_vnode_num(dim)]->group[(lvl)]))
#define PROCESS_GROUP_GROUP_ROOT(dim) ((get_current_process_group())->node[get_current_vnode_num(dim)]->sparse_item_root)

#define NODE_GROUP_ROOT(dim) (get_current_pnode(dim)->sparse_item_root)

#define ENTRY_GROUP_ROOT() (entity_node_struct.sparse_item_root)

extern struct group_struct phy_group_struct[PNODE_NUM][GROUP_LVL_NUM];
extern struct node_struct phy_node_struct[PNODE_NUM];
extern struct item_struct *phy_item_struct[PNODE_NUM];
extern struct item_struct *phy_item_struct_end[PNODE_NUM];
extern void *phy_item_struct_of[PNODE_NUM];
extern int phy_group_lvl[PNODE_NUM];
extern int phy_group_order[PNODE_NUM];
extern struct group_struct entity_group_struct[GROUP_LVL_NUM];
extern struct node_struct entity_node_struct;


extern struct group_struct_vtable g_group_struct_vtbl;
extern struct node_struct_vtable g_node_vtbl;

int get_current_node_num(int dim_type);
int get_current_vnode_num(int dim_type);
int arch_curr_cpu_num(void);
struct task_struct *get_current_task(void);
struct process_group_struct *get_current_process_group(void);

word_t node_budget(int migration_type);
void init_item(struct item_struct *item);



/* DIM_TIME 
 *  	static allocat - 每task
 *	lastarrive（到达时记录该时间）, sporadicperiod（可以进入零星周期无数次-item属性）, delay_time(v_lazy_allocat -item属性) , allocat_size（分配参数）
 *	base: vtime_base -> Vcpu node 这里对于每一个task(vcpu_struct)来说，可以认为其持有整个VCPU节点，每一个节点有一个超周期作为一次可以使用的总时间量
 *	size: vtime_superperiod（可以重复进入超周期无数次）- 当超周期不足时，使用stream_window来补充超周期
 *	idx: vtimeslice.no - 用户使用虚拟时间
 *	
 *	<trans_relat> 将多个Vcpu node转换为单个/多个CPU node的分配，分配规则是优先级等参数 -> 调用runtime allocat由sched()函数决定，sched()决定所有资源的调度
 *	sched函数由事件（异常中断）驱动，lcc被调用
 *	序列化映射：将每一个任务认为独占的资源线性的排列在物理资源的申请队列上
 *	并行化映射：将每一个任务认为独占的多个资源（无依赖）分配在多个物理资源实例上
 *
 *	runtime allocat - 全局共享
 *	recentusage, deadlinearrvie（每次使用时记录该时间）, nextarrive(理论上等于lastarrive+ sporadicperiod), allocat_size（分配参数）
 *	base: ptime_base -> CPU node 物理CPU执行时间
 *	size: ptime_superperiod，一个超周期表示一次可以使用的最大时长，但是可以重复进入超周期无数次 - stream_window
 *	idx: ptimeslice.no - 时间可以做为消耗品，用完即释放；但是也可以重复利用的，在下次零星周期时
 *	负债率：当一个超周期时间分配完毕后，就会超载使用下一个超周期，即'负债'，通过统计负债率来计算可调度性
 *	逾期时间：last_arrive+v_sporadic_period < current_arrive时，则表示该执行已逾期；差值为逾期时间
 *	
 *
 */

/* DIM_MEMORY
 *	static allocat - 每task
 *	prot+attr（item属性）, ref_count（item属性）, delay_time(v_lazy_allocat -Item属性), allocat_size（分配参数）
 *	base: vptr_base(maybe vspace base) -> vspace is the same of many user -> VUMA node 这里对于每一个task(mm_struct)来说，可以认为其持有整个VUMA节点
 *	size:vptr_size(maybe vspace size) - 当vspace空间不足时，使用stream_window来补充虚拟内存
 *	idx: vaddr - 用户使用虚拟内存
 *
 *	<trans_relat> 将多个 VUMA node转换为单个/多个UMA node的分配，分配规则是bound_head(item_time)等参数-> 调用runtime allocat由sched()函数决定，sched()决定所有资源的调度
 *	sched函数由缺页异常驱动，lcc被调用
 *	单节点映射：将每一个任务认为独占的虚拟内存映射到某个内存节点的申请队列上
 *	多节点映射：将每一个任务认为独占的多个虚拟内存（无依赖）分配在多个内存节点上
 *
 *	runtime allocat - 全局共享
 *	page page_spec（item属性）, 反映射rmap（物理到虚拟映射，item属性），allocat_size（分配参数）
 *	base: pptr_base -> UMA node 物理UMA内存节点
 *	size: pptr_size，一个内存节点尺寸表示当前可以使用的内存长度，但是可以通过热插拔动态的增加，或是zram动态的调整节点内存的大小 - stream_window
 *	idx: paddr - 内存是可以重复利用的，在任意时刻；并且可以共享使用
 *	负债率：类似，不过用于计算置换内存量的需求
 */

/* DIM_* 
 * 	其他资源类似， 网络带宽， 磁盘面积， IO时分复用， 中断号范围， 总线带宽，类似虚拟化中的virtio & virq
 *
 */

 /* 分配侧 
  * 对于每一个task所持有的虚拟资源，使用地址范围作为每一个group的freelist代表的单位，即GROUP_RANGE_NUM
  * 每一个freelist的item，表示一块连续的内存，该内存存储的item作为管理这块地址的结构体，内部item可以指向
  * 下一级group的freelist的item，类似页表映射
  */
  
 /* 使用侧 
  * 对于全局所持有的物理资源，使用尺寸范围作为每一个group的freelist代表的单位，即GROUP_ORDER_NUM
  */

