/* */
#pragma once

#include <types.h>
#include <stdlib.h>
#include <stdio.h>
#include <list.h>
#include <err.h>


#include "tensor_struct.h"

#define assert(x) 
#define assert_msg(...)
#define spin_lock_irqsave(x, y)
#define spin_unlock_irqrestore(x, y)
#define spin_lock_init(x)


// helper function
static inline int item_state_to_migration(int state)
{
	int migration_type = -1;

	switch (state) {
	case STATE_ITEM_MOVABLE_NORMAL:
	case STATE_ITEM_MOVABLE_COMP:
	case STATE_ITEM_MOVABLE_NODE:
		migration_type = MIGRATION_MOVABLE;
		break;
	case STATE_ITEM_SLUB_TYPES:
	case STATE_ITEM_LRU_CACHE_TYPES:
		migration_type = MIGRATION_LRUTYPE;
		break;
	case STATE_ITEM_NOMOVABLE_TYPES:
		migration_type = MIGRATION_UNMOVABLE;
		break;
	case STATE_ITEM_RECLAIM_TYPES:
		migration_type = MIGRATION_RECLAIMABLE;
		break;
	case STATE_ITEM_SPARSE_TYPES:
	case STATE_ITEM_SPARSE_STREAM:
		migration_type = MIGRATION_SPARSE_MOVABLE;
	default:
		break;
	}

	return migration_type;
}

static inline void prepare_node_struct_alloc(word_t nitems, int flags,
						int *migration_type,
						bool *is_combined, 
						bool *is_virtual,
						bool *is_stream,
						bool *is_entry)
{
	*is_virtual = false;
	*is_stream = false;
	*is_entry = false;
	/* The fixed item corresponding to the reserved item must be
	 * used up during the boot period, and the fixed item
	 * (debugging item) at runtime is allocated by the NORMAL item,
	 * so its number is limited to UNMOV_MAX_ITEMS, and each allocation
	 * must be aligned according to the order
	 */
	if (flags == FUNC_PHYSICAL_DEBUG) {
		*migration_type = MIGRATION_UNMOVABLE;
		*is_combined = false;
		return;
	}

	/* Principles of static allocation (for contiguous reserved memory):
	 * 1. Take them all (OR)
	 * 2. Take away by order size (OR)
	 * 3. Partially take away, all of each block_index is taken away
	 * Default: reserved memory itself is contiguous memory
	 */
	if (flags == FUNC_PHYSICAL_RESERVED) {
		*migration_type = MIGRATION_UNMOVABLE;
		*is_combined = true;
		return;
	}

	if (nitems == 1 && flags != FUNC_PHYSICAL_STREAM &&
		flags != FUNC_VIRTUAL &&
		flags != FUNC_VIRTUAL_STREAM &&
		flags != FUNC_ENTITY) {
		*migration_type = MIGRATION_LRUTYPE;
		*is_combined = false;
		return;
	}

	switch (flags) {
	case FUNC_PHYSICAL_DCONT:
		*migration_type = MIGRATION_MOVABLE;
		*is_combined = true;
		break;
	case FUNC_PHYSICAL_CONT:
	case FUNC_PHYSICAL_MMU:
	case FUNC_PHYSICAL_OBJECT:
		/* For the case of cross-lvl,
		 * the number of item needs to be aligned into one lvl
		 */
		*migration_type = MIGRATION_MOVABLE;
		*is_combined = false;
		
		break;
	case FUNC_PHYSICAL_STREAM:
		*migration_type = MIGRATION_SPARSE_MOVABLE;
		*is_combined = false;
		*is_stream = true;
		break;	
	case FUNC_VIRTUAL:
		*migration_type = MIGRATION_SPARSE_MOVABLE;
		*is_combined = false;
		*is_virtual = true;		
		break;
	case FUNC_VIRTUAL_STREAM:
		*migration_type = MIGRATION_SPARSE_MOVABLE;
		*is_combined = false;
		*is_virtual = true;
		*is_stream = true;	
		break;
	case FUNC_ENTITY:
		*migration_type = MIGRATION_SPARSE_MOVABLE;
		*is_combined = false;
		*is_entry = true;
	case FUNC_PHYSICAL_UNDEFINED:
	default:
		printf("NO defined flags");
	}
}

