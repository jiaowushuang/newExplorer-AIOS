#include "tensor_struct.h"
#include "tensor_ops.h"


struct group_struct g_group_struct[PNODE_NUM][GROUP_LVL_NUM];
struct node_struct g_node_struct[PNODE_NUM];
struct item_struct *g_item_struct[PNODE_NUM];
struct item_struct *g_item_struct_end[PNODE_NUM];
void *g_item_struct_of[PNODE_NUM];
int g_group_lvl[PNODE_NUM];
int g_group_order[PNODE_NUM];


static status_t group_struct_compound(struct item_struct **item_pptr, word_t nitems,
					int migration_type, bool is_combined);
static status_t group_struct_decompose(struct item_struct *item_pptr);
static status_t group_struct_dump_group(struct group_struct *const self);
static status_t node_struct_migrate(struct node_struct *const self,
				       struct item_struct **item_pptr, int order, int lvl,
				       int type, bool is_combined);
static status_t node_struct_dispatch(struct node_struct *const self,
					struct item_struct **item_pptr, int lvl,
					int migration_type, int order,
					bool is_combined);
static status_t node_struct_init(struct node_struct *const self);
static status_t node_struct_lend(struct item_struct **item_pptr, int migration_type,
				    int lvl, int lvl_order, bool is_combined);
static status_t node_struct_alloc(struct node_struct *pnode, range_base_t *range_base,
				     word_t nitems, int flags);
static status_t node_struct_free(range_base_t range_base);
static status_t node_struct_dump_node(struct node_struct *const self);
static void group_struct_dump_migration(struct group_struct *const self,
					  int type);

struct group_struct_vtable g_group_struct_vtbl = {
	.compound = group_struct_compound,
	.decompose = group_struct_decompose,
	.dump_area = group_struct_dump_group,
};

struct node_struct_vtable g_node_vtbl = {
	.migrate = node_struct_migrate,
	.dispatch = node_struct_dispatch,
	.init = node_struct_init,
	.lend = node_struct_lend,
	.alloc = node_struct_alloc,
	.free = node_struct_free,
	.dump_node = node_struct_dump_node,
};

/*
 * Locate the struct item for both the matching buddy in our
 * pair (buddy1) and the combined O(n+1) item they form (item).
 *
 * 1) Any buddy B1 will have an order O twin B2 which satisfies
 * the following equation:
 *     B2 = B1 ^ (1 << O)
 * For example, if the starting buddy (buddy2) is #8 its order
 * 1 buddy is #10:
 *     B2 = 8 ^ (1 << 1) = 8 ^ 2 = 10
 *
 * 2) Any buddy B will have an order O+1 parent P which
 * satisfies the following equation:
 *     P = B & ~(1 << O)
 *
 * Assumption: *item_struct is contiguous at least up to MAX_ORDER
 */
/* relative order */
void split2_group_migration(struct group_struct *group, struct item_struct *item,
			   int size, int order_max, int migration_type, int lvl)
{
	struct item_struct *present;
	struct list_node *temp;

	present = &item[size];
	temp = &GROUP_CELL(group, migration_type)[order_max];

	list_add_head(&present->node, temp);
	clear_item_present(present);
	set_item_order(present, order_max);
	clear_item_state(present);
	set_item_node(present, get_recent_node_num());
	set_item_section(present, lvl);
	GROUP_CELLCOUNT(group, migration_type)[order_max]++;
	/* according to range_base, can find which node or area */
}

static void split_group_migration(struct group_struct *group,
				 struct item_struct *item, int lvl, int order_min,
				 int order_max, int migration_type)
{
	int block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, lvl);

	while (order_max > order_min) {
		order_max--; /* size >>= 1; */
		split2_group_migration(group, item, BIT(order_max + block_shift),
				      order_max, migration_type, lvl);
	}

	set_item_present(item);
	set_item_order(item, order_max);
	set_item_node(item, get_recent_node_num());
	set_item_section(item, lvl);
}

struct item_struct *remove_group_migration(struct group_struct *group, int lvl,
				      int order, int migration_type)
{
	struct item_struct *item;
	word_t order_index;
	struct list_node *temp;
	int block_shift;
	int max_order;
	long item_idx = -1;

	max_order = (lvl == g_group_lvl[get_recent_node_num()]) ?
			    g_group_order[get_recent_node_num()] :
			    GROUP_ORDER_NUM;

	for (order_index = order; order_index < max_order; order_index++) {
		temp = &GROUP_CELL(group, migration_type)[order_index];
		item = list_head_entry(temp, item, node);

		if (!item)
			continue;

		item_idx = item_to_idx(item);
		/* idx align */
		block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, lvl);
		if (item_idx & MASK(block_shift))
			goto bad_align;

		assert(!item_is_present(item));
		list_del(&item->node);
		GROUP_CELLCOUNT(group, migration_type)[order_index]--;
		split_group_migration(group, item, lvl, order, order_index,
				     migration_type);

		return item;
	}

