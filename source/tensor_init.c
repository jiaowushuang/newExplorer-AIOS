#include "tensor_struct.h"
#include "tensor_ops.h"

uintptr_t phy_init_linear_ptr[PNODE_NUM];

static void *__init_linear_space(int node_id, size_t len)
{
	/* offset requires a preset interval:
	 * 1. memory: for each pnode(new one), reserving the algo struct
	 * 2. time: for each pnode(new one), reserving window align time
	 *
	 */

	uintptr_t ptr;
	
	if (!phy_init_linear_ptr[node_id])
		phy_init_linear_ptr[node_id]= NODE_RESERVED_RANGE_BASE;

	ptr = ROUND_UP(phy_init_linear_ptr[node_id], 8);
	phy_init_linear_ptr[node_id] = (ptr + ROUND_UP(len, 8));
	if (phy_init_linear_ptr[node_id] >= NODE_RESERVED_RANGE_SIZE)
		return NULL;
	
	return (void *)ptr;
}


static status_t __get_pnode_of(int type, int node_id, range_base_t *range, size_t *size)
{
	switch (type) {
		case DIM_MEMORY: /* dts of */
			*range = NODE_FIXED_RANGE_BASE;
			*size = NODE_FIXED_RANGE_SIZE;
			break;
		case DIM_TIME:
			*range = NODE_SUPER_PERIOD_WINODW_BASE;
			*size = NODE_SUPER_PERIOD_WINODW_SIZE;	
			break;
		default:
			printf("cannot get node with invaild dim type\n");
			break;
	}

	return NO_ERROR;
}

static status_t __get_vnode_of(int type, int node_id, range_base_t *range, size_t *size)
{
	switch (type) {
		case DIM_MEMORY: /* dts of */
			*range = VNODE_FIXED_RANGE_BASE;
			*size = VNODE_FIXED_RANGE_SIZE;
			break;
		case DIM_TIME:
			*range = VNODE_SUPER_PERIOD_WINODW_BASE;
			*size = VNODE_SUPER_PERIOD_WINODW_SIZE;	
			break;
		default:
			printf("cannot get node with invaild dim type\n");
			break;
	}

	return NO_ERROR;
}

void __init_slice(struct slice_struct *slice, size_t bsize, unsigned int th) 
{
	init_list_node(&slice->empty.item_list);
	slice->empty.item_count = 0;
	init_list_node(&slice->full.item_list);
	slice->full.item_count = 0;
	init_list_node(&slice->free.item_list);
	slice->free.item_count = 0;
	slice->bsize = bsize;
	slice->threshold_freeitems = th;
}

static void __init_free_group_list(struct group_struct *group)
{
	word_t order;

	for (word_t index = 0; index < MIGRATION_TYPES; index++) {
		for (order = 0; order < MAX_IDX_NUM; order++) {
			init_list_node(&group->free_group[index].free_cell[order]);
			group->free_group[index].free_count[order] = 0;
		}
	}
}

static void __init_free_node_list(struct node_struct *pnode)
{
	word_t node_id;

	for (node_id = 0; node_id < NODE_LVL_NUM; node_id++) {
		init_list_node(&pnode->lru_cache.free_cell[node_id]);
		pnode->lru_cache.free_count[node_id] = 0;
		//init_list_node(&pnode->node_map.free_cell[node_id]);
		//pnode->node_map.free_count[node_id] = 0;	
	}
}
static void __init_group_attr(struct group_attr *attr, range_base_t offset, range_base_t size,
		      word_t nitems, word_t status)
{
	attr->offset = offset;
	attr->size = size;
	attr->nr_freeitems = nitems;
	attr->status = status;
}

void init_item(struct item_struct *item)
{
      	init_list_node(&item->node);
      	item->flags = 0;
      	item->word1[0] = 0;
      	item->word1[1] = 0;
      	item->word2[0] = 0;
	item->word2[1] = 0;
	item->word2[2] = 0;
}