// item function
static inline bool compound_item(struct item_struct *item)
{
	return item->compound_head;
}

static inline struct item_struct *next_compound_item(struct item_struct *item)
{
	return item->compound_node;
}

static inline struct item_struct *next_node_compound_item(struct item_struct *item)
{
	return item->compound_hnode;
}


static inline void set_item_section(struct item_struct *item, word_t table)
{
	MR(item->flags, BASE_ITEM_SECTION, OFFSET_ITEM_SECTION, table);
}

static inline void clear_item_section(struct item_struct *item)
{
	MR(item->flags, BASE_ITEM_SECTION, OFFSET_ITEM_SECTION,
	   MASK(OFFSET_ITEM_SECTION));
}

static inline word_t get_item_section(struct item_struct *item)
{
	return GR(item->flags, BASE_ITEM_SECTION, OFFSET_ITEM_SECTION);
}

static inline void set_item_present(struct item_struct *item)
{
	SB(item->flags, BASE_ITEM_PRESENT);
}

static inline void clear_item_present(struct item_struct *item)
{
	CB(item->flags, BASE_ITEM_PRESENT);
}

static inline bool item_is_present(struct item_struct *item)
{
	return GB(item->flags, BASE_ITEM_PRESENT) != 0;
}

static inline void set_item_order(struct item_struct *item, word_t order)
{
	MR(item->flags, BASE_ITEM_ORDER, OFFSET_ITEM_ORDER, order);
}

static inline void clear_item_order(struct item_struct *item)
{
	ZR(item->flags, BASE_ITEM_ORDER, OFFSET_ITEM_ORDER);
}

static inline word_t get_item_order(struct item_struct *item)
{
	return GR(item->flags, BASE_ITEM_ORDER, OFFSET_ITEM_ORDER);
}

static inline void set_item_state(struct item_struct *item, word_t state)
{
	MR(item->flags, BASE_ITEM_STATE, OFFSET_ITEM_STATE, state);
}

static inline void clear_item_state(struct item_struct *item)
{
	ZR(item->flags, BASE_ITEM_STATE, OFFSET_ITEM_STATE);
}

static inline word_t get_item_state(struct item_struct *item)
{
	return GR(item->flags, BASE_ITEM_STATE, OFFSET_ITEM_STATE);
}

static inline void set_item_func(struct item_struct *item, word_t state)
{
	MR(item->flags, BASE_ITEM_FUNC, OFFSET_ITEM_FUNC, state);
}

static inline void clear_item_func(struct item_struct *item)
{
	ZR(item->flags, BASE_ITEM_FUNC, OFFSET_ITEM_FUNC);
}

static inline word_t get_item_func(struct item_struct *item)
{
	return GR(item->flags, BASE_ITEM_FUNC, OFFSET_ITEM_FUNC);
}

static inline void set_item_freeidx(struct item_struct *item, word_t state)
{
	MR(item->flags, BASE_ITEM_FREEIDX, OFFSET_ITEM_FREEIDX, state);
}

static inline void clear_item_freeidx(struct item_struct *item)
{
	ZR(item->flags, BASE_ITEM_FREEIDX, OFFSET_ITEM_FREEIDX);
}

static inline word_t get_item_freeidx(struct item_struct *item)
{
	return GR(item->flags, BASE_ITEM_FREEIDX, OFFSET_ITEM_FREEIDX);
}


static inline void set_item_table_no(struct item_struct *item, word_t table)
{
	MR(item->flags, BASE_ITEM_TABLE, OFFSET_ITEM_TABLE, table);
}

static inline void clear_item_table_no(struct item_struct *item)
{
	ZR(item->flags, BASE_ITEM_TABLE, OFFSET_ITEM_TABLE);
}