bad_align: // means the current lvl is not prepared
	printf("item %d order %d bad align by level %d of migration_type %d of nitems %d!\n",
		item_idx, order, lvl, migration_type,
		node_budget(migration_type));

	return NULL;
}

static inline struct item_struct *find_index_item(struct group_struct *group,
					      int order, int index,
					      int migration_type)
{
	struct item_struct *item;
	struct list_node *temp;
	int i = 0;

	temp = &GROUP_CELL(group, migration_type)[order];
	// coverity[dead_error_line:SUPPRESS]
	list_for_each_entry_list(temp, item, node) {
		if (i == index)
			return item;
		++i;
	}
	return NULL;
}

static inline void compound_append(struct item_struct *node, struct item_struct *head)
{
	struct item_struct *temp;

	temp = head->compound_head;
	if (!temp) {
		head->compound_head = node;
		return;
	}
	while (temp->compound_node)
		temp = temp->compound_node;
	temp->compound_node = node;
}

static inline struct item_struct *compound_remove_head(struct item_struct *head)
{
	struct item_struct *temp;

	temp = head->compound_head;
	if (!temp)
		return NULL;
	head->compound_head = temp->compound_node;
	temp->compound_node = NULL;

	return temp;
}

static inline void cell_to_compound(struct list_node *cell,
					struct item_struct *split,
					struct item_struct *head)
{
	struct item_struct *temp;
	struct list_node *head_list;

	// split != head of cell
	while (!list_is_empty(cell)) {
		head_list = list_remove_head(cell);
		temp = list_entry_list(head_list, temp, node);
		if (split && list_next_entry(cell, temp, node) == split) {
			compound_append(temp, head);
			break;
		}
		compound_append(temp, head);
	}
}

int enqueue_group_migration(struct group_struct *group, struct item_struct **item_pptr,
			   int order, int norder, int migration_type)
{
	struct item_struct *item, *split_page;
	struct item_struct *head = *item_pptr;
	struct list_node *temp;
	word_t order_index, temp_count, remain_count = 2 * norder;

	for (order_index = order - 1;; order_index--) {
		temp = &GROUP_CELL(group, migration_type)[order_index];
		temp_count = GROUP_CELLCOUNT(group, migration_type)[order_index];
		item = list_head_entry(temp, item, node);

		if (!item)
			continue;

		if (!head)
			head = item;

		if (remain_count > temp_count) {
			remain_count -= temp_count;
			remain_count *= 2;
			cell_to_compound(temp, NULL, head);
			GROUP_CELLCOUNT(group, migration_type)
			[order_index] -= temp_count;
			continue;
		} else {
			// remain_count != 0
			split_page = find_index_item(group, order_index,
						     remain_count, migration_type);
			cell_to_compound(temp, split_page, head);
			GROUP_CELLCOUNT(group, migration_type)
			[order_index] -= remain_count;
		}

		*item_pptr = head;
		return 0;
	}

	return remain_count;
}

static inline bool buddy_is_valid(int migration_type, long buddy_idx,
				  struct item_struct *buddy_page)
{
	bool valid = idx_is_valid(buddy_idx);
	bool in_list = list_in_list(&buddy_page->node);
	int state = get_item_state(buddy_page);
	int buddy_migration_type = item_state_to_migration(state);

	return valid && in_list && buddy_migration_type == migration_type;
}

