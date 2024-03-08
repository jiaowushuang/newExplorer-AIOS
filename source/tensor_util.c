#include "tensor.h"

void *kmalloc(size_t size)
{
	void *vaddr;
	status_t ret;

	if (!size)
		return NULL;


	paddr_t paddr = 0;
	struct node_struct *pn = get_recent_pn();
	int gfp_flags = FUNC_OBJECT;

	ret = pn->vtbl->alloc(pn, &paddr, size_to_grains(size),
				gfp_flags);
	if (ret) {
		dump_grains(ret);
		return NULL;
	}

	vaddr = (void *)paddr_to_kvaddr(paddr);
	return vaddr;
}

int kfree(void *object)
{
	status_t ret;

	if (!object)
		return ERR_INVALID_ARGS;

	struct node_struct *pn = get_recent_pn();
	struct grain_struct *page = virt_to_page(object);

	paddr_t paddr = page_to_paddr(page);

	ret = pn->vtbl->free(paddr);
	if (ret)
		dump_grains(ret);

	return ret;
}

void dump_grains(status_t ret)
{
	struct node_struct *pn = get_recent_pn();

	printf("pma page-caller retv is %d\n", ret);
	pn->vtbl->dump_node(pn);
}