static inline word_t get_item_table_no(struct item_struct *item)
{
	return GR(item->flags, BASE_ITEM_TABLE, OFFSET_ITEM_TABLE);
}

static inline void set_item_node(struct item_struct *item, word_t node)
{
	MR(item->flags, BASE_ITEM_NODE, OFFSET_ITEM_NODE, node);
}

static inline void clear_item_node(struct item_struct *item)
{
	ZR(item->flags, BASE_ITEM_NODE, OFFSET_ITEM_NODE);
}

static inline word_t get_item_node(struct item_struct *item)
{
	return GR(item->flags, BASE_ITEM_NODE, OFFSET_ITEM_NODE);
}

static inline void set_item_dim(struct item_struct *item, word_t val)
{
	MR(item->flags, BASE_ITEM_DIM, OFFSET_ITEM_DIM, val);
}
static inline void clear_item_dim(struct item_struct *item, word_t val)
{
	CR(item->flags, BASE_ITEM_DIM, OFFSET_ITEM_DIM, val);
}

static inline word_t get_item_dim(struct item_struct *item)
{
	return GR(item->flags, BASE_ITEM_DIM, OFFSET_ITEM_DIM);
}

static inline word_t next_buddy_idx(long item_idx, word_t order)
{
	return item_idx ^ BIT(order);
}

static inline bool item_is_buddy(struct item_struct *buddy, word_t order)
{
	/* compare item order? / table id? / present? */
	bool is_buddy_present;
	bool is_order_equal;

	is_buddy_present = item_is_present(buddy); 
	// diff: SPARSE_TABLE(allocat) and SPARSE_BLOCK(free) is buddy,
	// and canot buddy merge
	is_order_equal = (order == get_item_order(buddy));

	return !is_buddy_present && is_order_equal;
}

// conversion

static inline word_t range_to_idx(range_base_t range_base)
{
	return range_base >> GROUP_ORDER_NUM_SHIFT;
}

static inline range_base_t idx_to_range(word_t idx)
{
	return idx << GROUP_ORDER_NUM_SHIFT;
}


static inline int idx_to_lvl(long idx)
{
	word_t block_shift, block_size, block_mask, lvl_idx, lvl;

	for (lvl = 0; lvl < GROUP_LVL_NUM; ++lvl) {
		block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM_SHIFT, lvl);
		block_size = BIT(block_shift);
		block_mask = block_size - 1;
		lvl_idx = idx >> block_shift;
		idx &= block_mask;
		if (lvl_idx)
			break;
	}

	return lvl;
}


static inline int nitems_to_lvl(word_t *nitems, bool align)
{
	int block_shift;
	word_t block_mask;
	word_t block_size;
	word_t lvl_nitem;

	for (int level = 0; level < MAX_GROUP_LVLS; ++level) {
		block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM_SHIFT, level);
		block_size = BIT(block_shift);
		block_mask = block_size - 1;
		lvl_nitem = (*nitems) >> block_shift;
		if (!lvl_nitem)
			continue;
		if (align)
			*nitems &= ~block_mask;
		return level;
	}
	return -1;
}


static inline struct node_struct *get_pnode_num(int nu)
{
	return &phy_node_struct[nu];
}

static inline struct node_struct *get_current_pnode(int dim_type)
{
	return get_pnode_num(get_current_node_num(dim_type));
}

static inline int get_num_pnode(struct node_struct *pnode)
{
	int idx = (pnode >=phy_node_struct) ? 
		(pnode - phy_node_struct)   : -1;

	return idx;
}

static inline struct node_struct *get_vnode_num(int nu)
{
	return PROCESS_GROUP_NODE_NU(nu);
}

static inline struct node_struct *get_recent_vnode(int dim_type)
{
	return get_vnode_num(get_current_vnode_num(dim_type));
}

static inline int get_num_vnode(struct node_struct *vnode)
{
	int idx = vnode - PROCESS_GROUP_NODE_NU(0);

	return idx;
}