struct item_struct *free_group_migration(struct item_struct *item,
				    struct group_struct *group, int lvl,
				    int order, int migration_type)
{
	long combined_idx;
	long buddy_idx;
	struct list_node *temp;
	struct item_struct *buddy_page;
	word_t max_order;
	long idx;
	int pid = get_item_node(item);
	int old_lvl = -1;
	int block_shift, abs_order;

	assert(item_is_present(item));

	block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, lvl);
	abs_order = order + block_shift;

	while (old_lvl != lvl) {
		old_lvl = lvl;
		idx = item_to_rel_idx(item, pid);

		max_order = (lvl == g_group_lvl[get_recent_node_num()]) ?
				    g_group_order[get_recent_node_num()] :
				    GROUP_ORDER_NUM;

		while (order <= max_order - 1) {
			clear_item_present(item);
			// FOR buddy_is_valid
			// clear_item_state(item);

			if (migration_type == MIGRATION_PCPTYPE ||
			    (lvl == g_group_lvl && order == max_order - 1))
				goto free_item_migration_direct;

			buddy_idx = next_buddy_idx(idx, abs_order);
			buddy_page = item + (buddy_idx - idx);
			if (!buddy_is_valid(migration_type, buddy_idx,
					    buddy_page) ||
			    !item_is_buddy(buddy_page, order))
				goto free_item_migration_done;

			list_del(&buddy_page->node);
			clear_item_order(buddy_page);
			clear_item_node(buddy_page);
			GROUP_CELLCOUNT(group, migration_type)[order]--;
			combined_idx = buddy_idx & idx;
			item = item + (combined_idx - idx);
			idx = combined_idx;
			order++;
			abs_order++;
			set_item_order(item, order);
		}

free_item_migration_done:
		if ((order < max_order - 2) &&
		    buddy_is_valid(migration_type, buddy_idx, buddy_page)) {
			struct item_struct *higher_page;
			struct item_struct *higher_buddy;

			combined_idx = buddy_idx & idx;
			higher_page = item + (combined_idx - idx);
			buddy_idx = next_buddy_idx(combined_idx, abs_order + 1);
			higher_buddy = higher_page + (buddy_idx - combined_idx);
			if (idx_is_valid(buddy_idx) &&
			    item_is_buddy(higher_buddy, order + 1)) {
				temp = &GROUP_CELL(group,
						      migration_type)[order];
				list_add_tail(&item->node, temp);
				goto free_item_migration_out;
			}
		}

		if (order >= GROUP_ORDER_NUM) {
			order = 0;
			group = get_group_lvl(--lvl, pid);
			continue;
		}

free_item_migration_direct:

		temp = &GROUP_CELL(group, migration_type)[order];
		list_add_head(&item->node, temp);

free_item_migration_out:
		set_item_order(item, order);
		set_item_section(item, lvl);
		GROUP_CELLCOUNT(group, migration_type)[order]++;
	}

	return item;
}

void dequeue_group_migration(struct item_struct *item, int migration_type)
{
	int order_index, lvl_index;
	struct item_struct *temp_page;
	int pnode_id;
	word_t nitems = 0;
	int block_shift;
	struct node_struct *pnode;

	do {
		temp_page = (struct item_struct *)compound_remove_head(item);
		if (!temp_page)
			temp_page = item;

		pnode_id = get_item_node(temp_page);

		if (pnode_id >= PNODE_NUM)
			pnode_id = 0;

		pnode = get_pnode_num(pnode_id);
		order_index = get_item_order(temp_page);
		lvl_index = get_item_section(temp_page);
		if (lvl_index > MAX_GROUP_LVLS)
			lvl_index = MAX_GROUP_LVLS;
		free_group_migration(temp_page, get_group_lvl(lvl_index, pnode_id),
				    lvl_index, order_index, migration_type);
		block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, lvl_index);
		nitems = BIT(order_index + block_shift);
		if (migration_type != MIGRATION_UNMOVABLE)
			pnode->attr.nr_freeitems += nitems;
	} while (compound_item(item));
}

void split_item_migration(struct group_struct *group, int lvl, int order,
			  struct item_struct *item, int migration_type)
{
	int block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, lvl);

	split2_group_migration(group, item, BIT(order - 1 + block_shift),
			      order - 1, migration_type, lvl);
	split2_group_migration(group, item, 0, order - 1, migration_type, lvl);
}

word_t group_budget(struct group_struct *group, int lvl, int migration_type,
		   int order, int order_end)
{
	int order_cnt = 0;
	word_t nitems = 0;
	int nitem_base;

	nitem_base = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, lvl);
	for (int index = order; index <= order_end; ++index) {
		order_cnt = GROUP_CELLCOUNT(group, migration_type)[index];
		nitems += order_cnt * BIT(index) * BIT(nitem_base);
	}
	return nitems;
}

word_t node_budget_lvl(int migration_type, int lvl)
{
	word_t budget = 0;

	budget += group_budget(get_group_lvl(lvl, get_recent_node_num()), lvl,
			      migration_type, 0, MAX_ITEM_ORDER);

	return budget;
}

word_t node_budget(int migration_type)
{
	word_t budget = 0;

	for (int i = g_group_lvl; i < MAX_GROUP_LVLS; i++)
		budget += node_budget_lvl(migration_type, i);

	return budget;
}

/* In the memory allocator, for each node, there is a continuous space without
 * holes and the node has a start address, so relative item descriptors can be
 * applied to each item of the node, that is, the item descriptor of each node
 * is idx from 0 to the maximum item frame number, and when the absolute item
 * frame number needs to be used, it needs to be calculated in combination
 * with the start address of the node; the memory allocator is implemented
 * using the hierarchical Buddy algorithm, which divides all item of a node
 * into different groups, each group having the same order range, using the
 * aligned mapping address as the boundary
 */

