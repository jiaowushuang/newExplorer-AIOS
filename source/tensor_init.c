#include "tensor.h"

extern void *built_linear_space(size_t len);

static void __init_free_area_list(struct area_struct *slice)
{
	word_t order;

	for (word_t index = 0; index < MIGRATION_TYPES; index++) {
		for (order = 0; order < GRAIN_SIZE_SHIFT; order++) {
			init_list_node(&slice->free_area[index].freelist[order]);
			slice->free_area[index].free_count[order] = 0;
		}
	}
}

static void __init_free_node_list(struct node_struct *pn)
{
	word_t core;

	for (core = 0; core < NODE_X_NU; core++) {
		init_list_node(&pn->pcp.freelist[core]);
		pn->pcp.free_count[core] = 0;
	}
}
static void __init_area_attr(struct area_attr *attr, paddr_t addr, paddr_t size,
		      word_t ngrains, word_t status)
{
	attr->addr = addr;
	attr->size = size;
	attr->nr_freegrains = ngrains;
	attr->status = status;
}

void __init_pa_pernode(struct node_struct *self)
{
	void *grains_ptr;

	if (!self->attr.size)
		return;

	int pn_size_shift = fls(self->attr.size) - 1;
	int pn_grains = BIT(pn_size_shift - GRAIN_SIZE_SHIFT);
	size_t page_size = sizeof(struct grain_struct) * pn_grains;

	grains_ptr = built_linear_space(page_size);
#ifndef PNODE_NUM
	g_grains = (struct grain_struct *)grains_ptr;
	g_grains_end = grains_ptr + page_size;
	g_grains_of =
		(void *)ROUNDUP(POINTER_TO_UINT(g_grains_end), PAGE_SIZE);
	for (int index = 0; index < pn_grains; index++) {
		init_list_node(&g_grains[index].node);
		g_grains[index].flags = 0;
		g_grains[index].compound_head = NULL;
	}
#else
	int pid = self->pn_id;

	g_grains[pid] = (struct grain_struct *)grains_ptr;
	g_grains_end[pid] = grains_ptr + page_size;
	g_grains_of[pid] = (void *)ROUNDUP(
		POINTER_TO_UINT(g_grains_end[pid]), PAGE_SIZE);
	for (int index = 0; index < pn_grains; index++) {
		init_list_node(&g_grains[pid][index].node);
		g_grains[pid][index].flags = 0;
		g_grains[pid][index].compound_head = NULL;
	}
#endif
}

void __init_pnode(struct node_struct *self)
{
	word_t node_x_ngrains = NODE_X_SIZE / PAGE_SIZE;

	self->pn_id = get_nu_pn(self);
	spin_lock_init(&self->node_struct_lock);
	self->area_vtbl = &g_area_struct_vtbl;
	self->vtbl = &g_node_vtbl;
	__init_free_node_list(self);
	__init_area_attr(&self->attr, NODE_X_BASE, NODE_X_SIZE, node_x_ngrains,
			 0);
}