static inline struct group_struct *get_group_lvl(int lvl, int nu)
{
	return &phy_group_struct[nu][lvl];
}

static inline int get_lvl_group(struct group_struct *group, int nu)
{
	return (group - &phy_group_struct[0][0]) - nu * GROUP_LVL_NUM;
}

static inline struct group_struct *get_vgroup_lvl(int lvl, int nu)
{
	return PROCESS_GROUP_GROUP_NU(lvl, nu);
}

static inline int get_lvl_vgroup(struct group_struct *group, int nu)
{
	return group - PROCESS_GROUP_GROUP_NU(0, nu);
}


static inline long idx_to_rel_idx(long idx, int *pnode_id, int dim)
{
	struct node_struct *pnode;
	long rel_idx;
	int dim_base = dim * NODE_LVL_NUM;

	for (int i = 0; i < NODE_LVL_NUM; i++) {
		pnode = get_pnode_num(i + dim_base);
		if (idx >= range_to_idx(pnode->attr.offset) &&
		    idx < range_to_idx(pnode->attr.offset + pnode->attr.size)) {
			if (pnode_id)
				*pnode_id = i+dim_base;
			rel_idx = idx - range_to_idx(pnode->attr.offset);
			return rel_idx;
		}
	}

	return -1;
}

static inline long idx_to_rel_idx_sparse(long idx, struct item_struct **item, 
		int *node_id, int dim, int endlvl)
{
	struct item_struct *temp, *temp_root;
	word_t block_shift, block_mask;
	long idx_index, freeidx;

	temp = PROCESS_GROUP_GROUP_ROOT(dim);
	for (int level = 0; level < MAX_GROUP_LVLS; ++level) {
		block_shift = FUNCTION_ITEM_LVL(GROUP_RANGE_NUM_SHIFT, level);
		block_mask = BIT(block_shift)-1;
		idx_index = idx >> block_shift;
		idx &= block_mask;
		
		temp_root = (struct item_struct *)item_to_range_transform(temp);
		if (!temp_root)
			return ERR_FAULT;

		temp = &temp_root[idx_index];

		if (endlvl == level)
			break;
		
		if (!temp->next)
			return ERR_FAULT;
		temp = temp->next;
	}
	if (item)
		*item = temp;
	if (node_id)
		*node_id = get_item_node(temp);
	return idx_index;
}

static inline long item_to_rel_idx(struct item_struct *item, int pnode_id)
{
	long rel_idx;

	rel_idx = (long)(item - phy_item_struct[pnode_id]);
	
	return rel_idx;
}

static inline long item_to_rel_idx_sparse(struct item_struct *item)
{
	long rel_idx;

	rel_idx = (long)(item - item->prev->next); // item->prev: root, item->prev->next: item array start

	return rel_idx;
}


// [only physical]
static inline struct item_struct *rel_idx_to_item(long rel_idx, int pnode_id)
{
	return &phy_item_struct[pnode_id][rel_idx];
}


static inline long item_to_idx(struct item_struct *item)
{
	long idx = 0;
	int node_id = get_item_node(item);


	struct node_struct *pnode = get_pnode_num(node_id);
	idx = item_to_rel_idx(item, node_id) + range_to_idx(pnode->attr.offset);


	return idx;
}

static inline long item_to_idx_sparse(struct item_struct *item)
{
	long idx = 0;
	int dim = get_item_dim(item);
	int lvl = get_item_section(item);
	int block_shift = FUNCTION_ITEM_LVL(GROUP_RANGE_NUM_SHIFT, lvl);
	
	struct item_struct *root = PROCESS_GROUP_GROUP_ROOT(dim);
	
	while (item->prev) {
		idx |= item_to_rel_idx_sparse(item) << block_shift;
		block_shift += MAX_ITEM_RANGE+1; 
		item = item->prev;
	}
	
	idx |= (long)(root - item) << block_shift;
	
	return idx;
}

