#include "tensor_ops.h"

extern void dump_item(status_t ret);
void *kmalloc(size_t size)
{
	void *range_transform;
	status_t ret;

	if (!size)
		return NULL;


	range_base_t range_base = 0;
	struct node_struct *pnode = get_recent_pnode();
	int gfp_flags = FUNC_OBJECT;

	ret = pnode->vtbl->alloc(pnode, &range_base, size_to_item(size),
				gfp_flags);
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

	struct node_struct *pnode = get_recent_pnode();
	struct item_struct *page = range_transform_to_item(object);

	range_base_t range_base = item_to_range(page);

	ret = pnode->vtbl->free(range_base);
	if (ret)
		dump_item(ret);

	return ret;
}

void dump_item(status_t ret)
{
	struct node_struct *pnode = get_recent_pnode();

	printf("pma page-caller retv is %d\n", ret);
	pnode->vtbl->dump_node(pnode);
}

