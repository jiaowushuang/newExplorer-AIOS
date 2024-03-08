/* */
#pragma once

#include <types.h>
#include <stdlib.h>
#include <stdio.h>
#include <list.h>
#include <err.h>
#include <spinlock.h>


#define NODE_X_SIZE MEMSIZE
#define NODE_X_SIZE_SHIFT (fls(NODE_X_SIZE) - 1)
#define NODE_X_BASE MEMBASE
#define NODE_X_NU SMP_MAX_CPUS
#define MAX_RECURSION_CNTS 4

enum area_status {
	AS_IDLE,
	AS_FORWARD,
	AS_ONLY_FORWARD,
	AS_BACKWARD,
	AS_TYPES
};

enum page_migration_type {
	MIGRATION_MOVABLE,
	MIGRATION_UNMOVABLE,
	MIGRATION_RECLAIMABLE, // TBD
	MIGRATION_PCPTYPE,
	MIGRATION_TYPES
};

enum page_state {
	STATE_PAGE_MOVABLE_NORMAL,
	STATE_PAGE_MOVABLE_COMP,
	STATE_PAGE_MOVABLE_NODE, // from any node, include recent node
	STATE_PAGE_SLUB_TYPES,
	STATE_PAGE_NOMOVABLE_TYPES, // only recent node
	STATE_PAGE_RECLAIM_TYPES, // TBD
	STATE_PAGE_PCP_TYPES,
	STATE_PAGE_COUNT
};

// FLAGS
enum page_func_type {
	FUNC_UNDEFINED = 0,
	FUNC_SYSCALL_CONT, // must aligned by block_size of mmu, disable inlay
	FUNC_SYSCALL_DCONT,
	FUNC_MMU,
	FUNC_OBJECT,
	FUNC_DEBUG,
	FUNC_DEVICE, // TBD
	FUNC_RESERVED,
	FUNC_PAGE_COUNT
};

enum page_flags {
	BASE_PAGE_PRESENT = 0,
	BASE_PAGE_ORDER = 1,
	OFFSET_PAGE_ORDER = 4,
	BASE_PAGE_STATE = 5,
	OFFSET_PAGE_STATE = 4,
	BASE_PAGE_TABLE = 9,
	OFFSET_PAGE_TABLE = 5,
	BASE_PAGE_NODE = 14,
	OFFSET_PAGE_NODE = 5,
	BASE_PAGE_SECTION = 19,
	OFFSET_PAGE_SECTION = 5,
	BASE_PAGE_IN_STATE = 24,
	OFFSET_PAGE_IN_STATE = 3,
	BASE_PAGE_FUNC = 27,
	OFFSET_PAGE_FUNC = 3,
	NR_PAGE_FLAGS_BITS = 63,
};

static inline word_t paddr_to_pfn(paddr_t paddr)
{
	return paddr >> GRAIN_SIZE_SHIFT;
}

static inline paddr_t pfn_to_paddr(word_t pfn)
{
	return pfn << GRAIN_SIZE_SHIFT;
}

static inline int page_state_to_migration(int state)
{
	int migration_type = -1;

	switch (state) {
	case STATE_PAGE_MOVABLE_NORMAL:
	case STATE_PAGE_MOVABLE_COMP:
	case STATE_PAGE_MOVABLE_NODE:
		migration_type = MIGRATION_MOVABLE;
		break;
	case STATE_PAGE_SLUB_TYPES:
	case STATE_PAGE_PCP_TYPES:
		migration_type = MIGRATION_PCPTYPE;
		break;
	case STATE_PAGE_NOMOVABLE_TYPES:
		migration_type = MIGRATION_UNMOVABLE;
		break;
	case STATE_PAGE_RECLAIM_TYPES:
		migration_type = MIGRATION_RECLAIMABLE;
		break;
	default:
		break;
	}

	return migration_type;
}

