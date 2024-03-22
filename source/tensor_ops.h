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
	case STATE_ITEM_PCP_TYPES:
		migration_type = MIGRATION_PCPTYPE;
		break;
	case STATE_ITEM_NOMOVABLE_TYPES:
		migration_type = MIGRATION_UNMOVABLE;
		break;
	case STATE_ITEM_RECLAIM_TYPES:
		migration_type = MIGRATION_RECLAIMABLE;
		break;
	default:
		break;
	}

	return migration_type;
}

static inline void prepare_node_struct_alloc(word_t nitems, int flags,
						int *magration_type,
						bool *is_combined)
{
	/* The fixed item corresponding to the reserved item must be
	 * used up during the boot period, and the fixed item
	 * (debugging item) at runtime is allocated by the NORMAL item,
	 * so its number is limited to UNMOV_MAX_ITEMS, and each allocation
	 * must be aligned according to the order
	 */
	if (flags == FUNC_DEBUG) {
		*magration_type = MIGRATION_UNMOVABLE;
		*is_combined = false;
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
		*is_combined = true;
		return;
	}

	if (nitems == 1) {
		*magration_type = MIGRATION_PCPTYPE;
		*is_combined = false;
		return;
	}

	switch (flags) {
	case FUNC_SYSCALL_DCONT:
		*magration_type = MIGRATION_MOVABLE;
		*is_combined = true;
		break;
	case FUNC_SYSCALL_CONT:
	case FUNC_MMU:
	case FUNC_OBJECT:
		/* For the case of cross-lvl,
		 * the number of item needs to be aligned into one lvl
		 */
		*magration_type = MIGRATION_MOVABLE;
		*is_combined = false;
		break;
	case FUNC_UNDEFINED:
	default:
		printf("NO defined flags");
	}
}

// item function
static inline bool compound_item(struct item_struct *item)
{
	return item->compound_head;
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

static inline void set_item_in_state(struct item_struct *item, word_t val)
{
	MR(item->flags, BASE_ITEM_IN_STATE, OFFSET_ITEM_IN_STATE, val);
}
static inline void clear_item_in_state(struct item_struct *item, word_t val)
{
	CR(item->flags, BASE_ITEM_IN_STATE, OFFSET_ITEM_IN_STATE, val);
}

static inline word_t get_item_in_state(struct item_struct *item)
{
	return GR(item->flags, BASE_ITEM_IN_STATE, OFFSET_ITEM_IN_STATE);
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
	is_order_equal = (order == get_item_order(buddy));

	return !is_buddy_present && is_order_equal;
}

// conversion

static inline word_t range_to_idx(range_base_t range_base)
{
	return range_base >> GROUP_ORDER_NUM;
}

static inline range_base_t idx_to_range(word_t idx)
{
	return idx << GROUP_ORDER_NUM;
}

static inline int idx_to_lvl(long idx, int lvl_index)
{
	int lvl = -1;
	word_t block_shift, block_size, block_mask, lvl_idx;

	if (!idx) {
		lvl = g_group_lvl[get_recent_node_num()];
		goto zero_idx;
	}

	for (lvl = lvl_index; lvl >= 0; --lvl) {
		block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, lvl);
		block_size = BIT(block_shift + FUNCTION_ITEM(GROUP_ORDER_NUM));
		block_mask = block_size - 1;
		lvl_idx = idx >> block_shift;
		idx &= !block_mask;
		if (lvl_idx)
			break;
	}

zero_idx:
	return lvl;
}

static inline int nitems_to_lvl(word_t *nitems, bool align)
{
	int block_shift;
	word_t block_mask;
	word_t block_size;
	word_t lvl_nitem;

	for (int level = 0; level < MAX_GROUP_LVLS; ++level) {
		block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, level);
		block_size = BIT(block_shift);
		block_mask = block_size - 1;
		lvl_nitem = (*nitems) >> block_shift;
		if (!lvl_nitem)
			continue;
		if (align)
			*nitems &= !block_mask;
		return level;
	}
	return -1;
}




static inline struct node_struct *get_pnode_num(int nu)
{
	return &g_node_struct[nu];
}

static inline struct node_struct *get_recent_pnode(void)
{
	return get_pnode_num(get_recent_node_num());
}

static inline int get_num_pnode(struct node_struct *pnode)
{
	return pnode - g_node_struct;
}


static inline struct group_struct *get_group_lvl(int lvl, int nu)
{
	return &g_group_struct[nu][lvl];
}

static inline int get_lvl_group(struct group_struct *group, int nu)
{
	return (group - &g_group_struct[0][0]) - nu * GROUP_LVL_NUM;
}


static inline long idx_to_rel_idx(long idx, int *pnode_id)
{
	struct node_struct *pnode;
	long rel_idx;


	for (int i = 0; i < PNODE_NUM; i++) {
		pnode = get_pnode_num(i);
		if (idx >= range_to_idx(pnode->attr.addr) &&
		    idx < range_to_idx(pnode->attr.addr + pnode->attr.size)) {
			if (pnode_id)
				*pnode_id = i;
			rel_idx = idx - range_to_idx(pnode->attr.addr);
			return rel_idx;
		}
	}

	return -1;
}

static inline long item_to_rel_idx(struct item_struct *item, int pnode_id)
{
	long rel_idx;

	rel_idx = (long)(item - g_item_struct[pnode_id]);
	return rel_idx;
}

static inline struct item_struct *rel_idx_to_item(long rel_idx, int pnode_id)
{
	return &g_item_struct[pnode_id][rel_idx];
}

static inline long item_to_idx(struct item_struct *item)
{
	long idx;
	int pnode_id = get_item_node(item);

	if (pnode_id >= PNODE_NUM)
		pnode_id = 0;

	struct node_struct *pnode = get_pnode_num(pnode_id);

	idx = item_to_rel_idx(item, pnode_id) + range_to_idx(pnode->attr.addr);
	return idx;
}

static inline struct item_struct *idx_to_item(long idx)
{
	int pnode_id;
	long rel_idx = idx_to_rel_idx(idx, &pnode_id);

	if (rel_idx < 0)
		return NULL;
	struct item_struct *item = rel_idx_to_item(rel_idx, pnode_id);
	return item;
}

static inline range_base_t item_to_range(struct item_struct *item)
{
	return idx_to_range(item_to_idx(item));
}

static inline struct item_struct *range_to_item(range_base_t range_base)
{
	return idx_to_item(range_to_idx(range_base));
}

static inline struct node_struct *idx_to_node(long idx)
{
	int pnode_id;
	long rel_idx = idx_to_rel_idx(idx, &pnode_id);

	if (rel_idx < 0)
		return NULL;
	return get_pnode_num(pnode_id);
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
	range_base_t range_base = item_to_range(item);
	void *range_transform = (void *)range_base_transform(range_base);

	return range_transform;
}

static inline struct item_struct *range_transform_to_item(const void *range_transform)
{
	range_base_t range_base = range_transform_base(range_transform);
	struct item_struct *item = range_to_item(range_base);
	return item;
}

static inline bool idx_is_valid(long idx)
{
	return idx >= 0 && idx < NODE_FIXED_RANGE_SIZE / FIXED_ITEM_SIZE;
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