static inline int forward_group_budget(int reverse_index, int migration_type,
				      int order)
{
	int order_index = order;

	while (reverse_index >= g_group_lvl) {
		if (group_budget(get_group_lvl(reverse_index, get_recent_node_num()),
				reverse_index, migration_type, order_index,
				MAX_ITEM_ORDER)) {
			break;
		}
		--reverse_index;
		order_index = 0;
	}

	return reverse_index;
}

static inline int backward_group_budget(int index, int migration_type, int order)
{
	int order_index = order;
	int order_nitem = 0;
	int nitem_need = BIT(order);

	while (index < GROUP_LVL_NUM) {
		order_nitem =
			group_budget(get_group_lvl(index, get_recent_node_num()), index,
				    migration_type, 0, order_index - 1);
		if (order_nitem >= nitem_need)
			break;
		nitem_need -= order_nitem;
		++index;
		order_index = GROUP_ORDER_NUM;
		nitem_need <<= FUNCTION_ITEM(GROUP_ORDER_NUM);
	}
	return index;
}

struct item_struct *forward_remove_group_migration(int reverse_index,
					      int migration_type, int order)
{
	int last_index;
	struct item_struct *last_item = NULL;

	last_index = forward_group_budget(reverse_index, migration_type, order);
	if (last_index < g_group_lvl)
		goto bad_budget;
	while (last_index != reverse_index) {
		last_item = remove_group_migration(
			get_group_lvl(last_index, get_recent_node_num()), last_index, 0,
			migration_type);
		assert(last_item);
		split_item_migration(get_group_lvl(last_index + 1,
						 get_recent_node_num()),
				     last_index + 1, GROUP_ORDER_NUM,
				     last_item, migration_type);
		++last_index;
	}
	last_item =
		remove_group_migration(get_group_lvl(last_index, get_recent_node_num()),
				      last_index, order, migration_type);

bad_budget:

	return last_item;
}

struct item_struct *backward_remove_group_migration(int index, int migration_type,
					       int order)
{
	int last_index, order_index = order, norder = 1;
	struct item_struct *last_item = NULL;

	last_index = backward_group_budget(index, migration_type, order);
	if (last_index == GROUP_LVL_NUM)
		goto bad_budget;
	while (last_index != index) {
		norder = enqueue_group_migration(
			get_group_lvl(index, get_recent_node_num()), &last_item,
			order_index, norder, migration_type);
		order_index = GROUP_ORDER_NUM;
		++index;
		// norder != 0
	}
	norder = enqueue_group_migration(get_group_lvl(index, get_recent_node_num()),
					&last_item, order_index, norder,
					migration_type);
	// norder == 0
bad_budget:
	return last_item;
}

status_t dispatch_group_migration(struct node_struct *pnode, struct item_struct **item_pptr,
				int order, int lvl, int migration_type,
				bool is_combined)
{
	struct item_struct *tmp_item_ptr;
	status_t ret;

	if (migration_type == MIGRATION_RECLAIMABLE)
		return ERR_INVALID_ARGS;

	if (migration_type == MIGRATION_PCPTYPE) {
		/* Migrate the memory item from MOVABLE to the object of the specified
		 * migration type(PCP), and delay the return to MOVABLE
		 */
		/* only forward */
		assert(lvl == MAX_GROUP_LVLS && order == 0 &&
		       lvl == MAX_GROUP_LVLS);
		ret = pnode->vtbl->migrate(pnode, &tmp_item_ptr, order, lvl,
					MIGRATION_PCPTYPE, is_combined);
		if (ret)
			return ret;
		goto final_idx;
	} else if (migration_type == MIGRATION_UNMOVABLE) {
		/* Migrate the memory item from MOVABLE to the object of the specified
		 * migration type (UNMOVABLE), and never return it to MOVABLE
		 */

		/* not is_combined, only one lvl(?TODO:), aligned if need */
		/* only forward */
		ret = pnode->vtbl->migrate(pnode, &tmp_item_ptr, order, lvl,
					MIGRATION_UNMOVABLE, is_combined);
		if (ret)
			return ret;
		goto final_idx;
	} else { /* for MIGRATION_MOVABLE, only for backward find */
		pnode->attr.status = GROUP_BACKWARD;
		ret = pnode->vtbl->dispatch(pnode, &tmp_item_ptr, lvl, migration_type, order,
					 is_combined);
		if (ret) /* backward fail, for other node to migrate */
			ret = pnode->vtbl->migrate(pnode, &tmp_item_ptr, order, lvl,
						MIGRATION_MOVABLE, is_combined);
		if (ret)
			return ERR_NO_MEMORY;
	}
final_idx:
	if (item_pptr)
		*item_pptr = tmp_item_ptr;
	return NO_ERROR;
}