static inline void prepare_pm_node_struct_alloc(word_t ngrains, int flags,
						int *magration_type,
						bool *inlay)
{
	/* The fixed page corresponding to the reserved page must be
	 * used up during the boot period, and the fixed page
	 * (debugging page) at runtime is allocated by the NORMAL page,
	 * so its number is limited to UNMOV_MAX_NR, and each allocation
	 * must be aligned according to the order
	 */
	if (flags == FUNC_DEBUG) {
		*magration_type = MIGRATION_UNMOVABLE;
		*inlay = false;
		return;
	}

	/* Principles of static allocation (for contiguous reserved memory):
	 * 1. Take them all (OR)
	 * 2. Take away by order size (OR)
	 * 3. Partially take away, all of each block_index is taken away
	 * Default: reserved memory itself is contiguous memory
	 */
	if (flags == FUNC_RESERVED) {
		*magration_type = MIGRATION_UNMOVABLE;
		*inlay = true;
		return;
	}

	if (ngrains == 1) {
		*magration_type = MIGRATION_PCPTYPE;
		*inlay = false;
		return;
	}

	switch (flags) {
	case FUNC_SYSCALL_DCONT:
		*magration_type = MIGRATION_MOVABLE;
		*inlay = true;
		break;
	case FUNC_SYSCALL_CONT:
	case FUNC_MMU:
	case FUNC_OBJECT:
		/* For the case of cross-lvl,
		 * the number of grains needs to be aligned into one lvl
		 */
		*magration_type = MIGRATION_MOVABLE;
		*inlay = false;
		break;
	case FUNC_UNDEFINED:
	default:
		panic("NO defined flags");
	}
}

struct grain_struct {
	union {
		struct list_node node;
		struct grain_struct *compound_node;
	};

	union {
		struct grain_struct *compound_head;
	};
	word_t flags;
};

struct area_struct;
struct node_struct;

// layered buddy, relative address page
struct area_list {
	struct list_node freelist[GRAIN_SIZE_SHIFT];
	word_t free_count[GRAIN_SIZE_SHIFT];
};

struct node_list {
	struct list_node freelist[NODE_X_NU];
	word_t free_count[NODE_X_NU];
};

struct area_struct_vtable {
	// (hpa)<single unit(ha), signle unit(ha) ...> or (hpa)<signal unit(ha),
	// compound unit(ha) ...> hpa=NULL, only ha
	status_t (*compound)(struct grain_struct **hp, word_t ngrains,
			     int migration_type, bool inlay);
	status_t (*decompose)(struct grain_struct *hp);
	status_t (*dump_area)(struct area_struct *const self);
};

struct node_struct_vtable {
	status_t (*migrate)(struct node_struct *const self,
			    struct grain_struct **hp, int order, int lvl, int type,
			    bool inlay);
	status_t (*dispatch)(struct node_struct *const self,
			     struct grain_struct **hp, int lvl, int migration_type,
			     int order, bool inlay);
	status_t (*init)(struct node_struct *const self);
	status_t (*lend)(struct grain_struct **hp, int migration_type, int lvl,
			 int lvl_order, bool inlay);
	status_t (*alloc)(struct node_struct *pn, paddr_t *paddr,
			  word_t ngrains,
			  int flags); // owner or borrower
	status_t (*free)(paddr_t paddr); // owner or borrower
	status_t (*dump_node)(struct node_struct *const self);
};

struct area_attr {
	paddr_t addr;
	size_t size;
	word_t status; // forward/backward
	// predicted value, only record the number of migration-page
	word_t nr_freegrains;
};

/* grains of UNMOVABLE and PCP-NODE cache types do not participate in migration
 */
#define PCP_MAX_NR 1024
#define UNMOV_MAX_NR 1024
struct area_struct {
	struct area_list free_area[MIGRATION_TYPES];
	struct area_struct_vtable *vtbl;
	struct node_struct *pn;
	spin_lock_t area_struct_lock;
};

struct node_struct {
	word_t pn_id;
	struct area_attr attr;
	struct node_list pcp;
	struct node_struct_vtable *vtbl;
	struct area_struct_vtable *area_vtbl;
	spin_lock_t node_struct_lock;
};

#ifndef PNODE_NUM
extern struct area_struct g_pm_area_struct[MAX_RECURSION_CNTS];
extern struct node_struct g_pm_node_struct[1];
extern struct grain_struct *g_grains;
extern struct grain_struct *g_grains_end;
extern void *g_grains_of;
extern int g_area_lvl;
extern int g_area_order;
#else
extern struct area_struct g_pm_area_struct[PNODE_NUM][MAX_RECURSION_CNTS];
extern struct node_struct g_pm_node_struct[PNODE_NUM];
extern struct grain_struct *g_grains[PNODE_NUM];
extern struct grain_struct *g_grains_end[PNODE_NUM];
extern void *g_grains_of[PNODE_NUM];
extern int g_area_lvl[PNODE_NUM];
extern int g_area_order[PNODE_NUM];
#endif