void __init_slice_pernode(struct node_struct *self)
{
	word_t node_x_ngrains = self->attr.size / PAGE_SIZE;

#ifndef PNODE_NUM
	g_area_lvl = ngrains_to_lvl(&node_x_ngrains, false);
	if (g_area_lvl < 0)
		panic("No INIT slice!\n");

	for (int lvl = 0; lvl < MAX_RECURSION_CNTS; lvl++) {
#else
	int pid = self->pn_id;

	g_area_lvl[pid] = ngrains_to_lvl(&node_x_ngrains, false);
	if (g_area_lvl[pid] < 0)
		panic("No INIT slice!\n");

	for (int lvl = 0; lvl < MAX_RECURSION_CNTS; lvl++) {
#endif

#ifndef PNODE_NUM
		spin_lock_init(&g_pm_area_struct[lvl].area_struct_lock);
		g_pm_area_struct[lvl].pn = self;
		__init_free_area_list(&g_pm_area_struct[lvl]);
		g_pm_area_struct[lvl].vtbl = &g_area_struct_vtbl;
#else
		spin_lock_init(&g_pm_area_struct[pid][lvl].area_struct_lock);
		g_pm_area_struct[pid][lvl].pn = self;
		__init_free_area_list(&g_pm_area_struct[pid][lvl]);
		g_pm_area_struct[pid][lvl].vtbl = &g_area_struct_vtbl;
#endif
	}
}

void __init_pnode_pa(struct node_struct *self, bool payload)
{
	int pid = self->pn_id;
	paddr_t res = self->attr.addr;

#ifndef PNODE_NUM
	int lvl = g_area_lvl;

	if (payload)
		res = vaddr_to_paddr(g_grains_of);
#else
	int lvl = g_area_lvl[pid];

	if (payload)
		res = vaddr_to_paddr(g_grains_of[pid]);
#endif

	word_t rem = res - self->attr.addr;
	int rem_pa_offset = rem / PAGE_SIZE;
	int rel_pa_offset = self->attr.size / PAGE_SIZE;

	int block_shift;
	int block_size;
	int max_block_free;
	struct grain_struct *pa;
	struct area_struct *slice;
	struct grain_struct *pa_addr;

#ifndef PNODE_NUM
#ifndef DOWN_GROW
	pa_addr = g_grains_end;
#else
	pa_addr = g_grains;
#endif
#else
#ifndef DOWN_GROW
	pa_addr = g_grains_end[pid];
#else
	pa_addr = g_grains[pid];
#endif
#endif
	long reserved_pfn_end = -1;
	long reserved_pfn_start = reserved_pfn_end;

#ifdef RESERVED_MEMBASE
	reserved_pfn_start = paddr_to_pfn(RESERVED_MEMBASE);
	reserved_pfn_end = reserved_pfn_start + paddr_to_pfn(RESERVED_MEMSIZE);
#endif

	rel_pa_offset -= rem_pa_offset;
	self->attr.nr_freegrains = rel_pa_offset;
	for (; lvl < MAX_RECURSION_CNTS; ++lvl) {
		int order_index;
		long pfn;

		block_shift = PM_LX_X(GRAIN_SIZE_SHIFT, lvl);
		block_size = BIT(block_shift);
		if (block_size > rel_pa_offset)
			continue;
		pfn = paddr_to_pfn(self->attr.addr);
		if (pfn & (block_size - 1)) {
#ifndef PNODE_NUM
			g_area_lvl++;
#else
			g_area_lvl[pid]++;
#endif
			continue;
		}

		for (order_index = MAX_PAGE_ORDER; order_index >= 0;
		     order_index--) {
			max_block_free =
				rel_pa_offset / (block_size * BIT(order_index));
			if (!max_block_free)
				continue;
#ifndef PNODE_NUM
			if (lvl == g_area_lvl && !g_area_order)
				g_area_order =
					MIN(order_index + 1, GRAIN_SIZE_SHIFT);
#else
			if (lvl == g_area_lvl[pid] && !g_area_order[pid])
				g_area_order[pid] =
					MIN(order_index + 1, GRAIN_SIZE_SHIFT);
#endif
			slice = get_slice_lvl(lvl, pid);
			for (int i = max_block_free; i > 0; --i) {
#ifndef DOWN_GROW
				pa = pa_addr -
				     i * block_size * BIT(order_index);
				set_page_order(pa, order_index);
				set_page_node(pa, pid);
				pfn = page_to_pfn(pa);
				ASSERT_MSG(
					!(pfn & (block_size - 1)),
					"[pfn %lx, block_size %lx, lvl %d, pid %d, order_index %d, pa %p, g_pa %p]\n",
					pfn, block_size, lvl, pid, order_index,
					pa, g_grains);
				if (pfn >= reserved_pfn_start &&
				    pfn < reserved_pfn_end) {
					// coverity[dead_error_begin:SUPPRESS]
					list_add_tail(&pa->node,
						    &AREA_FREELIST(
							    slice,
							    MIGRATION_UNMOVABLE)
							    [order_index]);
					AREA_FREECOUNT(slice, MIGRATION_UNMOVABLE)
					[order_index]++;
				} else {
					list_add_tail(&pa->node,
						    &AREA_FREELIST(
							    slice,
							    MIGRATION_MOVABLE)
							    [order_index]);
					AREA_FREECOUNT(slice, MIGRATION_MOVABLE)
					[order_index]++;
				}

#else
				pa = pa_addr +
				     (i - 1) * block_size * BIT(order_index);
				set_page_order(pa, order_index);
				set_page_node(pa, pid);
				pfn = page_to_pfn(pa);
				ASSERT_MSG(
					!(pfn & (block_size - 1)),
					"[pfn %lx, block_size %lx, lvl %d, pid %d, order_index %d, pa %p, g_pa %p]\n",
					pfn, block_size, lvl, pid, order_index,
					pa, g_grains);
				if (pfn >= reserved_pfn_start &&
				    pfn < reserved_pfn_end) {
					list_add_tail(&pa->node,
						    &AREA_FREELIST(
							    slice,
							    MIGRATION_UNMOVABLE)
							    [order_index]);
					AREA_FREECOUNT(slice, MIGRATION_UNMOVABLE)
					[order_index]++;
				} else {
					list_add_tail(&pa->node,
						    &AREA_FREELIST(
							    slice,
							    MIGRATION_MOVABLE)
							    [order_index]);
					AREA_FREECOUNT(slice, MIGRATION_MOVABLE)
					[order_index]++;
				}
#endif
			}
			rel_pa_offset -=
				max_block_free * block_size * BIT(order_index);
#ifndef DOWN_GROW
			pa_addr = pa;
#else
			pa_addr = pa + max_block_free * block_size *
					       BIT(order_index);
#endif
		}

		if (!rel_pa_offset)
			break;
	}
#ifndef PNODE_NUM
	printf("rel ngrains is %d-%d, g_lvl-g_order is %d-%d\n", rel_pa_offset,
		      self->attr.nr_freegrains, g_area_lvl, g_area_order);
#else
	printf("rel ngrains is %d-%d, g_lvl-g_order is %d-%d\n",
		      rel_pa_offset, self->attr.nr_freegrains, g_area_lvl[pid],
		      g_area_order[pid]);
#endif
}

void _init_pnode(struct node_struct *self)
{
	__init_pnode(self);
	__init_pa_pernode(self);
	__init_slice_pernode(self);
	__init_pnode_pa(self, true);
}

void mm_init(void)
{
	struct node_struct *pn;

#ifndef PNODE_NUM
	pn = get_recent_pn();
	_init_pnode(pn);
#else
	for (int i = 0; i < PNODE_NUM; i++) {
		pn = get_pn_nu(i);
		_init_pnode(pn);
	}
#endif
}