status_t dispatch_group(struct node_struct *pnode, struct item_struct **item_pptr, int lvl,
		      int migration_type, int order, bool is_combined)
{
	struct item_struct *tmp_item_ptr;
	int block_shift;
	word_t status;

	assert(pnode && item_pptr);

	if (migration_type == MIGRATION_PCPTYPE) {
		struct list_node *tmp_head;

		tmp_head = list_remove_head(&PCP_CELL(pnode));
		if (tmp_head) {
			/* head == hot */
			tmp_item_ptr = list_entry_list(tmp_head, tmp_item_ptr, node);
			*item_pptr = tmp_item_ptr;
			PCP_CELLCOUNT(pnode)--;
			goto direct_dispatch;
		}
	}

	status = pnode->attr.status;

	tmp_item_ptr = *item_pptr;
	block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, lvl);

	switch (status) {
	case GROUP_IDLE:
	case GROUP_ONLY_FORWARD:
	case GROUP_FORWARD: {
		tmp_item_ptr = forward_remove_group_migration(lvl, migration_type,
						      order);
		if (tmp_item_ptr) {
			pnode->attr.status = GROUP_FORWARD;
			*item_pptr = tmp_item_ptr;
			goto final_dispatch;
		}

		if (status == GROUP_ONLY_FORWARD) {
			printf("lvl is %d, migration_type is %d, order is %d, is_combined is %d\n",
			       lvl, migration_type, order, is_combined);
			return ERR_NO_MEMORY;
		}

		status_t ret = dispatch_group_migration(pnode, &tmp_item_ptr, order, lvl,
						      migration_type, is_combined);
		if (ret) {
			printf("lvl is %d, migration_type is %d, order is %d, is_combined is %d\n",
			       lvl, migration_type, order, is_combined);
			return ret;
		}

		if (tmp_item_ptr)
			*item_pptr = tmp_item_ptr;
		return NO_ERROR;
	}
	case GROUP_BACKWARD: {
		if (!is_combined) {
			printf("lvl is %d, migration_type is %d, order is %d, is_combined is %d\n",
			       lvl, migration_type, order, is_combined);
			return ERR_INVALID_ARGS;
		}

		tmp_item_ptr = backward_remove_group_migration(lvl, migration_type,
						       order);
		if (tmp_item_ptr) {
			pnode->attr.status = GROUP_IDLE;
			*item_pptr = tmp_item_ptr;
			goto final_dispatch;
		}
		printf("lvl is %d, migration_type is %d, order is %d, is_combined is %d\n",
		       lvl, migration_type, order, is_combined);
		return ERR_NO_MEMORY;
	}
	default:
		printf("NO dispatch type!");
		// unreachable
	}

final_dispatch:
	pnode->attr.nr_freeitems -= BIT(order + block_shift);
	if (compound_item(tmp_item_ptr))
		set_item_state(tmp_item_ptr, STATE_ITEM_MOVABLE_COMP);
	else
		set_item_state(tmp_item_ptr, STATE_ITEM_MOVABLE_NORMAL);

direct_dispatch:

	return NO_ERROR;
}

/* TODO: pcp free to balance for each core */
status_t find_group_reversed(int migration_type, struct item_struct *item)
{
	struct item_struct *item_ptr = item, *tmp_item_ptr;

	int pnode_id = get_item_node(item_ptr);


	if (pnode_id >= PNODE_NUM)
		pnode_id = 0;

	struct node_struct *pnode = get_pnode_num(pnode_id);

	/* Migrate the memory item from MOVABLE to the object of the specified
	 * migration type(PCP), and delay the return to MOVABLE by PCP count
	 */
	if (migration_type == MIGRATION_PCPTYPE) {
		list_add_head(&item_ptr->node,
			     &PCP_CELL(pnode)); // free hot item
		PCP_CELLCOUNT(pnode)++;
		if (PCP_CELLCOUNT(pnode) < PCP_MAX_ITEMS)
			goto final_reversed;
		tmp_item_ptr = list_tail_entry(&PCP_CELL(pnode), tmp_item_ptr, node);
		// coverity[var_deref_model:SUPPRESS]
		list_del(&tmp_item_ptr->node); // tail = cold
		PCP_CELLCOUNT(pnode)--;
		item_ptr = tmp_item_ptr;
		// TO DEBUG
		// migration_type = MIGRATION_MOVABLE;
	}

	dequeue_group_migration(item_ptr, migration_type);

final_reversed:
	return NO_ERROR;
}