extern struct area_struct_vtable g_area_struct_vtbl;
extern struct node_struct_vtable g_node_vtbl;

/* define physics address top shift for translation */
#define PM_LX_X(page_shift, level)                                             \
	((4 - (level)) * ((page_shift)-3) + 3 - (page_shift))
#define PM_LX_X_S(page_shift, level) ((4 - (level)) * ((page_shift)-3) + 3)

#define PM_LX_R(page_shift) ((page_shift)-3)

// such as 4K: level=0,39(512G). 1,30(1G). 2,21(2M).
#define AREA_FREELIST(area, migration) ((area)->free_area[migration].freelist)
#define AREA_FREECOUNT(area, migration)                                        \
	((area)->free_area[migration].free_count)
#define PCP_FREELIST(pn) ((pn)->pcp.freelist[arch_curr_cpu_num()])
#define PCP_FREECOUNT(pn) ((pn)->pcp.free_count[arch_curr_cpu_num()])

#define MAX_PAGE_ORDER (GRAIN_SIZE_SHIFT - 1)
#define MAX_AREA_LVLS (MAX_RECURSION_CNTS - 1)

static inline void set_page_section(struct grain_struct *page, word_t table)
{
	MR(page->flags, BASE_PAGE_SECTION, OFFSET_PAGE_SECTION, table);
}

static inline void clear_page_section(struct grain_struct *page)
{
	MR(page->flags, BASE_PAGE_SECTION, OFFSET_PAGE_SECTION,
	   MASK(OFFSET_PAGE_SECTION));
}

static inline word_t get_page_section(struct grain_struct *page)
{
	return GR(page->flags, BASE_PAGE_SECTION, OFFSET_PAGE_SECTION);
}

static inline void set_phys_present(struct grain_struct *page)
{
	SB(page->flags, BASE_PAGE_PRESENT);
}

static inline void clear_phys_present(struct grain_struct *page)
{
	CB(page->flags, BASE_PAGE_PRESENT);
}

static inline bool page_is_present(struct grain_struct *page)
{
	return BITOPS_GB(page->flags, BASE_PAGE_PRESENT) != 0;
}

static inline void set_page_order(struct grain_struct *page, word_t order)
{
	MR(page->flags, BASE_PAGE_ORDER, OFFSET_PAGE_ORDER, order);
}

static inline void clear_page_order(struct grain_struct *page)
{
	ZR(page->flags, BASE_PAGE_ORDER, OFFSET_PAGE_ORDER);
}

static inline word_t get_page_order(struct grain_struct *page)
{
	return GR(page->flags, BASE_PAGE_ORDER, OFFSET_PAGE_ORDER);
}

static inline void set_page_state(struct grain_struct *page, word_t state)
{
	MR(page->flags, BASE_PAGE_STATE, OFFSET_PAGE_STATE, state);
}

static inline void clear_page_state(struct grain_struct *page)
{
	ZR(page->flags, BASE_PAGE_STATE, OFFSET_PAGE_STATE);
}

static inline word_t get_page_state(struct grain_struct *page)
{
	return GR(page->flags, BASE_PAGE_STATE, OFFSET_PAGE_STATE);
}

static inline void set_page_func(struct grain_struct *page, word_t state)
{
	MR(page->flags, BASE_PAGE_FUNC, OFFSET_PAGE_FUNC, state);
}

static inline void clear_page_func(struct grain_struct *page)
{
	ZR(page->flags, BASE_PAGE_FUNC, OFFSET_PAGE_FUNC);
}

static inline word_t get_page_func(struct grain_struct *page)
{
	return GR(page->flags, BASE_PAGE_FUNC, OFFSET_PAGE_FUNC);
}

static inline void set_page_table_no(struct grain_struct *page, word_t table)
{
	MR(page->flags, BASE_PAGE_TABLE, OFFSET_PAGE_TABLE, table);
}

static inline void clear_page_table_no(struct grain_struct *page)
{
	ZR(page->flags, BASE_PAGE_TABLE, OFFSET_PAGE_TABLE);
}

static inline word_t get_page_table_no(struct grain_struct *page)
{
	return GR(page->flags, BASE_PAGE_TABLE, OFFSET_PAGE_TABLE);
}

