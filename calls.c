#include <string.h>

#include "calls.h"

void *_sbrk(ptrdiff_t incr);

struct block *freehead = NULL;

struct block {
	size_t size;
	struct block *p;
	struct block *n;
};

void *malloc(size_t size)
{
	struct block *fb;
	char *b;

	size += sizeof(struct block);
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

			return ((unsigned char *) fb + sizeof(struct block));
		}

		fb = fb->n;
	}
		
	if ((b = _sbrk(size)) == NULL)
		return NULL;
	
	*((size_t *) b) = size;

	return (b + sizeof(struct block));
}

void free(void *ptr)
{
	struct block *fb;

	ptr -= sizeof(struct block);

	fb = (struct block *) ptr;

	fb->p = NULL;
	fb->n = freehead;
	
	freehead->p = fb;

	freehead = fb;

	for (fb = freehead; fb != NULL;) {
		size_t sz;
	
		sz = *((size_t *) fb);

		if (((char * ) fb) + sz == _sbrk(0)) {
			if (fb->p != NULL)
				fb->p->n = fb->n;
			else
				freehead = fb->n;
			
			if (fb->n != NULL)
				fb->n->p = fb->p;

			fb = fb->n;
			
			_sbrk(-((ptrdiff_t) sz));
		
			continue;
		}
 
		fb = fb->n;
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