// for compound item
status_t find_group(word_t nitems, int migration_type, bool is_combined, int lvl,
		  int lvl_end, struct item_struct **item)
{
	int lvl_order;
	int level;
	int block_shift;
	word_t block_mask;
	word_t block_size;
	word_t lvl_nitem;
	struct item_struct *tmp_item_ptr;
	struct item_struct *item_ptr = NULL;
	struct node_struct *pnode = get_recent_pnode();

	for (level = lvl; level < lvl_end; ++level) {
		block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, level);
		block_size = BIT(block_shift);
		block_mask = block_size - 1;
		lvl_nitem = nitems >> block_shift;
		nitems &= block_mask;
		if (!lvl_nitem)
			continue;

		do {
			if (is_combined)
				lvl_order = item_to_orders(&lvl_nitem);
			else
				lvl_order = item_to_order(&lvl_nitem);

			status_t ret = pnode->vtbl->dispatch(pnode, &tmp_item_ptr, level,
							  migration_type,
							  lvl_order, is_combined);
			if (ret)
				goto bad_idx;

			if (!item_ptr)
				item_ptr = tmp_item_ptr;

			if (is_combined &&
			    (item_ptr != tmp_item_ptr || !compound_item(tmp_item_ptr)))
				compound_append(tmp_item_ptr, item_ptr);
		} while (lvl_nitem);
	}

	if (item)
		*item = item_ptr;

	return NO_ERROR;

bad_idx:

	if (item_ptr)
		find_group_reversed(migration_type, item_ptr);
	return ERR_NO_MEMORY;
}

status_t compound_group(struct item_struct **item_pptr, word_t nitems, int migration_type,
		      bool is_combined)
{
	struct item_struct *item_ptr = NULL;

	if (!nitems)
		return ERR_INVALID_ARGS;
	if (migration_type < 0 || migration_type >= MIGRATION_TYPES)
		return ERR_INVALID_ARGS;


	status_t ret = find_group(nitems, migration_type, is_combined,
				g_group_lvl[get_recent_node_num()], GROUP_LVL_NUM,
				&item_ptr);


	if (ret)
		return ret;
	if (item_pptr)
		*item_pptr = item_ptr;

	return NO_ERROR;
}

status_t decompose_group(struct item_struct *item_pptr)
{
	struct item_struct *item_ptr = item_pptr;

	int migration_type = item_state_to_migration(get_item_state(item_pptr));

	if (migration_type < 0 || migration_type >= MIGRATION_TYPES)
		return ERR_INVALID_ARGS;

	status_t ret = find_group_reversed(migration_type, item_ptr);
	return ret;
}

// __init
status_t init_pnode(struct node_struct *self)
{
	_init_pnode(self);
	return NO_ERROR;
}

status_t migrate_group(struct node_struct *const self, struct item_struct **item_pptr,
		     int order, int lvl, int type, bool is_combined)
{
	struct node_struct *const pnode = self;
	struct item_struct *tmp_item_ptr;

	switch (type) {
	case MIGRATION_PCPTYPE: {
		status_t ret = pnode->vtbl->dispatch(
			pnode, &tmp_item_ptr, lvl, MIGRATION_MOVABLE, order, is_combined);
		if (ret)
			return ret;

		// not do that list append, but free to diff pos
		set_item_state(tmp_item_ptr, STATE_ITEM_PCP_TYPES);
		break;
	}

	case MIGRATION_UNMOVABLE: {
		pnode->attr.status = GROUP_ONLY_FORWARD;

		status_t ret = pnode->vtbl->dispatch(
			pnode, &tmp_item_ptr, lvl, MIGRATION_MOVABLE, order, is_combined);
		if (ret)
			return ret;
		set_item_state(tmp_item_ptr, STATE_ITEM_NOMOVABLE_TYPES);
		break;
	}

	case MIGRATION_MOVABLE: {
		status_t ret = pnode->vtbl->lend(&tmp_item_ptr, MIGRATION_MOVABLE, lvl,
					      order, is_combined);
		if (ret)
			return ret;
		// not do that list append, but free to diff pos
		set_item_state(tmp_item_ptr, STATE_ITEM_MOVABLE_NODE);

		break;
	}

	case MIGRATION_RECLAIMABLE:
	default:
		printf("INvalid migration type!\n");
		// unreachable
	}

	if (item_pptr)
		*item_pptr = tmp_item_ptr;
	return NO_ERROR;
}

#ifdef PNODE_NUM
// the only one level
status_t repeating_dispatch(struct item_struct **item_pptr, struct node_struct *pnode,
			    int lvl, int migration_type, word_t nitems,
			    bool is_combined)
{
	struct item_struct *tmp_item_ptr, *item_ptr = NULL;
	int order;
	status_t ret;

	while (nitems) {
		if (is_combined)
			order = item_to_orders(&nitems);
		else
			order = item_to_order(&nitems);

		ret = pnode->vtbl->dispatch(pnode, &tmp_item_ptr, lvl, migration_type,
					 order, is_combined);
		if (ret)
			return ret;
		if (!item_ptr)
			item_ptr = tmp_item_ptr;

		if (is_combined && (item_ptr != tmp_item_ptr || !compound_item(item_ptr)))
			compound_append(tmp_item_ptr, item_ptr);
	}

	if (item_pptr)
		*item_pptr = item_ptr;
	return NO_ERROR;
}
#endif