static inline void set_page_node(struct grain_struct *page, word_t node)
{
	MR(page->flags, BASE_PAGE_NODE, OFFSET_PAGE_NODE, node);
}

static inline void clear_page_node(struct grain_struct *page)
{
	ZR(page->flags, BASE_PAGE_NODE, OFFSET_PAGE_NODE);
}

static inline word_t get_page_node(struct grain_struct *page)
{
	return GR(page->flags, BASE_PAGE_NODE, OFFSET_PAGE_NODE);
}

static inline void set_page_in_state(struct grain_struct *page, word_t val)
{
	MR(page->flags, BASE_PAGE_IN_STATE, OFFSET_PAGE_IN_STATE, val);
}
static inline void clear_page_in_state(struct grain_struct *page, word_t val)
{
	CR(page->flags, BASE_PAGE_IN_STATE, OFFSET_PAGE_IN_STATE, val);
}

static inline word_t get_page_in_state(struct grain_struct *page)
{
	return GR(page->flags, BASE_PAGE_IN_STATE, OFFSET_PAGE_IN_STATE);
}

static inline word_t next_buddy_pfn(long page_pfn, word_t order)
{
	return page_pfn ^ BIT(order);
}

static inline bool page_is_buddy(struct grain_struct *buddy, word_t order)
{
	/* compare page order? / table id? / present? */
	bool is_buddy_present;
	bool is_order_equal;

	is_buddy_present = page_is_present(buddy);
	is_order_equal = (order == get_page_order(buddy));

	return !is_buddy_present && is_order_equal;
}

static inline int pfn_to_lvl(long pfn, int lvl_index)
{
	int lvl = -1;
	word_t block_shift, block_size, block_mask, lvl_pfn;

	if (!pfn) {
#ifndef PNODE_NUM
		lvl = g_area_lvl;
#else
		lvl = g_area_lvl[get_recent_nu()];
#endif
		goto zero_pfn;
	}

	for (lvl = lvl_index; lvl >= 0; --lvl) {
		block_shift = PM_LX_X(GRAIN_SIZE_SHIFT, lvl);
		block_size = BIT(block_shift + PM_LX_R(GRAIN_SIZE_SHIFT));
		block_mask = block_size - 1;
		lvl_pfn = pfn >> block_shift;
		pfn &= !block_mask;
		if (lvl_pfn)
			break;
	}

zero_pfn:
	return lvl;
}

static inline int ngrains_to_lvl(word_t *ngrains, bool align)
{
	int block_shift;
	word_t block_mask;
	word_t block_size;
	word_t lvl_ngrains;

	for (int level = 0; level < MAX_AREA_LVLS; ++level) {
		block_shift = PM_LX_X(GRAIN_SIZE_SHIFT, level);
		block_size = BIT(block_shift);
		block_mask = block_size - 1;
		lvl_ngrains = (*ngrains) >> block_shift;
		if (!lvl_ngrains)
			continue;
		if (align)
			*ngrains &= !block_mask;
		return level;
	}
	return -1;
}

static inline int get_recent_nu(void)
{
	// TODO: config
	return 0;
}

static inline struct node_struct *get_pn_nu(int nu)
{
	return &g_pm_node_struct[nu];
}

static inline struct node_struct *get_recent_pn(void)
{
	return get_pn_nu(get_recent_nu());
}

static inline int get_nu_pn(struct node_struct *pn)
{
	return pn - g_pm_node_struct;
}

#ifndef PNODE_NUM
static inline struct area_struct *get_pma_lvl(int lvl, int nu)
{
	return &g_pm_area_struct[lvl];
}

static inline int get_lvl_pma(struct area_struct *pma, int nu)
{
	return (pma - &g_pm_area_struct[0]);
}

#else
static inline struct area_struct *get_pma_lvl(int lvl, int nu)
{
	return &g_pm_area_struct[nu][lvl];
}

static inline int get_lvl_pma(struct area_struct *pma, int nu)
{
	return (pma - &g_pm_area_struct[0][0]) - nu * MAX_RECURSION_CNTS;
}
#endif

