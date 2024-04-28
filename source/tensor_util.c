#include "tensor_ops.h"

extern void dump_item(status_t ret);
void *kmalloc(size_t size)
{
	void *range_transform;
	status_t ret;

	if (!size)
		return NULL;

	range_base_t range_base = 0;
	struct node_struct *node = get_current_pnode(DIM_MEMORY);
	int gfp_flags = FUNC_PHYSICAL_OBJECT;

	ret = node->vtbl->alloc(node, &range_base, size_to_item(size),
				gfp_flags, NULL);
	if (ret) {
		dump_item(ret);
		return NULL;
	}

	range_transform = (void *)range_base_transform(range_base);
	return range_transform;
}

int kfree(void *object)
{
	status_t ret;

	if (!object)
		return ERR_INVALID_ARGS;

	struct node_struct *node = get_current_pnode(DIM_MEMORY);
	struct item_struct *page = range_transform_to_item(object, DIM_MEMORY);

	range_base_t range_base = item_to_range(page);

	ret = node->vtbl->free(range_base, false, DIM_MEMORY);
	if (ret)
		dump_item(ret);

	return ret;
}

void dump_item(status_t ret)
{
	struct node_struct *node;

	for (int i = 0; i < NODE_DIM_NUM; i++) {
		node = get_current_pnode(i);
		node->vtbl->dump_node(node, true);
	}

	printf("pma page-caller retv is %d\n", ret);

	for (int i = 0; i < NODE_DIM_NUM; i++) {
		node = get_recent_vnode(i);
		node->vtbl->dump_node(node, false);
	}
}