status_t lend_group(struct item_struct **item_pptr, int migration_type, int lvl,
		  int lvl_order, bool is_combined)
{
#ifdef PNODE_NUM

	struct item_struct *tmp_item_ptr, *item_ptr = NULL;
	struct node_struct *pnode;
	int i;
	status_t ret;
	int block_shift;
	long nr_items, pn_freeitem;

	block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM, lvl);
	nr_items = BIT(lvl_order + block_shift);

	for (i = 0; i < PNODE_NUM && nr_items; i++) {
		pnode = get_pnode_num(i);
		pn_freeitem = pnode->attr.nr_freeitems;
		if (!pn_freeitem)
			continue;

		if (nr_items > pn_freeitem) {
			if (!is_combined)
				continue;
			ret = repeating_dispatch(&tmp_item_ptr, pnode, lvl,
						 migration_type, pn_freeitem,
						 is_combined);
			if (ret)
				return ret;
			nr_items -= pn_freeitem;
		} else {
			ret = repeating_dispatch(&tmp_item_ptr, pnode, lvl,
						 migration_type, nr_items,
						 is_combined);
			if (ret)
				return ret;
			nr_items = 0;
		}
		if (!item_ptr)
			item_ptr = tmp_item_ptr;
		if (is_combined && (item_ptr != tmp_item_ptr || !compound_item(item_ptr)))
			compound_append(tmp_item_ptr, item_ptr);
	}

	if (item_pptr)
		*item_pptr = item_ptr;
	return NO_ERROR;
#endif

	return ERR_NO_MEMORY;
}

bool budget_sufficient(long nitems, int migration_type)
{
	struct node_struct *pnode;
	long remaining_nitem = 0;


	for (int i = 0; i < PNODE_NUM; i++) {
		pnode = get_pnode_num(i);
		nitems -= pnode->attr.nr_freeitems;
		if (nitems < 0)
			return true;
	}

	if (migration_type == MIGRATION_PCPTYPE)
		remaining_nitem = PCP_CELLCOUNT(pnode);
	else if (migration_type == MIGRATION_UNMOVABLE)
		remaining_nitem = node_budget(migration_type);
	if (remaining_nitem >= nitems)
		return true;
	return false;
}

// pcp & migration
status_t node_struct_migrate(struct node_struct *const self,
				struct item_struct **item_pptr, int order, int lvl,
				int type, bool is_combined)
{
	return migrate_group(self, item_pptr, order, lvl, type, is_combined);
}

// (item)<single unit(ha), signle unit(ha) ...> or (item)<signal unit(ha),
// compound unit(ha) ...> item=NULL, only ha
static status_t group_struct_compound(struct item_struct **item_pptr, word_t nitems,
					int migration_type, bool is_combined)
{
	return compound_group(item_pptr, nitems, migration_type, is_combined);
}

static status_t group_struct_decompose(struct item_struct *item_pptr)
{
	return decompose_group(item_pptr);
}

static void
group_struct_dump_migration(struct group_struct *const self, int type)
{
	struct group_list *group_list;

	group_list = &self->free_group[type];
	for (int order = 0; order < GROUP_ORDER_NUM; order++) {
		if (group_list->free_count[order])
			printf(
				"[type, cell, order, count]:%d, %p, %d, %ld\n",
				type, &group_list->free_cell[order], order,
				group_list->free_count[order]);
	}
}

static status_t group_struct_dump_group(struct group_struct *const self)
{
	for (int type = 0; type < MIGRATION_TYPES; type++)
		group_struct_dump_migration(self, type);

	return NO_ERROR;
}

static status_t node_struct_dispatch(struct node_struct *const self,
					struct item_struct **item_pptr, int lvl,
					int migration_type, int order,
					bool is_combined)
{
	return dispatch_group(self, item_pptr, lvl, migration_type, order, is_combined);
}

static status_t node_struct_init(struct node_struct *const self)
{
	return init_pnode(self);
}

static status_t node_struct_lend(struct item_struct **item_pptr, int migration_type,
				    int lvl, int lvl_order, bool is_combined)
{
	return lend_group(item_pptr, migration_type, lvl, lvl_order, is_combined);
}