void __init_item_pernode(struct node_struct *self, size_t size)
{
	void *item_ptr;

	if (!size)
		return;

	int pn_size_shift = fls(size) - 1;
	int pn_item = BIT(pn_size_shift - GROUP_ORDER_NUM_SHIFT);
	size_t item_size = sizeof(struct item_struct) * pn_item;

	int pid = self->node_id;
	
	item_ptr = __init_linear_space(pid, item_size);

	
	if (!phy_item_struct[pid] )
		phy_item_struct[pid] = (struct item_struct *)item_ptr;
	if (!phy_item_struct_of[pid])
		phy_item_struct_of[pid] = (void *)ROUND_UP(
			NODE_RESERVED_RANGE_SIZE+NODE_RESERVED_RANGE_BASE,
			FIXED_ITEM_SIZE);
	phy_item_struct_end[pid] = item_ptr + item_size;
	
	int index = item_to_rel_idx(item_ptr, pid);
	for (int cnt = 0; cnt < pn_item; index++, cnt++) {
		init_item(&phy_item_struct[pid][index]);
	}

}

void __init_node(struct node_struct *self, range_base_t range, size_t size, bool is_virtual)
{
	word_t node_x_nitem = size / FIXED_ITEM_SIZE;

	self->node_id = is_virtual ? get_num_vnode(self): get_num_pnode(self);
	spin_lock_init(&self->node_struct_lock);
	init_list_node(&self->node);
	self->group_vtbl = &g_group_struct_vtbl;
	self->vtbl = &g_node_vtbl;
	self->sequeue_stream_id = NODE_RESERVED_RANGE_SIZE / FIXED_ITEM_SIZE;
	
	if (is_virtual) {
		__init_slice(&self->sparse_itemarray, 
			sizeof(struct item_struct) * BIT(MAX_ITEM_RANGE+1), 64);
		__init_slice(&self->sparse_grouparray, 
			sizeof(struct group_struct) * GROUP_LVL_NUM, 64);
	}

	__init_free_node_list(self);
	__init_group_attr(&self->attr, range, size, node_x_nitem, GROUP_IDLE);
}

void __init_group_pernode(struct node_struct *self, struct group_struct *group, size_t size, 
			bool is_virtual)
{
	word_t node_x_nitem = size / FIXED_ITEM_SIZE;
	
	int pid = self->node_id;
	if (!is_virtual) {
		phy_group_lvl[pid] = nitems_to_lvl(&node_x_nitem, false);
		if (phy_group_lvl[pid] < 0)
			printf("No INIT group!\n");
	}

	for (int lvl = 0; lvl < GROUP_LVL_NUM; lvl++) {
		spin_lock_init(&group[lvl].group_struct_lock);
		group[lvl].node = self;
		group[lvl].group_id = 0;
		__init_free_group_list(&group[lvl]);
		group[lvl].vtbl = &g_group_struct_vtbl;

	}
	self->group = group;
}

