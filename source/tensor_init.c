#include "tensor_struct.h"
#include "tensor_ops.h"

void *__init_linear_space(size_t len)
{
	return NULL;
}

static void __init_free_group_list(struct group_struct *group)
{
	word_t order;

	for (word_t index = 0; index < MIGRATION_TYPES; index++) {
		for (order = 0; order < GROUP_ORDER_NUM; order++) {
			init_list_node(&group->free_group[index].free_cell[order]);
			group->free_group[index].free_count[order] = 0;
		}
	}
}

static void __init_free_node_list(struct node_struct *pnode)
{
	word_t core;

	for (core = 0; core < PNODE_NUM; core++) {
		init_list_node(&pnode->pcp.free_cell[core]);
		pnode->pcp.free_count[core] = 0;
	}
}
static void __init_group_attr(struct group_attr *attr, range_base_t addr, range_base_t size,
		      word_t nitems, word_t status)
{
	attr->addr = addr;
	attr->size = size;
	attr->nr_freeitems = nitems;
	attr->status = status;
}

void __init_pa_pernode(struct node_struct *self)
{
	void *item_ptr;

	if (!self->attr.size)
		return;

	int pn_size_shift = fls(self->attr.size) - 1;
	int pn_item = BIT(pn_size_shift - GROUP_ORDER_NUM);
	size_t item_size = sizeof(struct item_struct) * pn_item;

	item_ptr = __init_linear_space(item_size);

	int pid = self->pnode_id;

	g_item_struct[pid] = (struct item_struct *)item_ptr;
	g_item_struct_end[pid] = item_ptr + item_size;
	g_item_struct_of[pid] = (void *)ROUND_UP(
		POINTER_TO_UINT(g_item_struct_end[pid]), FIXED_ITEM_SIZE);
	for (int index = 0; index < pn_item; index++) {
		init_list_node(&g_item_struct[pid][index].node);
		g_item_struct[pid][index].flags = 0;
		g_item_struct[pid][index].compound_head = NULL;
	}

}

void __init_pnode(struct node_struct *self)
{
	word_t node_x_nitem = NODE_FIXED_RANGE_SIZE / FIXED_ITEM_SIZE;

	self->pnode_id = get_num_pnode(self);
	spin_lock_init(&self->node_struct_lock);
	self->group_vtbl = &g_group_struct_vtbl;
	self->vtbl = &g_node_vtbl;
	__init_free_node_list(self);
	__init_group_attr(&self->attr, NODE_FIXED_RANGE_BASE, NODE_FIXED_RANGE_SIZE, node_x_nitem,
			 0);
}

void __init_group_pernode(struct node_struct *self)
{
	word_t node_x_nitem = self->attr.size / FIXED_ITEM_SIZE;


	int pid = self->pnode_id;

	g_group_lvl[pid] = nitems_to_lvl(&node_x_nitem, false);
	if (g_group_lvl[pid] < 0)
		printf("No INIT group!\n");

	for (int lvl = 0; lvl < GROUP_LVL_NUM; lvl++) {
		spin_lock_init(&g_group_struct[pid][lvl].group_struct_lock);
		g_group_struct[pid][lvl].pnode = self;
		__init_free_group_list(&g_group_struct[pid][lvl]);
		g_group_struct[pid][lvl].vtbl = &g_group_struct_vtbl;

	}
}

