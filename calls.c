#include <string.h>

#include "calls.h"

void *_sbrk(ptrdiff_t incr);

struct freeblock *freehead;

struct freeblock {
	size_t size;
	struct freeblock *p;
	struct freeblock *n;
};

void heapinit()
{
	freehead = (struct freeblock *) _sbrk(sizeof(struct freeblock));

	freehead->size = sizeof(struct freeblock);
	freehead->n = NULL;
	freehead->p = NULL;
}

void *malloc(size_t size)
{
	struct freeblock *fb;
	char *b;

	size += sizeof(struct freeblock);
	size = (size & 0xfffffff8) + 0x8;

	fb = freehead;
	while (fb != NULL) {
		if (fb->size >= size) {
			if (fb->p != NULL)
				fb->p->n = fb->n;
			else
				freehead = fb->n;

			if (fb->n != NULL)
				fb->n->p = fb->p;

			return ((unsigned char *) fb + sizeof(size_t));
		}

		fb = fb->n;
	}
		
	if ((b = _sbrk(size)) == NULL)
		return NULL;
	
	*((size_t *) b) = size;

	return (b + sizeof(size_t));
}

void free(void *ptr)
{
	struct freeblock *fb;

	ptr -= sizeof(size_t);

	fb = (struct freeblock *) ptr;

	fb->p = NULL;
	fb->n = freehead;
	
	freehead->p = fb;

	freehead = fb;

	for (fb = freehead; fb != NULL; fb = fb->n) {
		size_t sz;
	
		sz = *((size_t *) fb);

		if (((char * ) fb) + sz == _sbrk(0)) {
			_sbrk(-sz);

			return;
		}
	}
}

void *realloc(void *ptr, size_t size)
{
	void *old, *new;
	size_t oldsz;

	old = ptr - sizeof(size_t);
	oldsz = *((size_t *) old);

	new = malloc(size);

	memcpy(new, old, oldsz);

	free(old);

	return NULL;
}