void __init_pnode_item(struct node_struct *self, size_t base_size, bool reserved)
{
	int pid = self->node_id;
	range_base_t res;
	word_t rem = 0;
	int lvl = phy_group_lvl[pid];

	if (reserved)
		res = range_transform_base(phy_item_struct_of[pid]) - self->attr.offset;

	int rem_pa_offset = rem / FIXED_ITEM_SIZE;
	int rel_pa_offset = base_size / FIXED_ITEM_SIZE;

	int block_shift;
	int block_size;
	int max_block_free;
	struct item_struct *item;
	struct group_struct *group;
	struct item_struct *item_start;

#ifndef DOWN_GROW
	item_start = phy_item_struct_end[pid];
#else
	int pn_size_shift = fls(base_size) - 1;
	int pn_item = BIT(pn_size_shift - GROUP_ORDER_NUM_SHIFT);

	item_start = (phy_item_struct_end- pn_item)[pid];
#endif

	long reserved_idx_end = -1;
	long reserved_idx_start = reserved_idx_end;

#ifdef RESERVED_MEMBASE
	reserved_idx_start = range_to_idx(RESERVED_MEMBASE);
	reserved_idx_end = reserved_idx_start + range_to_idx(RESERVED_MEMSIZE);
#endif

	rel_pa_offset -= rem_pa_offset;
	for (; lvl < GROUP_LVL_NUM; ++lvl) {
		int order_index;
		long idx;

		block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM_SHIFT, lvl);
		block_size = BIT(block_shift);
		if (block_size > rel_pa_offset)
			continue;
		idx = range_to_idx(self->attr.offset);
		if (idx & (block_size - 1)) {
			phy_group_lvl[pid]++;
			continue;
		}

		for (order_index = MAX_ITEM_ORDER; order_index >= 0;
		     order_index--) {
			max_block_free =
				rel_pa_offset / (block_size * BIT(order_index));
			if (!max_block_free)
				continue;

			if (lvl == phy_group_lvl[pid] && !phy_group_order[pid])
				phy_group_order[pid] =
					MIN(order_index + 1, GROUP_ORDER_NUM_SHIFT);

			group = get_group_lvl(lvl, pid);
			for (int i = max_block_free; i > 0; --i) {
#ifndef DOWN_GROW
				item = item_start -
				     i * block_size * BIT(order_index);
				set_item_order(item, order_index);
				set_item_node(item, pid);
				idx = item_to_idx(item);
				assert_msg(
					!(idx & (block_size - 1)),
					"[idx %lx, block_size %lx, lvl %d, pid %d, order_index %d, item %p, g_pa %p]\n",
					idx, block_size, lvl, pid, order_index,
					item, phy_item_struct);
				if (idx >= reserved_idx_start &&
				    idx < reserved_idx_end) {
					// coverity[dead_error_begin:SUPPRESS]
					list_add_tail(&item->node,
						    &GROUP_CELL(
							    group,
							    MIGRATION_UNMOVABLE)
							    [order_index]);
					GROUP_CELLCOUNT(group, MIGRATION_UNMOVABLE)
					[order_index]++;
				} else {
					list_add_tail(&item->node,
						    &GROUP_CELL(
							    group,
							    MIGRATION_MOVABLE)
							    [order_index]);
					GROUP_CELLCOUNT(group, MIGRATION_MOVABLE)
					[order_index]++;
				}

#else
				item = item_start +
				     (i - 1) * block_size * BIT(order_index);
				set_item_order(item, order_index);
				set_item_node(item, pid);
				idx = item_to_idx(item);
				assert_msg(
					!(idx & (block_size - 1)),
					"[idx %lx, block_size %lx, lvl %d, pid %d, order_index %d, item %p, g_pa %p]\n",
					idx, block_size, lvl, pid, order_index,
					item, phy_item_struct);
				if (idx >= reserved_idx_start &&
				    idx < reserved_idx_end) {
					list_add_tail(&item->node,
						    &GROUP_CELL(
							    group,
							    MIGRATION_UNMOVABLE)
							    [order_index]);
					GROUP_CELLCOUNT(group, MIGRATION_UNMOVABLE)
					[order_index]++;
				} else {
					list_add_tail(&item->node,
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
			item_start = item;
#else
			item_start = item + max_block_free * block_size *
					       BIT(order_index);
#endif
		}

		if (!rel_pa_offset)
			break;
	}

	printf("rel nitems is %d-%ld, g_lvl-g_order is %d-%d\n",
		      rel_pa_offset, self->attr.nr_freeitems, phy_group_lvl[pid],
		      phy_group_order[pid]);

}

void _init_pnode(struct node_struct *self, range_base_t range, size_t size)
{
	__init_node(self, range, size, false);
	__init_item_pernode(self, size);
	__init_group_pernode(self, phy_group_struct[self->node_id], 
			size, false);
	__init_pnode_item(self, size, true);
}

// init pnode
status_t init_pnode(struct node_struct *self, range_base_t range, size_t size)
{
	_init_pnode(self, range, size);
	
	return NO_ERROR;
}

// You need to ensure that the item struct address is continuous
status_t update_pnode(struct node_struct *self, size_t size)
{
	for (int lvl = 0; lvl < GROUP_LVL_NUM; lvl++) 
		self->group[lvl].group_id++; // per-update, group_id ++
	
	__init_item_pernode(self, size);
	__init_pnode_item(self, size, false);
	self->attr.nr_freeitems += size / FIXED_ITEM_SIZE;
	self->attr.status = 0;
	self->attr.size += size;
	
	return NO_ERROR;	
}

// init vnode
status_t init_vnode(struct node_struct *self, range_base_t range, size_t size)
{
	__init_node(self, range, size, true);
	
	struct slice_struct *slice = NODE_SLICE_STRUCT_GROUP(self);
	range_base_t range_base;
	status_t ret = self->vtbl->slice_alloc(slice, &range_base);
	
	if (ret)
		return NULL;	
	
	struct group_struct *group = range_base_transform(range_base);
	__init_group_pernode(self, group, true);
	
	return NO_ERROR;
}

status_t update_vnode(struct node_struct *self, size_t size)
{
	for (int lvl = 0; lvl < GROUP_LVL_NUM; lvl++) 
		self->group[lvl].group_id++; // per-update, group_id ++
	
	self->attr.nr_freeitems += size / FIXED_ITEM_SIZE;
	self->attr.status = 0;
	self->attr.size += size;
	
	return NO_ERROR;
}

status_t init_entity_node(struct node_struct *self)
{
	struct group_struct *group = entity_group_struct;

	self->group = group;
	for (int i = 0; i < GROUP_LVL_NUM; i++) {
		spin_lock_init(&group[i].group_struct_lock);		
		__init_free_group_list(&group[i]);
		group[i].node = self;
	}
	__init_slice(&self->sparse_itemarray, 
			sizeof(struct item_struct) * BIT(MAX_ITEM_RANGE+1), 64);
	for (int i = 0; i < PNODE_NUM; i++) {
		list_add_tail(&GROUP_CELL(group, MIGRATION_MOVABLE)[0], &phy_node_struct[i].node);
		GROUP_CELLCOUNT(group, MIGRATION_MOVABLE)[0]++;
	} // (group0, migrate_type==MIGRATION_MOVABLE, order==0)
	
	return NO_ERROR;
}

// how ? time -> node_struct.group_attr./size : 无限/有限
// how ? space -> node_struct.group_attr./size : 有限的物理内存/置换内存以及热插拔
// alloc once, and need update -> stream + window
// such as DIM_MEMORY and DIM_TIME
// infinite - void config - super_period window
// dts -static config
// hotplug -dynamic config
static status_t stream_window(int node_id, bool is_virtual, int dim)
{
	spin_lock_saved_state_t sflags;
	struct node_struct *node;
	status_t ret;
	range_base_t base;
	size_t size;

	
	if (is_virtual) { // little usage
		__get_pnode_of(dim, node_id, &base, &size);
		node = get_vnode_num(node_id);
		spin_lock_irqsave(&node->node_struct_lock, sflags);
		ret = update_vnode(node, size);	
		spin_unlock_irqrestore(&node->node_struct_lock, sflags);

	} else { // 
		__get_pnode_of(dim, node_id, &base, &size);
		node = get_pnode_num(node_id);
		spin_lock_irqsave(&node->node_struct_lock, sflags);
		ret = update_pnode(node, size);	
		spin_unlock_irqrestore(&node->node_struct_lock, sflags);
	}
	
	return ret;
}
// -------------------------------------------------------------------
void init_node(void)
{
	struct node_struct *node;
	range_base_t range;
	size_t size;
	int node_id = 0;

	for (int i = 0; i < NODE_DIM_NUM; i++) {
		for (int j = 0; j < NODE_LVL_NUM; j++) {
			node_id = j | (i << NODE_LVL_NUM);
			node = get_pnode_num(node_id);
			__get_pnode_of(i, node_id, &range, &size);
			init_pnode(node, range, size);
			node->attr.dim = i;
		}
	}

	for (int i = 0; i < NODE_DIM_NUM; i++) {
		for (int j = 0; j < NODE_LVL_NUM; j++) {
			node_id = j | (i << NODE_LVL_NUM);
			node = get_vnode_num(node_id);
			__get_vnode_of(i, node_id, &range, &size);
			init_vnode(node, range, size);
			node->attr.dim = i;
		}
	}	
	init_entity_node(&entity_node_struct);
}

int get_current_node_num(int dim_type)
{
	switch (dim_type) {
	case DIM_TIME:
		return 0;
	case DIM_MEMORY:
		return NODE_LVL_NUM;
	default:
		printf("");
		break;
	}
}

int get_current_vnode_num(int dim_type)
{
	switch (dim_type) {
	case DIM_TIME:
		return 0;
	case DIM_MEMORY:
		return NODE_LVL_NUM;
	default:
		printf("");
		break;
	}
}

int arch_curr_cpu_num(void)
{
	return 0;
}

struct item_struct *get_current_running_task(void)
{
	return NULL;
}

struct item_struct *get_current_create_task(void)
{
	return NULL;
}



struct process_group_struct *get_current_process_group(void)
{
	return NULL;
}

static inline ssize_t get_current_time(void)
{
}

static inline bool get_time_node_by_node(struct node_struct *self)
{
	return get_pnode_num(self->node_id & MASK(NODE_LVL_NUM));
}
struct node_struct *get_entity_node(void)
{
	return &entity_node_struct;
}

