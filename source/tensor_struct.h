#pragma once

#include <types.h>
#include <stdlib.h>
#include <stdio.h>
#include <list.h>
#include <err.h>

#define NODE_FIXED_RANGE_SIZE 0x40000000 // TODO:
#define NODE_FIXED_RANGE_SIZE_SHIFT (fls(NODE_FIXED_RANGE_SIZE) - 1)
#define NODE_FIXED_RANGE_BASE 0x80000000 // TODO:

#define FIXED_ITEM_SIZE 4096
#define FIXED_ITEM_SIZE_SHIFT 12
/* item of UNMOVABLE and PCP-NODE cache types do not participate in migration
 */
#define PCP_MAX_ITEMS 1024
#define UNMOV_MAX_ITEMS 1024

#define PNODE_NUM 1
#define GROUP_LVL_NUM 4
#define GROUP_ORDER_NUM 9

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

/* define physics address top shift for translation */
#define FUNCTION_ITEM_LVL(item_shift, level)                                             \
	((4 - (level)) * ((item_shift)-3) + 3 - (item_shift))

#define FUNCTION_ITEM(item_shift) ((item_shift)-3)

// such as 4K: level=0,39(512G). 1,30(1G). 2,21(2M).
#define GROUP_CELL(area, migration) ((area)->free_group[migration].free_cell)
#define GROUP_CELLCOUNT(area, migration)                                        \
	((area)->free_group[migration].free_count)
#define PCP_CELL(pnode) ((pnode)->pcp.free_cell[arch_curr_cpu_num()])
#define PCP_CELLCOUNT(pnode) ((pnode)->pcp.free_count[arch_curr_cpu_num()])

#define MAX_ITEM_ORDER (GROUP_ORDER_NUM - 1)
#define MAX_GROUP_LVLS (GROUP_LVL_NUM - 1)



enum group_status {
	GROUP_IDLE,
	GROUP_FORWARD,
	GROUP_ONLY_FORWARD,
	GROUP_BACKWARD,
	GROUP_STATUS,
};

enum item_migration_type {
	MIGRATION_MOVABLE,
	MIGRATION_UNMOVABLE,
	MIGRATION_RECLAIMABLE, // TBD
	MIGRATION_PCPTYPE,
	MIGRATION_TYPES
};

enum item_state {
	STATE_ITEM_MOVABLE_NORMAL,
	STATE_ITEM_MOVABLE_COMP,
	STATE_ITEM_MOVABLE_NODE, // from any node, include recent node
	STATE_ITEM_SLUB_TYPES,
	STATE_ITEM_NOMOVABLE_TYPES, // only recent node
	STATE_ITEM_RECLAIM_TYPES, // TBD
	STATE_ITEM_PCP_TYPES,
	STATE_ITEM_COUNT
};

// FLAGS
enum item_func_type {
	FUNC_UNDEFINED = 0,
	FUNC_SYSCALL_CONT, // must aligned by block_size of mmu, disable is_combined
	FUNC_SYSCALL_DCONT,
	FUNC_MMU,
	FUNC_OBJECT,
	FUNC_DEBUG,
	FUNC_DEVICE, // TBD
	FUNC_RESERVED,
	FUNC_ITEM_COUNT
};

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
	BASE_ITEM_IN_STATE = 24,
	OFFSET_ITEM_IN_STATE = 3,
	BASE_ITEM_FUNC = 27,
	OFFSET_ITEM_FUNC = 3,
	NR_ITEM_FLAGS_BITS = 63,
};

struct item_struct {
	union {
		struct list_node node;
		struct item_struct *compound_node;
	};

	union {
		struct item_struct *compound_head;
	};
	word_t flags;
};

struct group_struct;
struct node_struct;

// layered buddy, relative address page
struct group_list {
	struct list_node free_cell[GROUP_ORDER_NUM];
	word_t free_count[GROUP_ORDER_NUM];
};

struct node_list {
	struct list_node free_cell[PNODE_NUM];
	word_t free_count[PNODE_NUM];
};

struct group_struct_vtable {
	// (hpa)<single unit(ha), signle unit(ha) ...> or (hpa)<signal unit(ha),
	// compound unit(ha) ...> hpa=NULL, only ha
	status_t (*compound)(struct item_struct **hp, word_t nitems,
			     int migration_type, bool is_combined);
	status_t (*decompose)(struct item_struct *hp);
	status_t (*dump_area)(struct group_struct *const self);
};

struct node_struct_vtable {
	status_t (*migrate)(struct node_struct *const self,
			    struct item_struct **hp, int order, int lvl, int type,
			    bool is_combined);
	status_t (*dispatch)(struct node_struct *const self,
			     struct item_struct **hp, int lvl, int migration_type,
			     int order, bool is_combined);
	status_t (*init)(struct node_struct *const self);
	status_t (*lend)(struct item_struct **hp, int migration_type, int lvl,
			 int lvl_order, bool is_combined);
	status_t (*alloc)(struct node_struct *pnode, range_base_t *range_base,
			  word_t nitems,
			  int flags); // owner or borrower
	status_t (*free)(range_base_t range_base); // owner or borrower
	status_t (*dump_node)(struct node_struct *const self);
};

struct group_attr {
	range_base_t addr;
	size_t size;
	word_t status; // forward/backward
	// predicted value, only record the number of migration-page
	word_t nr_freeitems;
};


struct group_struct {
	struct group_list free_group[MIGRATION_TYPES];
	struct group_struct_vtable *vtbl;
	struct node_struct *pnode;
	spin_lock_t group_struct_lock;
};

struct node_struct {
	word_t pnode_id;
	struct group_attr attr;
	struct node_list pcp;
	struct node_struct_vtable *vtbl;
	struct group_struct_vtable *group_vtbl;
	spin_lock_t node_struct_lock;
};


extern struct group_struct g_group_struct[PNODE_NUM][GROUP_LVL_NUM];
extern struct node_struct g_node_struct[PNODE_NUM];
extern struct item_struct *g_item_struct[PNODE_NUM];
extern struct item_struct *g_item_struct_end[PNODE_NUM];
extern void *g_item_struct_of[PNODE_NUM];
extern int g_group_lvl[PNODE_NUM];
extern int g_group_order[PNODE_NUM];

extern struct group_struct_vtable g_group_struct_vtbl;
extern struct node_struct_vtable g_node_vtbl;

int get_recent_node_num(void);
word_t node_budget(int migration_type);
int arch_curr_cpu_num(void);