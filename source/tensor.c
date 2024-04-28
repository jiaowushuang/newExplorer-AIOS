#include "tensor_struct.h"
#include "tensor_ops.h"


struct group_struct phy_group_struct[PNODE_NUM][GROUP_LVL_NUM];
struct node_struct phy_node_struct[PNODE_NUM];
struct item_struct *phy_item_struct[PNODE_NUM];
struct item_struct *phy_item_struct_end[PNODE_NUM];
void *phy_item_struct_of[PNODE_NUM];
int phy_group_lvl[PNODE_NUM];
int phy_group_order[PNODE_NUM];

struct group_struct entity_group_struct[GROUP_LVL_NUM];
struct node_struct entity_node_struct;


// TODO: 

struct in_paras para_benchmark[DIM_NUM];


static status_t group_struct_compound(struct item_struct **item_pptr, word_t nitems,
					int migration_type, struct alloc_para *para, int dim);
static status_t group_struct_decompose(struct item_struct *item_pptr, bool is_virtual);
static status_t group_struct_dump_group(struct group_struct *const self, int migration_type);
static status_t node_struct_migrate(struct node_struct *const self,
				       struct item_struct **item_pptr, int order, int lvl,
				       int type, bool is_combined);
static status_t node_struct_dispatch(struct node_struct *const self,
					struct item_struct **item_pptr, int lvl,
					int migration_type, int order,
					bool is_combined, bool is_virtual,
					bool is_stream);
static status_t node_struct_init(struct node_struct *const self, bool is_virtual);
static status_t node_struct_lend(struct item_struct **item_pptr, int migration_type,
				    int lvl, int lvl_order, bool is_combined);
static status_t node_struct_alloc(struct node_struct *node, range_base_t *range_base,
				     word_t nitems, int flags, struct in_paras *paras);
static status_t node_struct_free(range_base_t range_base, bool is_virtual, int dim);
static status_t node_struct_dump_node(struct node_struct *const self, bool is_virtual);
static void group_struct_dump_migration(struct group_struct *const self,
					  int type);
static status_t node_struct_sched(struct node_struct *const self,
				struct in_paras *in, unsigned int *out);

extern status_t init_pnode(struct node_struct *self, range_base_t range, size_t size);

extern status_t init_vnode(struct node_struct *self, range_base_t range, size_t size);

extern void update_pnode(struct node_struct *self, size_t size);
extern void update_vnode(struct node_struct *self, size_t size);
status_t heap_alloc(struct slice_struct *slice, range_base_t *range_base);
status_t heap_free(range_base_t range_base);

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
	.slice_alloc = heap_alloc,
	.slice_free = heap_free,
	// .sched = ,
	// .map = ,
	// .rmap = ,
};


/* Resource	Reference	OPSA↓/F↑Object			Attribute		Allocat-order	Runtime-Resource-dependency-network
 *
 * virtual	
 *		size		free	single-process-		dis/continuous(finite)	disorder	Blocking wait on other items(v)
 *		range			mutli-thread		sparse(infinite)	order|disorder	Response action on other items(v)
 * f: v->p												event-driven(interrupt/exception)
 * 		prior		ready 	single-node-mutli-group	sparse			order				
 *		prior		ready   single-group-mutli-process					Blocking wait on other items(p->v)
 *		workload↑	free	multi-node		sparse			order		Response action on other items(p->v)
 *		
 *		Execution entity: Process -> Child Process -> Thread
 * physical
 *		size		free	mutli-allocat-request  	dis/continuous		disorder	Blocking wait on other items(p)
 *		range			in single/mutli-node	sparse			order|disorder	Response action on other items(p)
 *
 *
 * NOTE:
 * 1) time of item maybe wait on memory of item for allocat
 * 2) memory allocat overtime or replacement aging
 * 3) prior scheduing, load balancing
 * 4) free: The amount of remaining resources available
 * 5) ready: Number of ready tasks
 *
 */

/* f: v->p
 *		Reference		OPS rule A↓/F↑
 * level 0	node workload		order==0, allocat prior max; order↑, workload↑, allocat prior↓;
 *					load balancing: all node in the same order
 * level 1	process group prior(Ig)	order==0, allocat prior max; order↑, schedule↓, allocat prior↓;
 * level 2	process prior		..
 * level 3	thread prior		..
 *
 */

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
			   int size, int order_max, int migration_type, int lvl) // item/size maybe abs or rel
{
	struct item_struct *present;
	struct list_node *temp;
	int node_id;

	node_id = group->node->node_id;
	present = &item[size];
	temp = &GROUP_CELL(group, migration_type)[order_max];

	list_add_head(temp, &present->node);
	clear_item_present(present);
	set_item_order(present, order_max);
	clear_item_state(present);
	set_item_node(present, node_id);
	set_item_section(present, lvl);
	
	GROUP_CELLCOUNT(group, migration_type)[order_max]++;
	/* according to range_base, can find which node or area */
}

static void split_group_migration(struct group_struct *group,
				 struct item_struct *item, int lvl, int order_min,
				 int order_max, int migration_type)
{
	int item_shift = migration_type == MIGRATION_SPARSE_MOVABLE ? 
			GROUP_RANGE_NUM_SHIFT : GROUP_ORDER_NUM_SHIFT;
	int block_shift = FUNCTION_ITEM_LVL(item_shift, lvl);
	int size;
	int dim = group->node->attr.dim;
	int node_id = node_id = group->node->node_id;
			
	while (order_max > order_min) {
		order_max--; /* size >>= 1; */
		size = migration_type == MIGRATION_SPARSE_MOVABLE ? BIT(order_max) :
					BIT(order_max + block_shift);
		split2_group_migration(group, item, size,
				      order_max, migration_type, lvl);
	}

	set_item_present(item);
	set_item_order(item, order_max);
	set_item_node(item, node_id);
	set_item_section(item, lvl);
	set_item_freeidx(item, BIT(FUNCTION_ITEM(item_shift)));
	set_item_dim(item, dim);
}

static void sequence_split_group_migration(struct group_struct *group,
				 struct item_struct *item, int lvl, int order_min,
				 int order_max, int migration_type, int rel_idx)
{
	int item_shift = migration_type == MIGRATION_SPARSE_MOVABLE ? 
			GROUP_RANGE_NUM_SHIFT : GROUP_ORDER_NUM_SHIFT;
	int block_shift = FUNCTION_ITEM_LVL(item_shift, lvl);
	int rel;
	int size;
	struct item_struct *present, *free;
	struct list_node *temp;
	int node_id;

	node_id = group->node->node_id;
	while (order_max > order_min) {
		order_max--; /* size >>= 1; */
		size = migration_type == MIGRATION_SPARSE_MOVABLE ? BIT(order_max) :
					BIT(order_max + block_shift);
		
		present = &item[size];
		temp = &GROUP_CELL(group, migration_type)[order_max];
		rel = present - item;
		if (rel < rel_idx)
			free = item;
			
		else
			free = present;

		list_add_head(temp, &free->node);
		clear_item_present(free);
		set_item_order(free, order_max);
		clear_item_state(free);
		set_item_node(free, node_id);
		set_item_section(free, lvl);
		
		item = present;
		GROUP_CELLCOUNT(group, migration_type)[order_max]++;
	}

	free = &item[rel_idx];
	set_item_present(free);
	set_item_order(free, order_min);
	set_item_node(free, node_id);
	set_item_section(free, lvl);
	set_item_freeidx(free, BIT(FUNCTION_ITEM(item_shift)));
	set_item_dim(free, group->node->attr.dim);
	// current, the item of (idx, order) is empty, not in freecell
}

				 
struct item_struct *remove_group_migration(struct group_struct *group, int lvl,
				      int order, int migration_type,
				      bool is_table)
{
	struct item_struct *item;
	word_t order_index;
	struct list_node *temp;
	int block_shift;
	int max_order;
	long item_idx = -1;
	int dim = group->node->attr.dim;

	int item_shift = migration_type == MIGRATION_SPARSE_MOVABLE ? 
			GROUP_RANGE_NUM_SHIFT : GROUP_ORDER_NUM_SHIFT;
	
	if (migration_type == MIGRATION_SPARSE_MOVABLE)
		max_order = FUNCTION_ITEM(item_shift)-1;
	else
		max_order = (lvl == phy_group_lvl[get_current_node_num(dim)]) ?
			    phy_group_order[get_current_node_num(dim)] :
			    FUNCTION_ITEM(item_shift)-1;

	for (order_index = order; order_index < max_order; order_index++) {
		temp = &GROUP_CELL(group, migration_type)[order_index];
		
		// SPARSE_TABLE, must be new
		// At this time, the blocks represented in the table can no 
		// longer meet the allocation requirements
		if (migration_type == MIGRATION_SPARSE_MOVABLE && is_table && order_index == 0)
			continue;	

		// filter
		list_entry_list(temp, item, node) {
			// 1) order == 0, SPARSE_TABLE; SPARSE_BLOCK
			if (migration_type != MIGRATION_SPARSE_MOVABLE ||
				order_index != 0 ||
				item->prev == NULL) // !SPARSE_TABLE == SPARSE_BLOCK OR XX_BLOCK
				goto done_remove_group_migration;
			// 2) FREECELL, currfree stream item; subscribe stream item
			// stream gap:
			// between currfree(item->prev==nullptr&&item_root->next!=nulllptr) and 
			// subscribe(same to currfree),
			// gap item: gap_item_root->next==nulllptr
			// ----> subscribe_group_sparse
		}
		
		item = NULL;
done_remove_group_migration:	
		if (!item)
			continue;

		// TODO: only for abs
		if (migration_type != MIGRATION_SPARSE_MOVABLE) {
			item_idx = item_to_idx(item);
			block_shift = FUNCTION_ITEM_LVL(item_shift, lvl);
			if (item_idx & MASK(block_shift))
				goto bad_align_or_lack_memory;
		}

		assert(!item_is_present(item));
		list_del(&item->node);
		GROUP_CELLCOUNT(group, migration_type)[order_index]--;
		split_group_migration(group, item, lvl, order, order_index,
				     migration_type);

		return item;
	}

bad_align_or_lack_memory: // means the current lvl is not prepared
	printf("item %ld order %d bad align by level %d of migration_type %d of nitems %ld!\n",
		item_idx, order, lvl, migration_type,
		node_budget(migration_type));

	return NULL;
}
				      