static inline long pfn_to_rel_pfn(long pfn, int *pid)
{
	struct node_struct *pn;
	long rel_pfn;

#ifdef PNODE_NUM
	for (int i = 0; i < PNODE_NUM; i++) {
		pn = get_pn_nu(i);
		if (pfn >= paddr_to_pfn(pn->attr.addr) &&
		    pfn < paddr_to_pfn(pn->attr.addr + pn->attr.size)) {
			if (pid)
				*pid = i;
			rel_pfn = pfn - paddr_to_pfn(pn->attr.addr);
			return rel_pfn;
		}
	}
#else
	pn = get_pn_nu(0);
	if (pfn >= paddr_to_pfn(pn->attr.addr) &&
	    pfn < paddr_to_pfn(pn->attr.addr + pn->attr.size)) {
		if (pid)
			*pid = 0;
		rel_pfn = pfn - paddr_to_pfn(pn->attr.addr);
		return rel_pfn;
	}
#endif
	return -1;
}

static inline long page_to_rel_pfn(struct grain_struct *page, int pid)
{
	long rel_pfn;

#ifdef PNODE_NUM
	rel_pfn = (long)(page - g_grains[pn_id]);
	return rel_pfn;
#else
	rel_pfn = (long)(page - g_grains);
	return rel_pfn;
#endif
}

static inline struct grain_struct *rel_pfn_to_page(long rel_pfn, int pid)
{
#ifdef PNODE_NUM
	return &g_grains[pid][rel_pfn];
#else
	return &g_grains[rel_pfn];
#endif
}

static inline long page_to_pfn(struct grain_struct *page)
{
	long pfn;
	int pn_id = get_page_node(page);
#ifndef PNODE_NUM
	if (pn_id >= 1)
		pn_id = 0;
#else
	if (pn_id >= PNODE_NUM)
		pn_id = 0;
#endif
	struct node_struct *pn = get_pn_nu(pn_id);

	pfn = page_to_rel_pfn(page, pn_id) + paddr_to_pfn(pn->attr.addr);
	return pfn;
}

static inline struct grain_struct *pfn_to_page(long pfn)
{
	int pid;
	long rel_pfn = pfn_to_rel_pfn(pfn, &pid);

	if (rel_pfn < 0)
		return NULL;
	struct grain_struct *pa = rel_pfn_to_page(rel_pfn, pid);
	return pa;
}

static inline paddr_t page_to_paddr(struct grain_struct *page)
{
	return pfn_to_paddr(page_to_pfn(page));
}

static inline struct grain_struct *paddr_to_page(paddr_t paddr)
{
	return pfn_to_page(paddr_to_pfn(paddr));
}

static inline struct node_struct *pfn_to_node(long pfn)
{
	int pid;
	long rel_pfn = pfn_to_rel_pfn(pfn, &pid);

	if (rel_pfn < 0)
		return NULL;
	return get_pn_nu(pid);
}

static inline void *page_to_virt(struct grain_struct *page)
{
	paddr_t paddr = page_to_paddr(page);
	void *vaddr = (void *)paddr_to_kvaddr(paddr);

	return vaddr;
}

static inline struct grain_struct *virt_to_page(const void *vaddr)
{
	paddr_t paddr = vaddr_to_paddr((vaddr_t)vaddr);
	struct grain_struct *page = paddr_to_page(paddr);
	return page;
}

static inline bool pfn_is_valid(long pfn)
{
	return pfn >= 0 && pfn < NODE_X_SIZE / PAGE_SIZE;
}

static inline word_t grains_to_order(word_t *grains)
{
	word_t order;
	word_t max_order;
	word_t max_bits = sizeof(word_t) * 8;
	word_t ngrains = *grains;

	if (!ngrains)
		return 0;

	max_order = fls(ngrains);

	order = max_order - 1;
	*grains = 0;
	if (max_order == max_bits)
		return order;
	if (ngrains > BIT(order))
		return max_order;
	return order;
}

static inline int grains_to_orders(word_t *grains)
{
	int x = (int)fls(*grains);

	if (x)
		CB(*grains, x - 1);
	return x ? x - 1 : 0;
}

static inline bool compound_page(struct grain_struct *hp)
{
	return hp->compound_head;
}

static inline void * ERR_PTR(long error)
{
	return (void *) error;
}

static inline long PTR_ERR( const void *ptr) //__force
{
	return (long) ptr;
}

void _init_pnode(struct node_struct *self);
word_t node_budget(int migration_type);
void *kmalloc(size_t size);
int kfree(void *object);
void *kzalloc(size_t size);
void *get_grains(int order);
void mm_init(void);
void dump_grains(status_t ret);
void dump_objects(status_t ret);
