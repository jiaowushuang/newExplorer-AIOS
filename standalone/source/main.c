#include <types.h>
#include <stddef.h>
#include <stdio.h>

extern void mm_init(void);
extern void *kmalloc(size_t size);
extern int kfree(void *object);

#define PM_LX_X_TEST(page_shift, level) ((4 - (level)) * ((page_shift)-3) + 3 - (page_shift))

int main(int arg, char **args)
{
	int shift;
	size_t size = 0;

	mm_init();

	for (int l = 1; l < 4; l++) {
		shift = PM_LX_X_TEST(12, l);
		for (int i = 0; i < 9; i++) {
			unsigned long npages = 1 << (shift + i);
			size += 4096 * npages;
			if (l == 1)
				break;
			if (l == 3) {
				printf("--------%d--------\n", l);
				kmalloc(size);
				size = 0;
				void *obj1 = kmalloc(4096);
				kmalloc(4096);
				kmalloc(4096);
				kmalloc(4096);
				void *obj5 = kmalloc(4096);
				kfree(obj1);
				kfree(obj5);

				for (int j = 2; j < 9; j++)
					size += 4096 * (1 << (shift + j));

				void *obj2 = kmalloc(size);
				kfree(obj2);
				printf("4444\n");

				if (obj1) {
					printf("npages %ld lvl %d order %d, ptr %p\n", npages, l, i, obj1);
					// printf("npages %ld lvl %d order %d, ptr %p\n", npages, l, i, obj2);
					kfree(obj1);
					// kfree(obj2);
					void *obj1 = kmalloc(size);
					// void *obj2 = kmalloc(size);
					kfree(obj1);
					// kfree(obj2);
				}
			}
		}
	}

	return 0;
}