// [only physical]

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
					      
// [only physical]

static inline void compound_append(struct item_struct *node, struct item_struct *head)
{
	bool temp_head;

	temp_head = head->compound_head;
	if (!temp_head) {
		head->compound_head = 1;
		return;
	}
	struct item_struct *temp = head;
	while (temp->compound_node)
		temp = temp->compound_node;
	temp->compound_node = node;
}

// [only physical]

static inline struct item_struct *compound_remove_head(struct item_struct *head)
{
	bool temp_head;

	temp_head = head->compound_head;
	if (!temp_head)
		return NULL;
	head->compound_head = 0;
	head->compound_node = NULL;

	return head;
}

// [only physical]

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
					
// [only physical]

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
	//bool valid = rel_idx_is_valid(buddy_idx);
	bool in_list = list_in_list(&buddy_page->node);
	int state = get_item_state(buddy_page);
	int buddy_migration_type = item_state_to_migration(state);

	return  //valid && 
		in_list && buddy_migration_type == migration_type;
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
	int block_shift, order_index;
	int dim = group->node->attr.dim;
	assert(item_is_present(item));
	
	int item_shift = migration_type == MIGRATION_SPARSE_MOVABLE ? 
		GROUP_RANGE_NUM_SHIFT : GROUP_ORDER_NUM_SHIFT;
	block_shift = FUNCTION_ITEM_LVL(item_shift, lvl);
	if (migration_type == MIGRATION_SPARSE_MOVABLE) 
		order_index = order;
	else
		order_index = order + block_shift;
	
	while (old_lvl != lvl) {
		old_lvl = lvl;
		idx = item_to_rel_idx(item, pid);
		if (migration_type == MIGRATION_SPARSE_MOVABLE)
			max_order = FUNCTION_ITEM(item_shift)-1;
		else
			max_order = (lvl == phy_group_lvl[get_current_node_num(dim)]) ?
				    phy_group_order[get_current_node_num(dim)] :
				    FUNCTION_ITEM(item_shift)-1;

		while (order <= max_order - 1) {
			clear_item_present(item);
	
			if (migration_type == MIGRATION_LRUTYPE ||
			    (lvl == (migration_type == MIGRATION_SPARSE_MOVABLE ? 0 : phy_group_lvl[get_current_node_num(dim)]) && 
			    order == max_order - 1))
				goto free_item_migration_direct;

			if (get_item_freeidx(item) > 0 && 
				get_item_freeidx(item) < BIT(MAX_ITEM_RANGE+1)
				goto free_item_migration_direct;

			buddy_idx = next_buddy_idx(idx, order_index);
			buddy_page = item + (buddy_idx - idx);	
			
			if (!item_is_buddy(buddy_page, order) ||
			    !buddy_is_valid(migration_type, buddy_idx,
					    buddy_page))
				goto free_item_migration_done;

			list_del(&buddy_page->node);
			clear_item_order(buddy_page);
			clear_item_node(buddy_page);
			
			clear_item_section(item);
			clear_item_node(item);
			clear_item_order(item);
			clear_item_state(item);
			clear_item_dim(item);
			clear_item_freeidx(item);

			GROUP_CELLCOUNT(group, migration_type)[order]--;
			combined_idx = buddy_idx & idx;
			item = item + (combined_idx - idx);
			idx = combined_idx;
			order++;
			order_index++;
			set_item_order(item, order);
		}

free_item_migration_done:
		if ((order < max_order - 2) &&
		    buddy_is_valid(migration_type, buddy_idx, buddy_page)) {
			struct item_struct *higher_page;
			struct item_struct *higher_buddy;

			combined_idx = buddy_idx & idx;
			higher_page = item + (combined_idx - idx);
			buddy_idx = next_buddy_idx(combined_idx, order_index + 1);
			higher_buddy = higher_page + (buddy_idx - combined_idx);
			if (//rel_idx_is_valid(buddy_idx) &&
			    item_is_buddy(higher_buddy, order + 1)) {
				temp = &GROUP_CELL(group,
						      migration_type)[order];
				list_add_tail(&item->node, temp);
				goto free_item_migration_out;
			}
		}

		if (order >= FUNCTION_ITEM(item_shift)-1 && migration_type != MIGRATION_SPARSE_MOVABLE) {
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

// [only physical]

void dequeue_group_migration(struct item_struct *item, int migration_type)
{
	int order_index, lvl_index;
	struct item_struct *temp_page;
	int node_id;
	word_t nitems = 0;
	int block_shift;
	struct node_struct *node;
	int item_shift = GROUP_ORDER_NUM_SHIFT;
	do {
		temp_page = (struct item_struct *)compound_remove_head(item);
		if (!temp_page)
			temp_page = item;

		node_id = get_item_node(temp_page);

		if (node_id >= PNODE_NUM)
			node_id = 0;

		node = get_pnode_num(node_id);
		order_index = get_item_order(temp_page);
		lvl_index = get_item_section(temp_page);
		if (lvl_index > MAX_GROUP_LVLS)
			lvl_index = MAX_GROUP_LVLS;
		free_group_migration(temp_page, get_group_lvl(lvl_index, node_id),
				    lvl_index, order_index, migration_type);
		block_shift = FUNCTION_ITEM_LVL(item_shift, lvl_index);
		nitems = BIT(order_index + block_shift);
		if (migration_type != MIGRATION_UNMOVABLE)
			node->attr.nr_freeitems += nitems;
	} while (compound_item(item));
}

// [only physical]

void split_item_migration(struct group_struct *group, int lvl, int order,
			  struct item_struct *item, int migration_type)
{
	int item_shift = GROUP_ORDER_NUM_SHIFT;
	int block_shift = FUNCTION_ITEM_LVL(item_shift, lvl);

	split2_group_migration(group, item, BIT(order - 1 + block_shift),
			      order - 1, migration_type, lvl);
	split2_group_migration(group, item, 0, order - 1, migration_type, lvl);
}
			  
// [only physical]

word_t group_budget(struct group_struct *group, int lvl, int migration_type,
		   int order, int order_end)
{
	int order_cnt = 0;
	word_t nitems = 0;
	int nitem_base;
	int item_shift = GROUP_ORDER_NUM_SHIFT;
	nitem_base = FUNCTION_ITEM_LVL(item_shift, lvl);
	for (int index = order; index <= order_end; ++index) {
		order_cnt = GROUP_CELLCOUNT(group, migration_type)[index];
		nitems += order_cnt * BIT(index) * BIT(nitem_base);
	}
	return nitems;
}

// [only physical]

word_t node_budget_lvl(int migration_type, int lvl, int dim_type)
{
	word_t budget = 0;

	budget += group_budget(get_group_lvl(lvl, get_current_node_num(dim_type)), lvl,
			      migration_type, 0, MAX_ITEM_ORDER);

	return budget;
}

// [only physical]

word_t node_budget(int migration_type, int dim_type)
{
	word_t budget = 0;

	for (int i = phy_group_lvl[get_current_node_num(dim_type)]; i < MAX_GROUP_LVLS; i++)
		budget += node_budget_lvl(migration_type, i, dim_type);

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
 
// [only physical]

static inline int forward_group_budget(int reverse_index, int migration_type,
				      int order, int dim)
{
	int order_index = order;

	while (reverse_index >= phy_group_lvl[get_current_node_num(dim)]) {
		if (group_budget(get_group_lvl(reverse_index, get_current_node_num(dim)),
				reverse_index, migration_type, order_index,
				MAX_ITEM_ORDER)) {
			break;
		}
		--reverse_index;
		order_index = 0;
	}

	return reverse_index;
}

// [only physical]

static inline int backward_group_budget(int index, int migration_type, int order, int dim)
{
	int order_index = order;
	int order_nitem = 0;
	int nitem_need = BIT(order);
	int item_shift = GROUP_ORDER_NUM_SHIFT;
	while (index < GROUP_LVL_NUM) {
		order_nitem =
			group_budget(get_group_lvl(index, get_current_node_num(dim)), index,
				    migration_type, 0, order_index - 1);
		if (order_nitem >= nitem_need)
			break;
		nitem_need -= order_nitem;
		++index;
		order_index = item_shift;
		nitem_need <<= FUNCTION_ITEM(item_shift);
	}
	return index;
}

// [only physical]

struct item_struct *forward_remove_group_migration(int reverse_index,
					      int migration_type, int order, int dim)
{
	int last_index;
	struct item_struct *last_item = NULL;
	int item_shift = GROUP_ORDER_NUM_SHIFT;
	last_index = forward_group_budget(reverse_index, migration_type, order, dim);
	if (last_index < phy_group_lvl[get_current_node_num(dim)])
		goto bad_budget;
	while (last_index != reverse_index) {
		last_item = remove_group_migration(
			get_group_lvl(last_index, get_current_node_num(dim)), last_index, 0,
			migration_type, false);
		assert(last_item);
		split_item_migration(get_group_lvl(last_index + 1,
						 get_current_node_num(dim)),
				     last_index + 1, item_shift,
				     last_item, migration_type);
		++last_index;
	}
	last_item =
		remove_group_migration(get_group_lvl(last_index, get_current_node_num(dim)),
				      last_index, order, migration_type, false);

bad_budget:

	return last_item;
}

// [only physical]

struct item_struct *backward_remove_group_migration(int index, int migration_type,
					       int order, int dim)
{
	int last_index, order_index = order, norder = 1;
	struct item_struct *last_item = NULL;
	int item_shift = GROUP_ORDER_NUM_SHIFT;
	last_index = backward_group_budget(index, migration_type, order, dim);
	if (last_index == GROUP_LVL_NUM)
		goto bad_budget;
	while (last_index != index) {
		norder = enqueue_group_migration(
			get_group_lvl(index, get_current_node_num(dim)), &last_item,
			order_index, norder, migration_type);
		order_index = item_shift;
		++index;
		// norder != 0
	}
	norder = enqueue_group_migration(get_group_lvl(index, get_current_node_num(dim)),
					&last_item, order_index, norder,
					migration_type);
	// norder == 0
bad_budget:
	return last_item;
}

// [only physical]

status_t dispatch_group_migration(struct node_struct *node, struct item_struct **item_pptr,
				int order, int lvl, int migration_type,
				bool is_combined)
{
	struct item_struct *tmp_item_ptr;
	status_t ret;

	if (migration_type == MIGRATION_RECLAIMABLE || 
		migration_type == MIGRATION_SPARSE_MOVABLE)
		return ERR_INVALID_ARGS;

	if (migration_type == MIGRATION_LRUTYPE) {
		/* Migrate the memory item from MOVABLE to the object of the specified
		 * migration type(LRU_CACHE), and delay the return to MOVABLE
		 */
		/* only forward */
		assert(lvl == MAX_GROUP_LVLS && order == 0 &&
		       lvl == MAX_GROUP_LVLS);
		ret = node->vtbl->migrate(node, &tmp_item_ptr, order, lvl,
					MIGRATION_LRUTYPE, is_combined);
		if (ret)
			return ret;
		goto final_idx;
	} else if (migration_type == MIGRATION_UNMOVABLE) {
		/* Migrate the memory item from MOVABLE to the object of the specified
		 * migration type (UNMOVABLE), and never return it to MOVABLE
		 */

		/* not is_combined, only one lvl(?TODO:), aligned if need */
		/* only forward */
		ret = node->vtbl->migrate(node, &tmp_item_ptr, order, lvl,
					MIGRATION_UNMOVABLE, is_combined);
		if (ret)
			return ret;
		goto final_idx;
	} else { /* for MIGRATION_MOVABLE, only for backward find */
		node->attr.status = GROUP_BACKWARD;
		ret = node->vtbl->dispatch(node, &tmp_item_ptr, lvl, migration_type, order,
					 is_combined, false);
		if (ret) /* backward fail, for other node to migrate */
			ret = node->vtbl->migrate(node, &tmp_item_ptr, order, lvl,
						MIGRATION_MOVABLE, is_combined);
		if (ret)
			return ERR_NO_MEMORY;
	}
final_idx:
	if (item_pptr)
		*item_pptr = tmp_item_ptr;
	return NO_ERROR;
}

/* node is allocat */
static void set_group_item_state(struct item_struct *item, int migration_type,
					bool is_combined, bool is_virtual, bool is_stream,
					bool is_entry)
{
	if (is_stream) {
		if (is_virtual)
			set_item_state(item, STATE_ITEM_SPARSE_VIRTUAL_STREAM);
		else
			set_item_state(item, STATE_ITEM_SPARSE_PHYSICAL_STREAM);
	} else {
		if (migration_type == MIGRATION_SPARSE_MOVABLE)
			if (is_entry)
				set_item_state(item, STATE_ITEM_SPARSE_PRIOR);
			else
				set_item_state(item, STATE_ITEM_SPARSE_TYPES);
		else {
			if (compound_item(item))
				set_item_state(item, STATE_ITEM_MOVABLE_COMP);
			else
				set_item_state(item, STATE_ITEM_MOVABLE_NORMAL);
		}
	}

}


status_t dispatch_group(struct node_struct *node, struct item_struct **item_pptr, int lvl,
		      int migration_type, int order, struct alloc_para *para)
{
	struct item_struct *tmp_item_ptr;
	int block_shift;
	word_t status;
	bool is_combined, is_virtual, is_stream, is_entry;

	int dim = node->attr.dim;
	is_combined = para->is_combined;
	is_virtual = para->is_virtual;
	is_stream = para->is_stream;
	is_entry = para->is_entry;
	
	assert(node && item_pptr);
	
	if (migration_type == MIGRATION_LRUTYPE) {
		struct list_node *tmp_head;

		tmp_head = list_remove_head(&LRU_CACHE_CELL(node));
		if (tmp_head) {
			/* head == hot */
			tmp_item_ptr = list_entry_list(tmp_head, tmp_item_ptr, node);
			*item_pptr = tmp_item_ptr;
			LRU_CACHE_CELLCOUNT(node)--;
			goto direct_dispatch;
		}
	}

	status = node->attr.status;

	tmp_item_ptr = *item_pptr;
	block_shift = is_entry ? FUNCTION_ITEM_LVL_PRIOR(GROUP_PRIOR_NUM_SHIFT, level) :
				FUNCTION_ITEM_LVL(migration_type == MIGRATION_SPARSE_MOVABLE ? 
					GROUP_RANGE_NUM_SHIFT : GROUP_ORDER_NUM_SHIFT, level);

	switch (status) {
	case GROUP_IDLE:
	case GROUP_ONLY_FORWARD:
	case GROUP_FORWARD: {
		if (migration_type == MIGRATION_SPARSE_MOVABLE) {
			if (is_stream == true)
				tmp_item_ptr = forward_remove_group_stream(lvl, migration_type,
						      order, dim, para);
			else if (is_entry == true)
				tmp_item_ptr = forward_remove_group_prior(lvl, migration_type,
						      order, node);
			else
				tmp_item_ptr = forward_remove_group_sparse(lvl, migration_type,
						      order, dim, is_virtual);	
		} else {
			tmp_item_ptr = forward_remove_group_migration(lvl, migration_type,
									      order, dim);
		} 
		
		if (tmp_item_ptr) {
			node->attr.status = GROUP_FORWARD;
			*item_pptr = tmp_item_ptr;
			goto final_dispatch;
		}

		if (status == GROUP_ONLY_FORWARD) {
			printf("lvl is %d, migration_type is %d, order is %d, is_combined is %d\n",
			       lvl, migration_type, order, is_combined);
			return ERR_NO_MEMORY;
		}

		status_t ret = dispatch_group_migration(node, &tmp_item_ptr, order, lvl,
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
						       order, dim);
		if (tmp_item_ptr) {
			node->attr.status = GROUP_IDLE;
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

	// in entry, all task is ready or blocked, not running, so is 0.
	node->attr.nr_freeitems -= is_entry ? 0 : BIT(order + block_shift); // TODO:0
	if (dim == DIM_TIME) {
		// migrate in cell because of the node workload is changed.
		
	}
	
	set_group_item_state(tmp_item_ptr, migration_type, is_combined, 
				is_virtual, is_stream, is_entry);

direct_dispatch:

	return NO_ERROR;
}

// [virtual and physical(time)]
/*
 * root/parent item must be SPARSE_TABLE. 
 * root item '->next' must be point to item array.
 * root or child item '->prev' must be point to parent item or NULL
 * GROUP_CELL has SPARSE_BLOCK or free-SPARSE_TABLE
 * init, GROUP_CELL only has SPARSE_BLOCK.
 *
 * SPARSE_BLOCK can split to SPARSE_BLOCK or SPARSE_TABLE.
 * request need SPARSE_BLOCK only.
 * 
 */

enum {
	SPARSE_TABLE = 0, // order must be 0
	SPARSE_BLOCK = 1, // order 0-MAX_ITEM_RANGE
};

static inline bool sparse_table_free(struct item_struct *item, int order) 
{
	int nr_freeidxs = get_item_freeidx(item);
	bool is_free;
	assert(nr_freeidxs <= BIT(MAX_ITEM_RANGE+1));

	if (nr_freeidxs != 0) {
		nr_freeidxs -= BIT(order);
		set_item_freeidx(item, nr_freeidxs);
	}

	is_free = nr_freeidxs != 0;
	return is_free;
}

static void refill_sparse_table(struct item_struct *item, int lvl, 
		int migration_type, int dim, bool is_virtual)
{
	struct list_node *temp;

	if (is_virtual) {
		temp = &GROUP_CELL
			(get_vgroup_lvl(lvl, get_current_vnode_num(dim)), migration_type)[0];
		list_add_head(temp, &item->node);
		GROUP_CELLCOUNT
			(get_vgroup_lvl(lvl, get_current_vnode_num(dim)), migration_type)[0]++;

	} else {
		temp = &GROUP_CELL
			(get_group_lvl(lvl, get_current_node_num(dim)), migration_type)[0];
		list_add_head(temp, &item->node);
		GROUP_CELLCOUNT
			(get_group_lvl(lvl, get_current_node_num(dim)), migration_type)[0]++;

	}

}

static void remove_sparse_table(struct item_struct *item, int lvl, 
		int migration_type, int dim, bool is_virtual)
{
	struct list_node *temp;

	if (is_virtual) {
		temp = &GROUP_CELL
			(get_vgroup_lvl(lvl, get_current_vnode_num(dim)), migration_type)[0];
		list_del(&item->node);
		GROUP_CELLCOUNT
			(get_vgroup_lvl(lvl, get_current_vnode_num(dim)), migration_type)[0]--;

	} else {
		temp = &GROUP_CELL
			(get_group_lvl(lvl, get_current_node_num(dim)), migration_type)[0];
		list_del(&item->node);
		GROUP_CELLCOUNT
			(get_group_lvl(lvl, get_current_node_num(dim)), migration_type)[0]--;

	}
}

		
static struct item_struct * new_group_slice(struct node_struct *node, int lvl)
{
	struct slice_struct *slice = NODE_SLICE_STRUCT_ITEM(node);
	range_base_t range_base;
	status_t ret = node->vtbl->slice_alloc(slice, &range_base);
	if (ret)
		return NULL;
	struct item_struct *new_item = range_base_transform(range_base);
	init_item(new_item);
	if (!lvl && !node->sparse_item_root)
		node->sparse_item_root = new_item;

	return new_item;
}

static void delete_group_slice(struct group_struct *group, int lvl, 
		struct item_struct * item)
{
	struct slice_struct *slice = NODE_SLICE_STRUCT_ITEM(group->node);
	range_base_t range_base = range_transform_base(item);

	status_t ret = group->vtbl->slice_free(range_base);
	if (ret)
		return;
	if (!lvl && group->node->sparse_item_root)
		group->node->sparse_item_root = NULL;	
}

struct item_struct * new_group_root(struct group_struct *group, struct item_struct *item, // itemarray
			int lvl, int migration_type)
{
	struct list_node *temp;
	int order_index = MAX_ITEM_RANGE;
	
	if (!item)
		item = new_group_slice(group->node, lvl);
	if (!item)
		return NULL;
	
	temp = &GROUP_CELL(group, migration_type)[order_index];
	list_add_tail(temp, &item->node);
	GROUP_CELLCOUNT(group, migration_type)[order_index]++;

	return item;
}

void delete_group_root(struct group_struct *group, struct item_struct * item, // itemarray
		int lvl, int migration_type)
{
	int order_index = MAX_ITEM_RANGE;

	list_del(&item->node);
	GROUP_CELLCOUNT(group, migration_type)[order_index]--;
	
	clear_item_freeidx(item->prev);

	item->prev->next = NULL;
	item->prev = NULL;

	delete_group_slice(group, lvl, item);
}
		
/* 
 * allocating item
 * 1) Periodic - stream, such as time, periodic-task or sporadic-task
 * 2) Non-periodic(or oneshot) - Non-stream, such as memory, oneshot-task
 *
 * For items requiring continuity, the larger the size, the harder it is to obtain, 
 * so the idx of the item will be large for alignment purposes 
 * 
 * For stream item, the virtual timestamp does not represent the physical 
 * timestamp of the actual runtime, but is determined by priority and other factors. 
 * But the timeline must be incremental, that is, its assigned virtual/physical 
 * timestamps must always be incremental
 * NOTE: GROUP_CELL != IA0[0..n] + IA1[0..n] + ..., IA=itemarray
 *
 */

void merge_group_sparse_table(struct item_struct *item, int order, int level, int migration_type, int dim, 
				bool is_virtual) // item == root
{
	while (item && !sparse_table_free(item, order)) { // level==0, item=nullptr
		remove_sparse_table(item, --level, migration_type, dim, is_virtual);
		item = item->prev;
		order = 0;
	}
}

void refill_group_sparse_table(struct item_struct *item, int order, int level, int migration_type, int dim, 
				bool is_virtual) // item == root
{
	if (sparse_table_free(item, order))
		refill_sparse_table(item, level-1, migration_type, dim, is_virtual);

}

struct item_struct * forward_remove_group_sparse(int lvl, int migration_type, int order, 
		int dim, bool is_virtual) 
{
	int level, order_index;
	struct item_struct *item, *temp, *itemarray;
	status_t ret;
	struct group_struct *group;

	/* level		order		type
	 * current		current		SPARSE_BLOCK -> range align by size
	 * lower		0		SPARSE_TABLE -> 
	 *					1. GROUP_CELL item is empty 
	 *					2. GROUP_CELL item order is smaller
	 * 0			0		SPARSE_BLOCK
	 *	
	 */ 
	/* SPARSE_TABLE 
	 * 1. in freelist: nr_freeidx != 0
	 * 2. in allocat: nr_freeidx == 0
	 * 3. in free/destroy: id_stream && nr_freeidx == BIT(MAX_ITEM_RANGE+1)
	 */
	order_index = order;
	for (level = lvl; level >= 0; --level) { // empty: root->next==nullptr
		group = is_virtual ? get_vgroup_lvl(level, get_current_vnode_num(dim)) :
				get_group_lvl(level, get_current_node_num(dim));
		
		item = remove_group_migration(group, level, order_index, migration_type,
				level == lvl ? false : true);
		
		if (item)
			goto done_forward_remove_group_sparse;
		order_index = 0;
	}
	// level == 0
	item = new_group_root(group, NULL, level, migration_type);
	if (!item)
		goto fail_forward_remove_group_sparse;
	item = forward_remove_group_sparse(migration_type, order, lvl, is_virtual);
	if (!item)
		goto fail_forward_remove_group_sparse;
	return item;
	
done_forward_remove_group_sparse:
	if (level == lvl) { 		// SPARSE_BLOCK, allocat: item->prev==root && root->next==itemarray
		temp = item;
		itemarray = ROUND_DOWN(item, sizeof(struct item_struct) * 
				BIT(MAX_ITEM_RANGE+1));
		temp->prev = itemarray->prev;
		
		// SPARSE_TABLE merge
		merge_group_sparse_table(temp->prev, order, level, migration_type, dim, is_virtual);

	} else { 			// SPARSE_TABLE		
		int curr_order;
		
		for (++level; level <= lvl; ++level) {
			group = is_virtual ? get_vgroup_lvl(level, get_current_vnode_num(dim)) :
					get_group_lvl(level, get_current_node_num(dim));
			curr_order = (level == lvl) ? order : 0;
			itemarray = item->next;
			if (itemarray && itemarray->prev == item) {  // subscribe: item->prev==root&& root->next==itemarray
				printf("test case of coverage\n");
			} else if (!itemarray && !itemarray->prev) { // item: empty, the branch must be new itemarray
				temp = new_group_root(group, NULL, level, migration_type);

				if (!temp)
					goto fail_forward_remove_group_sparse;
				
				item->next = temp;
				set_item_freeidx(item, BIT(MAX_ITEM_RANGE+1));
				temp->prev = item; // itemarray: allocat, itemarray->prev = root

				temp = remove_group_migration(group, level, 
					curr_order, migration_type, 
					level == lvl ? false : true);
			}
			
			// subscribe+allocat, new table
			temp->prev = item;
			refill_group_sparse_table(item, curr_order, level, migration_type, dim, is_virtual);
			item = temp;
		}
	}
	return item;
fail_forward_remove_group_sparse:
	return NULL;

}

struct item_struct * sequence_remove_group_sparse(long idx, int migration_type, int order, 
			int lvl, int dim, struct alloc_para * para)
{
	struct item_struct *root, *itemarray, *root_prev = NULL;
	word_t block_shift, block_mask;
	long idx_index;
	struct node_struct *node;
	struct group_struct *group;
	bool is_virtual = para->is_virtual;
	bool is_entry = para->is_entry;
	
	root = is_entry ? ENTRY_GROUP_ROOT() :
			is_virtual ? PROCESS_GROUP_GROUP_ROOT(dim) :
				NODE_GROUP_ROOT(dim);	

	for (int level = is_entry ? 1 : 0; level < MAX_GROUP_LVLS; ++level) {
		block_shift = is_entry ? FUNCTION_ITEM_LVL_PRIOR(GROUP_PRIOR_NUM_SHIFT, level) :
					FUNCTION_ITEM_LVL(is_virtual ? 
					GROUP_RANGE_NUM_SHIFT : GROUP_ORDER_NUM_SHIFT, level);
		
		block_mask = BIT(block_shift)-1;
		idx_index = idx >> block_shift;
		idx &= block_mask;
		
		itemarray = (struct item_struct *)item_to_range_transform(root);
		if (!itemarray) {
			node = is_virtual ? PROCESS_GROUP_NODE(dim) : get_current_pnode(dim);
			group = &node->group[level];
			itemarray = new_group_root(group, NULL,  
						level, migration_type);
			if (!itemarray)
				return NULL;
			itemarray->prev = root_prev; // itemarray->prev = root
		}
		
		root = &itemarray[idx_index];
		root->prev = root_prev; // subscribe

		if (root_prev) {
			root_prev->next = itemarray;
			set_item_freeidx(root_prev, BIT(is_entry ? 
				GROUP_PRIOR_NUM_SHIFT : MAX_ITEM_RANGE+1));
		}
		
		if (lvl == level)
			break;
		
		root_prev = root;
		root = root->next;
	}

	return root;
}

status_t sequence_forward_group_sparse(struct item_struct *leaf, 
	struct group_struct *group, int lvl, int order, int migration_type, 
	bool is_virtual)
{
	int idx, buddy_idx, combined_idx, order_index, curr_order, level;
	struct item_struct *leaf_buddy, *temp, *leaf_root;
	unsigned long nid = group->node->node_id;
	int dim = group->node->attr.dim;
	
	idx = item_to_rel_idx_sparse(leaf);
	order_index = order;
	level = lvl;
	
	for (;;) {
		temp = leaf;
		for (; order_index < MAX_ITEM_RANGE+1; order_index++) {
			if (list_in_list(&leaf->node))
				goto done_sequence_forward_group_sparse;
			
			buddy_idx = next_buddy_idx(idx, order_index);
			leaf_buddy = leaf + (buddy_idx - idx);
			combined_idx = buddy_idx & idx;
			leaf = leaf + (combined_idx - idx);
			idx = combined_idx;
		}

		printf("subscribed item %p[%d] sequencing fail in %d level\n", temp, order, level);
		return ERR_FAULT;

done_sequence_forward_group_sparse:
		list_del(&leaf->node);
		GROUP_CELLCOUNT(group, migration_type)[order_index]--;
		sequence_split_group_migration(group, leaf, level, order, order_index,
					migration_type, idx);

		leaf_root = temp->prev;
		
		if (!leaf_root && level == 0)
			return NO_ERROR;
		if (!leaf_root && level > 0)
			return ERR_NOT_FOUND;

		curr_order = (level == lvl) ? order : 0;
		/*  
		 * next periodic item: 				op
		 * 1. current itemarray				alloc(order)
		 * 2. next root itemarray			
		 * 3. infinite or finite degrees
		 */		
		if (list_in_list(&leaf_root->node)) {
			merge_group_sparse_table(leaf_root, curr_order, level, migration_type, 
							dim, is_virtual);
			// leaf_root is allocat, break
			break;
		}

		// leaf_root is empty
		leaf = leaf_root;
		group = get_vgroup_lvl(--level, nid);
		order_index = 0;
	}
	return NO_ERROR;
}


/* For periodic item, re-push into the allocat queue based on the timestamp of 
 * the next period, but you need to rearrange the freelist of the root itemarray 
 * for the next period
 */ 
struct item_struct * subscribe_group_sparse(struct item_struct *item, struct group_struct *group, 
	int lvl, int order, int migration_type, struct alloc_para *para)
{
	struct item_struct *leaf;
	status_t ret;
	long lastidx;
	struct node_struct *node;
	bool is_virtual = para->is_virtual;
	bool is_entry = para->is_entry;
	
	int block_shift = is_entry ? FUNCTION_ITEM_LVL_PRIOR(GROUP_PRIOR_NUM_SHIFT, lvl) :
				FUNCTION_ITEM_LVL(is_virtual ? 
				GROUP_RANGE_NUM_SHIFT : GROUP_ORDER_NUM_SHIFT, lvl);
	node = group->node;
	if (item) { 	// item is allocat
		long idx = item_to_idx_sparse(item);
		// minsize = FIXED_ITEM_SIZE, sporadic_period is the count of minsize
		int sporadic_period = item->v_sporadic_period; 

		if (!sporadic_period)
			return NULL;

		lastidx = idx + sporadic_period;
	} else {	// item is nullptr		
		lastidx = node->sequeue_stream_id;
		if (!lastidx) // prior: group index == order
			lastidx = order;
	}

	// subscribe: itemarray->prev==item && item->next==itemarray
	leaf = sequence_remove_group_sparse(lastidx, migration_type, order, lvl, get_item_dim(item), 
						para);
	if (!leaf)
		goto fail_subscribe_group_sparse;

	ret = sequence_forward_group_sparse(leaf, group, lvl, order, migration_type, is_virtual);

	if (ret)
		goto fail_subscribe_group_sparse;
	
	if (!is_entry)
		node->sequeue_stream_id += BIT(order + block_shift);
	
	return leaf;
fail_subscribe_group_sparse:
	return NULL;
}

/* 
 * For stream, the following rules must be ensured when allocating items:
 * 1) Alloc a high order first, then a low order, vice versa. 
 *	Ensure that order increases and decreases by a factor of 2
 * 2) Group shall prevail (generally 4), each level of allocation, must ensure 
 *	that a block(generally 512) allocation is complete, continue 1) mode
 *	For example, the first time you allocate an item of Order 2, 
 *	freecell will have buddy items of order 2, and you will need to 
 *	use up buddy items before you can allocate the rest of the block from freecell
 *		
 *	first order	next order						count
 *	0		(0)							0*0+1=1=f(0)				
 *	1		(1), (0, 0)						1*1+1=2=f(1)
 *	2		(2), (1, 1), (1, 0, 0), (0, 0, 1), (0, 0, 0, 0)		2*2+1=5=f(2)
 *	3		...							5*5+1=26=f(3)			
 *
 *	n									f(n-1)*f(n-1)+1	
 * The above rules can satisfy the static allocat system
 *
 * 3) Additional rules are suitable for dynamic allocat systems:
 * 3.1) Allows sequeue_stream_id not to be increased consecutively,
 * 	Discontinuous gaps may be used for additional purposes, but will expire
 * 3.2) TODO: 3.1
 *
 */
struct item_struct *forward_remove_group_stream(int migration_type, int order, 
		int lvl, int dim, struct alloc_para *para)
{
	struct group_struct *group;
	int level;
	
	bool is_virtual = para->is_virtual;
	bool is_entry = para->is_entry;
	
	level = lvl;
	
	group = is_entry ? &entity_group_struct[level] :
			is_virtual ? get_vgroup_lvl(level, get_current_vnode_num(dim)) :
				get_group_lvl(level, get_current_node_num(dim));

	return subscribe_group_sparse(NULL, group, level, order, migration_type, 
				para);
}


/* 
 * freed item
 * 1) Non-reusable - stream, such as time
 * 2) Reusable - Non-stream, such as memory
 *
 */

void dequeue_group_sparse(struct item_struct *item, int migration_type)
{
	struct list_node *temp, *root;
	int nr_freeidx, block_shift, nitems;
	struct group_struct *group;
	struct node_struct *node;
	bool is_stream;
	bool is_virtual;
	bool is_entry;
	int level;
		
	// item must be SPARSE_BLOCK
	
	int lvl = get_item_section(item);
	int nu = get_item_node(item);
	int order = get_item_order(item);
	
	is_entry = get_item_state(item) == STATE_ITEM_SPARSE_PRIOR;

	if (is_entry) {
		dequeue_group_prior(item, migration_type);
		return;

	}

	is_virtual = get_item_state(item) == STATE_ITEM_SPARSE_VIRTUAL_STREAM;
	/* level		order			type
	 * current		current			SPARSE_BLOCK
	 * lower		0			SPARSE_TABLE
	 *						GROUP_CELL item order is MAX_ITEM_RANGE 
	 * 0			0			SPARSE_BLOCK
	 */
	node = get_vnode_num(nu);
	/* For Periodic stream, the item will be reused in the near future 
	 * To reduce reassignment of the Item, the itemarray it belongs to is 
	 * set to root->next for the next period, while the item itself remains 
	 * in the allocat state, and conversely, for oneshot items, it is in the 
	 * free state. In both cases, the current root's nr_freeidx is updated
	 */
	is_stream = get_item_state(item) == STATE_ITEM_SPARSE_VIRTUAL_STREAM | 
			get_item_state(item) == STATE_ITEM_SPARSE_PHYSICAL_STREAM;
	level = lvl;

	group = get_vgroup_lvl(lvl, nu);
	
	for (;;) {
		temp = free_group_migration(item, group, lvl, 
					order, migration_type);		
		if (level == lvl) {

			if (is_stream)
				temp = subscribe_group_sparse(item, group, 
					lvl, order, migration_type, is_virtual);
			else {
				block_shift = FUNCTION_ITEM_LVL(GROUP_RANGE_NUM_SHIFT, lvl);
				nitems = BIT(order + block_shift);
				node->attr.nr_freeitems += nitems;
			}
		}

		// update root freeidx
		root = item->prev;
		nr_freeidx = get_item_freeidx(root);
		nr_freeidx += BIT(order);
		set_item_freeidx(root, nr_freeidx);

		
		if (nr_freeidx == BIT(MAX_ITEM_RANGE+1)) {
			assert (root->next == temp);
			if (is_stream)
				delete_group_root(group, temp, lvl, migration_type);				
		} else {
			break;
		}

		// item must be SPARSE_TABLE, participation in buddy merge, root is in freelist
		item = root;
		group = get_vgroup_lvl(--lvl, nu);
		order = 0;
		list_del(&item->node);
		GROUP_CELLCOUNT(group, migration_type)[order]--;
	} 
}

//-------------------

// [prior]

struct item_struct *forward_remove_group_prior(int lvl, int migration_type, int order, 
	struct node_struct *node)
{
	struct group_struct *group;
	struct list_node *temp;
	struct item_struct *item;

	assert(item);
	
	group = &entity_group_struct[lvl];

	temp = &GROUP_CELL(group, migration_type)[order];
	list_add_tail(temp, &item->node);
	GROUP_CELLCOUNT(group, migration_type)[order]++;

	set_item_present(item);
	set_item_order(item, order);
	set_item_section(item, lvl);
	set_item_node(item, node->node_id);
	

	return item;
}

void dequeue_group_prior(struct item_struct *item, int migration_type)
{
	int level = get_item_section(item);
	int order = get_item_order(item);
	struct group_struct *group = &entity_group_struct[level];

	list_del(&item->node);
	GROUP_CELLCOUNT(group, migration_type)[order]--;
	
	clear_item_present(item);
	clear_item_order(item);
	clear_item_section(item);
	clear_item_node(item);
}

status_t migrate_group_cell()
{

}

status_t migrate_group_migration()
{

}

status_t traverse_group_sparse(int migration_type, int lvl, int lvl_end)
{
	
}

status_t reschedule(void)
{
	// 1) traverse_group_sparse
	// 2) migrate_group_migration
	// 3) migrate_group_cell
}

// ------------------



/* TODO: lru_cache free to balance for each core */
status_t find_group_reversed(int migration_type, struct item_struct *item, 
	bool is_virtual)
{
	struct item_struct *item_ptr = item, *tmp_item_ptr;
	struct node_struct *node;
	int node_id; 

	node_id = get_item_node(item_ptr);
	if (is_virtual)
		node = get_vnode_num(node_id);
	else
		node = get_pnode_num(node_id);

	/* Migrate the memory item from MOVABLE to the object of the specified
	 * migration type(LRU_CACHE), and delay the return to MOVABLE by LRU_CACHE count
	 */
	if (migration_type == MIGRATION_LRUTYPE) {
		list_add_head(&item_ptr->node,
			     &LRU_CACHE_CELL(node)); // free hot item
		LRU_CACHE_CELLCOUNT(node)++;
		if (LRU_CACHE_CELLCOUNT(node) < LRU_CACHE_MAX_ITEMS)
			goto final_reversed;
		tmp_item_ptr = list_tail_entry(&LRU_CACHE_CELL(node), tmp_item_ptr, node);
		// coverity[var_deref_model:SUPPRESS]
		list_del(&tmp_item_ptr->node); // tail = cold
		LRU_CACHE_CELLCOUNT(node)--;
		item_ptr = tmp_item_ptr;
		// TO DEBUG
		// migration_type = MIGRATION_MOVABLE;
	}

	if (migration_type == MIGRATION_SPARSE_MOVABLE) 
		dequeue_group_sparse(item_ptr, migration_type);
	else
		dequeue_group_migration(item_ptr, migration_type);

final_reversed:
	return NO_ERROR;
}

/* [level==0]
 * All nodes are sorted in workload from smallest to largest, 
 * and 'order' is the workload level.
 * The workload at the upper level needs to be migrated to the lower level, 
 * and the same level does not need to be migrated.
 */
struct node_struct *get_minload_pnode(int migration_type)
{
	// level == 0, group
	struct group_struct *group = entity_group_struct;
	// 'order'== 0
	int order = 0;
	struct list_node *list = &GROUP_CELL(group, migration_type)[order];
	// smallest workload == list head
	struct node_struct *pnode = list_head_entry(list, pnode, node);
	
	return pnode;
}

// for compound item
status_t find_group(word_t nitems, int migration_type, int lvl,
		  int lvl_end, struct item_struct **item, struct alloc_para *para,
		  int dim)
{
	int lvl_order;
	int level;
	int block_shift;
	word_t block_mask;
	word_t block_size;
	word_t lvl_nitem;
	struct item_struct *tmp_item_ptr;
	struct item_struct *item_ptr = NULL;
	struct node_struct *node;
	status_t ret;
	bool is_combined, is_virtual, is_entry; 

	is_combined = para->is_combined;
	is_virtual = para->is_virtual;
	is_entry = para->is_entry;

	/* e1) The node number is assigned based on the actual situation of all 
	 * physical nodes (current free capacity, node distance and other weights)
	 * The load on all nodes must be approximately the same
	 */
	/*
	 * choose node of allocat
	 */
	node = is_entry ? get_minload_pnode(migration_type): // Workload first
		is_virtual ? get_recent_vnode(dim) : // Distance first
			get_current_pnode(dim);

	for (level = is_entry ? lvl+1 : lvl; level < lvl_end; ++level) {
		block_shift = is_entry ? FUNCTION_ITEM_LVL_PRIOR(GROUP_PRIOR_NUM_SHIFT, level) :
				FUNCTION_ITEM_LVL(migration_type == MIGRATION_SPARSE_MOVABLE ? 
					GROUP_RANGE_NUM_SHIFT : GROUP_ORDER_NUM_SHIFT, level);

		block_size = BIT(block_shift);
		block_mask = block_size - 1;
		lvl_nitem = nitems >> block_shift;
		nitems &= block_mask;

		if (!lvl_nitem)
			continue;

		do {
			lvl_order = is_entry ? lvl_nitem :
					is_combined ? item_to_orders(&lvl_nitem) :
						item_to_order(&lvl_nitem);

			/* e2) The sequence number is assigned according to the 
			 * priority weight of the current node
			 */
			ret = node->vtbl->dispatch(node, &tmp_item_ptr, level,
							  migration_type,
							  lvl_order, para);
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
		      struct alloc_para *para, int dim)
{
	struct item_struct *item_ptr = NULL;

	if (migration_type < 0 || migration_type >= MIGRATION_TYPES)
		return ERR_INVALID_ARGS;

	int lvl = migration_type == MIGRATION_SPARSE_MOVABLE ? 0 : 
			phy_group_lvl[get_current_node_num(dim)];
	status_t ret = find_group(nitems, migration_type,
				lvl, GROUP_LVL_NUM,
				&item_ptr, para, dim);

	if (ret)
		return ret;
	if (item_pptr)
		*item_pptr = item_ptr;

	return NO_ERROR;
}

status_t decompose_group(struct item_struct *item_pptr, bool is_virtual)
{
	struct item_struct *item_ptr = item_pptr;

	int migration_type = item_state_to_migration(get_item_state(item_pptr));

	if (migration_type < 0 || migration_type >= MIGRATION_TYPES)
		return ERR_INVALID_ARGS;

	status_t ret = find_group_reversed(migration_type, item_ptr, is_virtual);
	return ret;
}

// [only physical]

status_t migrate_group(struct node_struct *const self, struct item_struct **item_pptr,
		     int order, int lvl, int type, bool is_combined)
{
	struct node_struct *const node = self;
	struct item_struct *tmp_item_ptr;

	switch (type) {
	case MIGRATION_LRUTYPE: {
		status_t ret = node->vtbl->dispatch(
			node, &tmp_item_ptr, lvl, MIGRATION_MOVABLE, order, is_combined, false);
		if (ret)
			return ret;

		// not do that list append, but free to diff pos
		set_item_state(tmp_item_ptr, STATE_ITEM_LRU_CACHE_TYPES);
		break;
	}

	case MIGRATION_UNMOVABLE: {
		node->attr.status = GROUP_ONLY_FORWARD;

		status_t ret = node->vtbl->dispatch(
			node, &tmp_item_ptr, lvl, MIGRATION_MOVABLE, order, is_combined, false);
		if (ret)
			return ret;
		set_item_state(tmp_item_ptr, STATE_ITEM_NOMOVABLE_TYPES);
		break;
	}

	case MIGRATION_MOVABLE: {
		status_t ret = node->vtbl->lend(&tmp_item_ptr, MIGRATION_MOVABLE, lvl,
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

// [only physical]

status_t repeating_dispatch(struct item_struct **item_pptr, struct node_struct *node,
			    int lvl, int migration_type, word_t nitems,
			    bool is_combined)
{
	struct item_struct *tmp_item_ptr, *item_ptr = NULL;
	int order;
	status_t ret;

	while (nitems) {
		order = is_combined ? item_to_orders(&nitems) :
				order = item_to_order(&nitems);

		ret = node->vtbl->dispatch(node, &tmp_item_ptr, lvl, migration_type,
					 order, is_combined, false);
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

// [only physical]

status_t lend_group(struct item_struct **item_pptr, int migration_type, int lvl,
		  int lvl_order, bool is_combined)
{
	struct item_struct *tmp_item_ptr, *item_ptr = NULL;
	struct node_struct *node;
	int i;
	status_t ret;
	int block_shift;
	long nr_items, pn_freeitem;

	block_shift = FUNCTION_ITEM_LVL(GROUP_ORDER_NUM_SHIFT, lvl);
	nr_items = BIT(lvl_order + block_shift);

	for (i = 0; i < PNODE_NUM && nr_items; i++) {
		node = get_pnode_num(i);
		pn_freeitem = node->attr.nr_freeitems;
		if (!pn_freeitem)
			continue;

		if (nr_items > pn_freeitem) {
			if (!is_combined)
				continue;
			ret = repeating_dispatch(&tmp_item_ptr, node, lvl,
						 migration_type, pn_freeitem,
						 is_combined);
			if (ret)
				return ret;
			nr_items -= pn_freeitem;
		} else {
			ret = repeating_dispatch(&tmp_item_ptr, node, lvl,
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
}


// [only physical]

bool budget_sufficient(long nitems, int migration_type, int dim_type)
{
	struct node_struct *node;
	long remaining_nitem = 0;
	int dim_base = dim_type * NODE_LVL_NUM;

	for (int i = 0; i < NODE_LVL_NUM; i++) {
		node = get_pnode_num(dim_base + i);
		nitems -= node->attr.nr_freeitems;
		if (nitems < 0)
			return true;
	}

	if (migration_type == MIGRATION_LRUTYPE)
		remaining_nitem = LRU_CACHE_CELLCOUNT(node);
	else if (migration_type == MIGRATION_UNMOVABLE)
		remaining_nitem = node_budget(migration_type, dim_type);
	if (remaining_nitem >= nitems)
		return true;
	return false;
}


// [only physical]
// lru_cache & migration

status_t node_struct_migrate(struct node_struct *const self,
				struct item_struct **item_pptr, int order, int lvl,
				int type, bool is_combined)
{
	return migrate_group(self, item_pptr, order, lvl, type, is_combined);
}


static status_t group_struct_compound(struct item_struct **item_pptr, word_t nitems,
					int migration_type, struct alloc_para *para, int dim)
{
	return compound_group(item_pptr, nitems, migration_type, para, dim);
}

static status_t group_struct_decompose(struct item_struct *item_pptr, bool is_virtual)
{
	return decompose_group(item_pptr, is_virtual);
}

static void
group_struct_dump_migration(struct group_struct *const self, int type, int migration_type)
{
	struct group_list *group_list;
	
	int item_shift = migration_type == MIGRATION_SPARSE_MOVABLE ? 
		GROUP_RANGE_NUM_SHIFT : GROUP_ORDER_NUM_SHIFT;
	
	group_list = &self->free_group[type];
	
	for (int order = 0; order < FUNCTION_ITEM(item_shift)-1; order++) {
		if (group_list->free_count[order])
			printf(
				"[type, cell, order, count]:%d, %p, %d, %ld\n",
				type, &group_list->free_cell[order], order,
				group_list->free_count[order]);
	}
}

static status_t group_struct_dump_group(struct group_struct *const self, , int migration_type)
{
	for (int type = 0; type < MIGRATION_TYPES; type++)
		group_struct_dump_migration(self, type, migration_type);

	return NO_ERROR;
}

static status_t node_struct_dispatch(struct node_struct *const self,
					struct item_struct **item_pptr, int lvl,
					int migration_type, int order,
					bool is_combined, bool is_virtual,
					bool is_stream)
{
	return dispatch_group(self, item_pptr, lvl, migration_type, order, is_combined, 
			is_virtual, is_stream);
}

static status_t node_struct_init(struct node_struct *const self, range_base_t range, size_t size, 
			bool virtual)
{
	if (virtual)
		return init_vnode(self, range, size);
	else
		return init_pnode(self, range, size);
}

// [only physical]

static status_t node_struct_lend(struct item_struct **item_pptr, int migration_type,
				    int lvl, int lvl_order, bool is_combined)
{
	return lend_group(item_pptr, migration_type, lvl, lvl_order, is_combined);
}


ssize_t preprocess_node_struct_alloc(int flags, word_t nitems,
					int migration_type, int dim)
{
	word_t tmp_nitems = nitems;

	switch (flags) {
	case FUNC_PHYSICAL_RESERVED: {
		if (tmp_nitems == node_budget(migration_type))
			break;
		for (int i = phy_group_lvl[get_current_node_num(dim)]; i < MAX_GROUP_LVLS; i++)
			if (tmp_nitems == node_budget_lvl(migration_type, i))
				break;
		item_to_orders(&tmp_nitems);
		if (!tmp_nitems)
			break;
		return ERR_INVALID_ARGS;
	}
	case FUNC_PHYSICAL_DEBUG: {
		word_t node_nitem = node_budget(migration_type);

		if (node_nitem > UNMOV_MAX_ITEMS || tmp_nitems > UNMOV_MAX_ITEMS)
			return ERR_INVALID_ARGS;
		item_to_orders(&tmp_nitems);
		if (tmp_nitems)
			return ERR_INVALID_ARGS;
		break;
	}
	case FUNC_PHYSICAL_CONT:
	case FUNC_PHYSICAL_MMU:
	case FUNC_PHYSICAL_OBJECT:
	case FUNC_PHYSICAL_STREAM:
	case FUNC_VIRTUAL:
	case FUNC_VIRTUAL_STREAM: {
		nitems = BIT(item_to_order(&tmp_nitems));
		break;
	}
	case FUNC_ENTITY:
	default:
		break;
	}
	return nitems;
}


// owner or borrower - static allocat
/* nitems:
 *
 * 1) group index = (size, range) -> size = BIT(group index), size = nitems
 * 2) group index = (prior) -> size = group index, size = 1
 * 3) ..
 *
 */

/* node is allocat or structure */
static status_t node_struct_alloc(struct node_struct *node, range_base_t *range_base,
				     word_t nitems, int flags, struct in_paras *paras)
{
	struct item_struct *item_pptr;
	int migration_type;
	bool is_combined, is_virtual, is_stream, is_entry;
	ssize_t psize;
	bool suff;
	int dim_type;
	
	if (!nitems && flags != FUNC_ENTITY) {
		printf("nitems args is zero\n");
		return ERR_INVALID_ARGS;
	}

	prepare_node_struct_alloc(nitems, flags, &migration_type, 
		&is_combined, &is_virtual, &is_stream, &is_entry);

	spin_lock_saved_state_t sflags;

	spin_lock_irqsave(&node->node_struct_lock, sflags);

	dim_type = node->attr.dim;
	psize = preprocess_node_struct_alloc(flags, nitems, migration_type, dim_type);
	if (psize <= 0) {
		spin_unlock_irqrestore(&node->node_struct_lock, sflags);
		printf("args parse is error, nitems is %ld, flags is %d, migration_type is %d, is_combined is %d\n",
		       psize, flags, migration_type, is_combined);
		return ERR_INVALID_ARGS;
	}

	// watermark
	if (migration_type != MIGRATION_SPARSE_MOVABLE) // virtual is Unlimited 
		suff = budget_sufficient(psize, migration_type, dim_type);
	else
		suff = true;
	
	if (!suff) {
		spin_unlock_irqrestore(&node->node_struct_lock, sflags);
		printf("budge is zero, nitems args is %ld\n", psize);
		return ERR_NO_MEMORY;
	}
	struct alloc_para para = {
		.is_combined = is_combined;
		.is_stream = is_stream;
		.is_virtual = is_virtual;
	};

	if (is_entry) {
		para->affinity = paras->affinity;
		para->max_prior = paras->max_prior;
		para->is_entry = true;
	}

	status_t ret =
		node->group_vtbl->compound(&item_pptr, psize, migration_type, 
		&para, dim_type);
	spin_unlock_irqrestore(&node->node_struct_lock, sflags);

	if (ret)
		return ret;

	if (range_base) {
		if (migration_type == MIGRATION_SPARSE_MOVABLE)
			*range_base = item_to_range_sparse(item_pptr);
		else
			*range_base = item_to_range(item_pptr);
	}

	if (paras)
		postprocess_node_struct(node, item_pptr, paras, is_virtual);
	
	return NO_ERROR;
}

// owner or borrower - static allocat
static status_t node_struct_free(range_base_t range_base, bool is_virtual, int dim)
{
	long idx = range_to_idx(range_base);
	struct node_struct *node;
	struct item_struct *item;

	if (is_virtual)
		node = idx_to_vnode(idx, dim);
	else
		node = idx_to_pnode(idx, dim);

	if (!node) {
		printf("range_base to node is error, range_base is 0x%lx\n", range_base);
		return ERR_INVALID_ARGS;
	}

	spin_lock_saved_state_t flags;

	spin_lock_irqsave(&node->node_struct_lock, flags);

	item = node->sparse_item_root ? range_to_item_sparse(range_base, dim) : 
			range_to_item(range_base, dim);
	status_t ret = node->group_vtbl->decompose(item, is_virtual);

	spin_unlock_irqrestore(&node->node_struct_lock, flags);

	return ret;
}

// DUMP STRUCT
void node_struct_dump_lru_cache(struct node_struct *const self)
{
	struct node_list *node_list;
	int node_num = NODE_LVL_NUM;
	
	node_list = &self->lru_cache;
	for (int nid = 0; nid < node_num; nid++) {
		if (node_list->free_count[nid])
			printf("[pcplist, pcpcount]: %p, %ld\n",
				  &node_list->free_cell[nid],
				  node_list->free_count[nid]);
	}
}

void node_struct_dump_group(int nid, bool is_virtual, int migration_type, int dim)
{
	struct group_struct *group;

	int lvl = migration_type == MIGRATION_SPARSE_MOVABLE ? 0 : 
		phy_group_lvl[get_current_node_num(dim)];
	
	for (; lvl < GROUP_LVL_NUM; lvl++) {

		if (is_virtual)
			group = get_vgroup_lvl(lvl, nid);
		else
			group = get_group_lvl(lvl, nid);

		printf("[group, lvl]: %p, %d\n", group, lvl);
		group->vtbl->dump_area(group, migration_type);
	}
}

void vnode_struct_dump_item(struct node_struct *const self)
{
	// many level, and each level, need to find SPARSE_TABLE, tree traverse
}

void node_struct_dump_item(struct node_struct *const self, bool is_virtual)
{
	int nid = self->node_id;

	if (!is_virtual)
		printf(
			"[pg_start %p, pg_end %p, pg_align %p, entry_lvl %d, entry_order %d]\n",
			phy_item_struct[nid], phy_item_struct_end[nid], phy_item_struct_of[nid],
			phy_group_lvl[nid], phy_group_order[nid]);
	else
		vnode_struct_dump_item(self);

}

static status_t node_struct_dump_node(struct node_struct *const self, bool is_virtual)
{
	spin_lock_saved_state_t sflags;

	spin_lock_irqsave(&self->node_struct_lock, sflags);

	int nid = self->node_id;
	int dim = self->attr.dim;
	int sparse_type = self->sparse_item_root ? MIGRATION_SPARSE_MOVABLE: MIGRATION_TYPES;
	printf(
		"[nid %d,  0x%lx, end 0x%lx, status %ld, nr_freeitems 0x%lx]\n",
		nid, self->attr., self->attr.size, self->attr.status,
		self->attr.nr_freeitems);
	
	node_struct_dump_lru_cache(self);
	node_struct_dump_group(nid, is_virtual, sparse_type, dim);
	node_struct_dump_item(self, is_virtual);

	spin_unlock_irqrestore(&self->node_struct_lock, sflags);
	return NO_ERROR;
}

// [item to slice]
enum {
	SLICE_EMPTY = 0,
	SLICE_FREE = 1,
	SLICE_FULL = 2,
};

status_t heap_alloc(struct slice_struct *slice, range_base_t *range_base)
{
	struct item_struct *item;
	int dim = DIM_MEMORY;
	assert(slice && !(slice & 0x2));
	assert(slice->bsize >= SLICE_STRUCT_MIN_BSIZE);

	// 1. free 2. empty
	if (slice->free.item_count) {
		item = list_head_entry(&slice->free.item_list, item, node);
		goto valid_free_slice;
	}
	if (slice->empty.item_count) {
		item = list_head_entry(&slice->empty.item_list, item, node);
		goto valid_empty_slice;
	}

	struct node_struct *node = get_current_pnode(dim);
	range_base_t range_base;
	status_t ret = node->vtbl->alloc(node, &range_base, FIXED_ITEM_SIZE, 
			FUNC_PHYSICAL_OBJECT, NULL);
	if (ret)
		return ERR_NO_MEMORY;

	item = range_to_item(range_base, dim);
	
	init_item(item);
	item->slice_head = slice;
	item->slice_stat = SLICE_EMPTY;
	list_add_tail(&slice->free.item_list, &item->node);
	slice->free.item_count++;

valid_free_slice:
valid_empty_slice:

	size_t size = FIXED_ITEM_SIZE / slice->bsize;
	unsigned int idx = 0;
	for_each_clear_bit(idx, &item->slice_ref, size) {
		set_bit(idx, &slice->ref);
		break;
	}
	if (range_base)
		*range_base = item_to_range(item) + idx * slice->bsize;
	// 1. free->full, 2. empty->free
	if (item->slice_stat == SLICE_EMPTY) {
		item->slice_stat = SLICE_FREE;
		list_del(&item->node);
		slice->empty.item_count--;
		list_add_tail(&slice->free.item_list, &item->node);
		slice->free.item_count++;
	}

	if (item->slice_stat == SLICE_FREE && find_first_zero_bit(&item->slice_ref, size) == size) {
		item->slice_stat = SLICE_FULL;
		list_del(&item->node);
		slice->free.item_count--;
		list_add_tail(&slice->full.item_list, &item->node);
		slice->full.item_count++;
	}

	return NO_ERROR;
}

status_t heap_free(range_base_t range_base)
{
	struct item_struct *item = range_to_item(range_base, DIM_MEMORY);
	assert(item && item_node_mapped(item));
	struct slice_struct *slice = item->slice_head;
	assert(slice && !(slice & 0x2));

	unsigned int idx;
	
	idx = range_base % FIXED_ITEM_SIZE;
	idx = idx / slice->bsize;
	
	if (!is_set_bit(idx, &item->slice_ref))
		return ERR_INVALID_ARGS;
	clear_bit(idx, &item->slice_ref);

	size_t size = FIXED_ITEM_SIZE / slice->bsize;

	// 1. free 2. full
	// 1. free->empty 2. full->free
	if (item->slice_stat == SLICE_FULL) {
		item->slice_stat = SLICE_FREE;
		list_del(&item->node);
		slice->full.item_count--;
		list_add_tail(&slice->free.item_list, &item->node);
		slice->free.item_count++;
	}
	
	if (item->slice_stat == SLICE_FREE && !find_first_bit(&item->slice_ref, size)) {
		item->slice_stat = SLICE_EMPTY;
		list_del(&item->node);
		slice->free.item_count--;
		item->slice_head = NULL;
		list_add_tail(&slice->empty.item_list, &item->node);
		slice->empty.item_count++;	
		if (slice->empty.item_count > slice->threshold_freeitems) {
			list_del(&item->node);
			slice->empty.item_count--;	
		
			struct node_struct *node = get_pnode_num(get_item_node(item)); 
			range_base_t range_base = item_to_range(item);

			status_t ret = node->vtbl->free(range_base, false);
			return ret;
		}
	}

	return NO_ERROR;
}

// --------------------------------------------------------------


// [virtual to physical]

void postprocess_node_struct(struct node_struct *node, 
		struct item_struct *in, 
		struct in_paras *paras, 
		bool is_virtual)
{
	int dim_type = node->attr.dim;
	struct item_struct *item = in;

	if (is_virtual) {	
		switch (dim_type) {
		case DIM_TIME:
			in->v_deadline_arrive = paras->deadline_time; // init
			in->v_lazy_allocat = paras->delay_time;			
			in->v_sporadic_period = paras->sporadic_period;
			break;
		case DIM_MEMORY:
			in->v_map_right = (paras->map_prot & 0xffff) << 16 | 
					  (paras->memory_attr & 0xffff);
			in->v_ownership = paras->ownership;
			in->v_ref_count++;
			break;
		}
	} else {
		switch (dim_type) {
		case DIM_TIME:
			in->p_last_arrive = paras->arrive_time;
			in->p_switch_time = paras->switch_time;
			in->p_yield_time = paras->yield_time;
			in->p_msp = paras->max_prior;
			break;
		case DIM_MEMORY:
						
			break;
		}
	}

	
}

static status_t node_struct_paras_update(struct node_struct *const self, 
						struct item_struct *in, // alloc-v
						struct in_paras *paras) // update by event
{
	struct item_struct *v_aging_head, *output_head;
	status_t ret;
	
	// DIM_TIME->Serial_No_item, DIM_*->DIM_TYPE
	v_aging_head = in->v_aging_head;
	if (!v_aging_head)
		return NO_ERROR;

	range_base_t out_idx;
	range_base_t in_idx;
	size_t item_size;

	in_idx = item_to_range(in);
	ret = self->vtbl->map(self, in_idx, &out_idx);
	if (ret)
		goto map_fail_item;
	// TODO: in->out, in->in_idx->out_idx->out
	output_head = range_to_item(out_idx); 
	
	item_size = BIT(get_item_order(output_head));
	if (paras->allocat_size != item_size) {
		range_base_t range_base = item_to_range(output_head);
		ret = self->vtbl->free(range_base);
		if (ret)
			goto free_fail_item;
	} else {
	// 更新参数
		ret = put_pitem_struct(in, output_head, self, paras);
		goto update_item;
	}

	return NO_ERROR;

map_fail_item:	
free_fail_item:
update_item:
	return ERR_FAULT;
}


// 为某个输入item（any dim type）和序列化参数， 生成待转置的参数item，该参数item只为DIM_TIME生成，其他类型的dim只需绑定DIM_TIME
// 预分配资源+序列化=资源执行流
static status_t node_struct_paras_gen(struct node_struct *const self, 
						struct item_struct *in, // alloc-v
						struct in_paras *paras, // update by event
						struct item_struct **out) // usage-v maybe new, maybe reuse
{
	// 序列化， 这里可能存在同步（线性点）的抽象， 动态
	range_base_t range_base = 0;
	struct node_struct *node_out;
	status_t ret;
	struct item_struct *serial_head, *output_head;

	assert(paras);



	// multi-node??
	ret = self->vtbl->alloc(self, &range_base, paras->allocat_size, FUNC_TRANS); // 可以是组合式
	// nitems = serial no val
	if (ret)
		goto alloc_fail_item;
	output_head = range_to_item(range_base);

	// get DIM_TIME item
	struct node_struct *node = get_time_node_by_node(self);
	spin_lock_saved_state_t sflags;

	spin_lock_irqsave(&node->node_struct_lock, sflags);
	struct list_node *node_map = node->node_map->free_cell[node->node_id];
	serial_head = list_head_entry(node_map, serial_head, node);
	spin_unlock_irqrestore(&node->node_struct_lock, sflags);
	
	in->v_aging_head = serial_head;
	if (out)
		*out = output_head;

	self->attr.node = node_out;
	return NO_ERROR;



alloc_fail_item:

	return ERR_FAULT;
}


void node_struct_benchmark(int dim_type, struct in_paras *in)
{
	// 参数归一化，参考基准值来进行参数转换,不同节点的基准值一致

	// in % para_benchmark
	for (int idx = 0; idx < VECTOR_NUM; idx++) {
		in->word[idx] = in->word[idx] % para_benchmark[dim_type].word[idx];
	}
}

// 输入转换参数， 输出各类机制确定的参数
static status_t node_struct_sched(struct node_struct *const self,
				struct in_paras *in, struct in_paras *out)
{
	// 参数转换
	return self->vtbl->sched(self, node_struct_benchmark(self->attr.dim, in), out); // 不同的调度策略
}


/* Step:
 * 1) task set[0..n] with some constraint(c) -> virtual node state limit value
 * 2) per-task workload splitting(DAG .etc) 
 * 3) 'parameter vectors' (in_paras .etc) 
 * 4) per-task virtual node with item set as 'in'				  
 * 5) event-driven with (f) - vno:pno -> 1:1			  
 * 6) per-node physical node with item set as 'out' + 		  
 * 7) physical and virtual node state set as 'state' -> physical node state == system node state
 * 8) 7) feedback to 1)
 * 
 * NO.:
 * 1) single-node(serial_no) 
 * 2) multi-node(parallel_no)	
 *
 * Parameter:
 * 1) 'in' - batch item of compound(multi-node, per-task)
 * 2) 'out' - batch item of compound(multi-node, phsical node)
 */
// 对不同维度，不同节点类型，不同节点数目的分配之用，
// 而维度/节点类型/节点数目之间的变换关系，则由transform:F原语确定
// such as DIM_MEMORY and DIM_TIME
// physical time -> virtual time: time priority map
// physical addr -> virtual addr: memory mmu map
// vaddr<item> = F(paddr<item>, delay, ...), F是内存映射关系
// vtime<item> = F(ptime<item>, prior, ...), F是调度关系
// F define - TRP: Transform Relationship
// vtime<item> @ prior<item> = ptime<item>
// vaddr<item> @ delay<item> = paddr<item>
// 在某个维度下， 输入某个虚拟节点， 参数节点约束， 输出某个物理节点

status_t node_struct_trans_relat(struct item_struct *in,
					struct item_struct **out,
					struct out_state *state)
{
	struct node_struct *node_in;
	struct in_paras para = 0;
	status_t ret;
	spin_lock_saved_state_t sflags;

	
	
	
	
	node_in = get_vnode_num(get_item_node(in));
	
	spin_lock_irqsave(&node_in->node_struct_lock, sflags);

	// 根据输入参数， 输出调度出的资源动态分配参数
	ret = node_struct_sched(node_in, paras, &para);
	if (ret)
		goto serial_fail_conf;

	// 更新输入item以及对应的输出item
	ret = node_struct_paras_update(node_in, in, paras);
	if (ret)
		goto updat_fail_item;
	
	// 静态分配+序列化/并行化参数->动态分配：方式一
	ret = node_struct_paras_gen(node_in, in, &para, out);
	if (ret)
		goto serial_fail_alloc;

	spin_unlock_irqrestore(&node_in->node_struct_lock, sflags);

	// iteration the items
	
serial_fail_alloc:	
serial_fail_conf:
updat_fail_item:

	spin_unlock_irqrestore(&node_in->node_struct_lock, sflags);
	return ERR_FAULT;
}

// --------------------------------------------------------