static inline struct item_struct *idx_to_item(long idx, int dim)
{
	int pnode_id;
	long rel_idx = idx_to_rel_idx(idx, &pnode_id, dim);

	if (rel_idx < 0)
		return NULL;
	struct item_struct *item = rel_idx_to_item(rel_idx, pnode_id);
	return item;
}

static inline struct item_struct *idx_to_item_sparse(long idx, int dim)
{
	struct item_struct *temp;
	
	idx_to_rel_idx_sparse(idx, &temp, NULL, dim, MAX_GROUP_LVLS-1);
	return temp;

}

static inline range_base_t item_to_range(struct item_struct *item)
{
	return idx_to_range(item_to_idx(item));
}

static inline range_base_t item_to_range_sparse(struct item_struct *item)
{
	return idx_to_range(item_to_idx_sparse(item));
}


static inline struct item_struct *range_to_item(range_base_t range_base, int dim)
{
	return idx_to_item(range_to_idx(range_base), dim);
}

static inline struct item_struct *range_to_item_sparse(range_base_t range_base, int dim)
{
	return idx_to_item_sparse(range_to_idx(range_base), dim);
}


static inline struct node_struct *idx_to_pnode(long idx, int dim)
{
	int pnode_id;
	long rel_idx = idx_to_rel_idx(idx, &pnode_id, dim);

	if (rel_idx < 0)
		return NULL;
	return get_pnode_num(pnode_id);
}

static inline struct node_struct *idx_to_vnode(long idx, int dim)
{
	int vnode_id;

	long rel_idx = idx_to_rel_idx_sparse(idx, NULL, &vnode_id, dim, MAX_GROUP_LVLS-1);

	if (rel_idx < 0)
		return NULL;
	return get_vnode_num(vnode_id);
}

static inline void *range_base_transform(range_base_t x)
{
	return (void *)(range_transform_t)(x);
}

static inline range_base_t range_transform_base(const void *x)
{
	return (range_transform_t)(x);
}

static inline void *item_to_range_transform(struct item_struct *item)
{
	if (!item)
		return NULL;
	range_base_t range_base = item_to_range(item);
	void *range_transform = (void *)range_base_transform(range_base);

	return range_transform;
}

static inline void *item_to_range_transform_sparse(struct item_struct *item)
{
	if (!item)
		return NULL;

	range_base_t range_base = item_to_range_sparse(item);
	void *range_transform = (void *)range_base_transform(range_base);

	return range_transform;
}


static inline struct item_struct *range_transform_to_item(const void *range_transform, int dim)
{
	range_base_t range_base = range_transform_base(range_transform);
	struct item_struct *item = range_to_item(range_base, dim);
	return item;
}

static inline struct item_struct *range_transform_to_item_sparse(const void *range_transform, int dim)
{
	range_base_t range_base = range_transform_base(range_transform);
	struct item_struct *item = range_to_item_sparse(range_base, dim);
	return item;
}


static inline word_t item_to_order(word_t *nr_items)
{
	word_t order;
	word_t max_order;
	word_t max_bits = sizeof(word_t) * 8;
	word_t nitems = *nr_items;

	if (!nitems)
		return 0;

	max_order = fls(nitems);

	order = max_order - 1;
	*nr_items = 0;
	if (max_order == max_bits)
		return order;
	if (nitems > BIT(order))
		return max_order;
	return order;
}

static inline int item_to_orders(word_t *nr_items)
{
	int x = (int)fls(*nr_items);

	if (x)
		CB(*nr_items, x - 1);
	return x ? x - 1 : 0;
}

static inline word_t size_to_order(size_t size)
{
	unsigned long x = (unsigned long)((size - 1) >> FIXED_ITEM_SIZE_SHIFT);

	return fls(x);
}

static inline word_t size_to_item(size_t size)
{
	unsigned long x = (unsigned long)(size >> FIXED_ITEM_SIZE_SHIFT);
	unsigned long y =
		(unsigned long)(size << (sizeof(x) * 8 - FIXED_ITEM_SIZE_SHIFT));

	return y ? (x + 1) : x;
}