void __init_pnode_pa(struct node_struct *self, bool payload)
{
	int pid = self->pnode_id;
	range_base_t res = self->attr.addr;


	int lvl = g_group_lvl[pid];

	if (payload)
		res = range_transform_base(g_item_struct_of[pid]);


	word_t rem = res - self->attr.addr;
	int rem_pa_offset = rem / FIXED_ITEM_SIZE;
	int rel_pa_offset = self->attr.size / FIXED_ITEM_SIZE;

	int block_shift;
	int block_size;
	int max_block_free;
	struct item_struct *pa;
	struct group_struct *group;
	struct item_struct *pa_addr;


#ifndef DOWN_GROW
	pa_addr = g_item_struct_end[pid];
#else
	pa_addr = g_item_struct[pid];
#endif

	long reserved_idx_end = -1;
	long reserved_idx_start = reserved_idx_end;

#ifdef RESERVED_MEMBASE
	reserved_idx_start = range_to_idx(RESERVED_MEMBASE);
	reserved_idx_end = reserved_idx_start + range_to_idx(RESERVED_MEMSIZE);
#endif

	rel_pa_offset -= rem_pa_offset;
	self->attr.nr_freeitems = rel_pa_offset;
	for (; lvl < GROUP_LVL_NUM; ++lvl) {
		int order_index;
		long idx;

		block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, lvl);
		block_size = BIT(block_shift);
		if (block_size > rel_pa_offset)
			continue;
		idx = range_to_idx(self->attr.addr);
		if (idx & (block_size - 1)) {
			g_group_lvl[pid]++;
			continue;
		}

		for (order_index = MAX_ITEM_ORDER; order_index >= 0;
		     order_index--) {
			max_block_free =
				rel_pa_offset / (block_size * BIT(order_index));
			if (!max_block_free)
				continue;

			if (lvl == g_group_lvl[pid] && !g_group_order[pid])
				g_group_order[pid] =
					MIN(order_index + 1, GROUP_ORDER_NUM);

			group = get_group_lvl(lvl, pid);
			for (int i = max_block_free; i > 0; --i) {
#ifndef DOWN_GROW
				pa = pa_addr -
				     i * block_size * BIT(order_index);
				set_item_order(pa, order_index);
				set_item_node(pa, pid);
				idx = item_to_idx(pa);
				assert_msg(
					!(idx & (block_size - 1)),
					"[idx %lx, block_size %lx, lvl %d, pid %d, order_index %d, pa %p, g_pa %p]\n",
					idx, block_size, lvl, pid, order_index,
					pa, g_item_struct);
				if (idx >= reserved_idx_start &&
				    idx < reserved_idx_end) {
					// coverity[dead_error_begin:SUPPRESS]
					list_add_tail(&pa->node,
						    &GROUP_CELL(
							    group,
							    MIGRATION_UNMOVABLE)
							    [order_index]);
					GROUP_CELLCOUNT(group, MIGRATION_UNMOVABLE)
					[order_index]++;
				} else {
					list_add_tail(&pa->node,
						    &GROUP_CELL(
							    group,
							    MIGRATION_MOVABLE)
							    [order_index]);
					GROUP_CELLCOUNT(group, MIGRATION_MOVABLE)
					[order_index]++;
				}

#else
				pa = pa_addr +
				     (i - 1) * block_size * BIT(order_index);
				set_item_order(pa, order_index);
				set_item_node(pa, pid);
				idx = item_to_idx(pa);
				assert_msg(
					!(idx & (block_size - 1)),
					"[idx %lx, block_size %lx, lvl %d, pid %d, order_index %d, pa %p, g_pa %p]\n",
					idx, block_size, lvl, pid, order_index,
					pa, g_item_struct);
				if (idx >= reserved_idx_start &&
				    idx < reserved_idx_end) {
					list_add_tail(&pa->node,
						    &GROUP_CELL(
							    group,
							    MIGRATION_UNMOVABLE)
							    [order_index]);
					GROUP_CELLCOUNT(group, MIGRATION_UNMOVABLE)
					[order_index]++;
				} else {
					list_add_tail(&pa->node,
						    &GROUP_CELL(
							    group,
							    MIGRATION_MOVABLE)
							    [order_index]);
					GROUP_CELLCOUNT(group, MIGRATION_MOVABLE)
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

	printf("rel nitems is %d-%ld, g_lvl-g_order is %d-%d\n",
		      rel_pa_offset, self->attr.nr_freeitems, g_group_lvl[pid],
		      g_group_order[pid]);

}

void _init_pnode(struct node_struct *self)
{
	__init_pnode(self);
	__init_pa_pernode(self);
	__init_group_pernode(self);
	__init_pnode_pa(self, true);
}

void mm_init(void)
{
	struct node_struct *pnode;


	for (int i = 0; i < PNODE_NUM; i++) {
		pnode = get_pnode_num(i);
		_init_pnode(pnode);
	}

}

// __init
status_t init_pnode(struct node_struct *self)
{
	_init_pnode(self);
	return NO_ERROR;
}

int get_recent_node_num(void)
{
	// TODO: config
	return 0;
}
int arch_curr_cpu_num(void)
{
	return 0;
}