ssize_t preprocess_node_struct_alloc(int flags, word_t nitems,
					int migration_type)
{
	word_t tmp_nitems = nitems;

	switch (flags) {
	case FUNC_RESERVED: {
		if (tmp_nitems == node_budget(migration_type))
			break;
		for (int i = g_group_lvl; i < MAX_GROUP_LVLS; i++)
			if (tmp_nitems == node_budget_lvl(migration_type, i))
				break;
		item_to_orders(&tmp_nitems);
		if (!tmp_nitems)
			break;
		return ERR_INVALID_ARGS;
	}
	case FUNC_DEBUG: {
		word_t node_nitem = node_budget(migration_type);

		if (node_nitem > UNMOV_MAX_ITEMS || tmp_nitems > UNMOV_MAX_ITEMS)
			return ERR_INVALID_ARGS;
		item_to_orders(&tmp_nitems);
		if (tmp_nitems)
			return ERR_INVALID_ARGS;
		break;
	}
	case FUNC_SYSCALL_CONT:
	case FUNC_MMU:
	case FUNC_OBJECT: {
		nitems = BIT(item_to_order(&tmp_nitems));
		break;
	}
	default:
		break;
	}
	return nitems;
}

// owner or borrower
static status_t node_struct_alloc(struct node_struct *pnode, range_base_t *range_base,
				     word_t nitems, int flags)
{
	struct item_struct *item_pptr;
	int migration_type;
	bool is_combined;
	ssize_t psize;

	// G_LOCK
	// watermark
	if (!nitems) {
		printf("nitems args is zero\n");
		return ERR_INVALID_ARGS;
	}

	prepare_node_struct_alloc(nitems, flags, &migration_type, &is_combined);

	spin_lock_saved_state_t sflags;

	spin_lock_irqsave(&pnode->node_struct_lock, sflags);
	psize = preprocess_node_struct_alloc(flags, nitems, migration_type);
	if (psize <= 0) {
		spin_unlock_irqrestore(&pnode->node_struct_lock, sflags);
		printf("args parse is error, nitems is %d, flags is %d, migration_type is %d, is_combined is %d\n",
		       psize, flags, migration_type, is_combined);
		return ERR_INVALID_ARGS;
	}

	bool suff = budget_sufficient(psize, migration_type);

	if (!suff) {
		spin_unlock_irqrestore(&pnode->node_struct_lock, sflags);
		printf("budge is zero, nitems args is %d\n", psize);
		return ERR_NO_MEMORY;
	}

	status_t ret =
		pnode->group_vtbl->compound(&item_pptr, psize, migration_type, is_combined);
	spin_unlock_irqrestore(&pnode->node_struct_lock, sflags);

	if (ret)
		return ret;
	if (range_base)
		*range_base = item_to_range(item_pptr);
	return NO_ERROR;
}

// owner or borrower
static status_t node_struct_free(range_base_t range_base)
{
	// G_LOCK

	long idx = range_to_idx(range_base);
	struct node_struct *pnode = idx_to_node(idx);

	if (!pnode) {
		printf("range_base to pnode is error, range_base is 0x%lx\n", range_base);
		return ERR_INVALID_ARGS;
	}

	spin_lock_saved_state_t flags;

	spin_lock_irqsave(&pnode->node_struct_lock, flags);
	status_t ret = pnode->group_vtbl->decompose(range_to_item(range_base));

	spin_unlock_irqrestore(&pnode->node_struct_lock, flags);

	return ret;
}

void node_struct_dump_pcp(struct node_struct *const self)
{
	struct node_list *node_list;

	node_list = &self->pcp;
	for (int cid = 0; cid < PNODE_NUM; cid++) {
		if (node_list->free_count[cid])
			printf("[pcplist, pcpcount]: %p, %d\n",
				  &node_list->free_cell[cid],
				  node_list->free_count[cid]);
	}
}

void pnode_node_struct_dump_group(int nid)
{
	struct group_struct *group;

	for (int lvl = g_group_lvl; lvl < GROUP_LVL_NUM; lvl++) {

		group = &g_group_struct[nid][lvl];

		printf("[group, lvl]: %p, %d\n", group, lvl);
		group->vtbl->dump_area(group);
	}
}

static status_t node_struct_dump_node(struct node_struct *const self)
{
	spin_lock_saved_state_t sflags;

	spin_lock_irqsave(&self->node_struct_lock, sflags);

	int nid = self->pnode_id;

	printf(
		"[nid %d, addr 0x%lx, end 0x%lx, status %d, nr_freeitems 0x%lx]\n",
		nid, self->attr.addr, self->attr.size, self->attr.status,
		self->attr.nr_freeitems);
	node_struct_dump_pcp(self);
	pnode_node_struct_dump_group(nid);

	printf(
		"[pg_start %p, pg_end %p, pg_align %p, entry_lvl %d, entry_order %d]\n",
		g_item_struct[nid], g_item_struct_end[nid], g_item_struct_of[nid],
		g_group_lvl[nid], g_group_order[nid]);

	spin_unlock_irqrestore(&self->node_struct_lock, sflags);
	return NO_ERROR;
}
