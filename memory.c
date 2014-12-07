/****************************************\
 *    A simple malloc function 					
 *    @author: vincent wei							
 *    @date: 2014 / 12 / 6							 
\****************************************/
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

/**********************************************************************************\
 *	why choose inline function? because macro defintion in c is error prone.									
 *	for a silly example, suppose we define a macro just as below:							
 *	#define double(x) (x + x) 																																		
 *  if we call it this way:																									   							
 *																																																
 *		int a = 0;																																									
 *		int b = double(++a);																																				
 *																																																
 *	you may assume b will evaluate to 2, but the real value of b will be 3													
 *	this is because that the c preprocessor will replace `double(++a)` as 													
 *	(++a) + (++a)																																										
 *	which will definitely evaluate to 3. so it is not what you expect it to be.										
 *	but if we just use a inline function. Everything will be ok. so lets stick 
 *  to using inline functions when possible. Besides inline function is more
 *	safe compared with macro definition and easier to debug when you get trouble. 
 *	And it is as just fast as macro definition without overhead of incurring a call 
 *	stack.																	
\***********************************************************************************/
// inline functon align 
inline size_t align(size_t x) {
	return (((x - 1) >> 2) << 2) + 4;
}
/**
 * there is a small trick here. the field data is just like a
 * read-only pointer to the first element of the array
 * and it is useful for special purpose here
 */

struct block_manager {
	struct block_manager *prev, *next;
	size_t size;
	int free;
	// a pointer to the allocated memory, just to validate the the pointer to be freed
	void *ptr;
	// a small trick, data + 1 will increase by 1 byte. 
	char data[0];
};

typedef struct block_manager* pbm_t;

#define BLOCK_SIZE 20
// global pointer to the first block
pbm_t global_ptr = NULL;

// find block that can be reused
pbm_t find_free_block(pbm_t *plast, size_t size) {
	pbm_t current = global_ptr;
	while (current && !(current->free && current->size >= size)) {
		*plast = current;
		current = current->next;
	}
	return current;
}
// split a big reusable block into two blocks if required sz is less that reusable block
void split_block(pbm_t pb, size_t sz) {
	pbm_t new_block;
	new_block = (pbm_t)(pb->data + sz);
	new_block->size = pb->size - sz - BLOCK_SIZE;
	new_block->prev = pb;
	new_block->next = pb->next;
	new_block->next->prev = new_block;
	new_block->ptr = new_block->data; // can also new_block->ptr = new_block + 1
	new_block->free = 1;
	pb->next = new_block;
	pb->size = sz;
}
// if can not find suitable block, we can get new block by calling sbrk(size)
pbm_t get_new_block(pbm_t last, size_t size) {
	pbm_t new_block = (pbm_t)sbrk(0);
	void *request = sbrk(size + BLOCK_SIZE);
	// if fail
	if (request == (void *)-1) 
		return NULL;
	// connect two block_manager
	if (last)
		last->next = new_block;
	else 
		global_ptr = new_block; // the very first call to get_new_block

	new_block->size = size;
	new_block->prev = last;
	new_block->next = NULL;
	new_block->free = 0;
	new_block->ptr = new_block->data;
	return new_block;
}

void *malloc(size_t sz) {
	if (sz <= 0) return NULL;
	size_t size = align(sz);
	pbm_t block;
	if (!global_ptr) {
		block = get_new_block(NULL, size);
		if (!block) 
		return NULL;
	}
	else {
		pbm_t last = global_ptr;
		block = find_free_block(&last, size);
		// not find reusabe space
		if (!block) {
			block = get_new_block(last, size);
			if (!block) return NULL;
		} 
		else {
			block->free = 0;
			if ((block->size - size) >= (BLOCK_SIZE + 4))
				split_block(block, size);
		}
	}
	return (void *)(block->data);
}
// fusion small fragmention 
pbm_t fusion(pbm_t bm) {
	if (bm->next && bm->next->free) {
		bm->size += (BLOCK_SIZE + bm->next->size);
		bm->next = bm->next->next;
		if (bm->next) 
			bm->next->prev = bm;
	}
	return bm;
}
// get a ptr to block_manager 
pbm_t get_block_manager(void *block) {
	return (pbm_t)block - 1;
}
// is a valid ptr to be freed
int valid(void *p) {
	if (global_ptr && p && p >= global_ptr && p < sbrk(0)) {
		pbm_t tmp = get_block_manager(p);
		return tmp->ptr == p;
	}
	return 0;
}

void *calloc(size_t nelem, size_t elsize) {
	size_t size = nelem * elsize;
	void *p = malloc(size);
	if (!p) return NULL;
	memset(p, 0, size);
	return p;
}

void free(void *ptr) {
	if (!valid(ptr))
		return;
	pbm_t bm = get_block_manager(ptr);
	bm->free = 1;
	// fusion with the previous block if possible
	if (bm->prev && bm->prev->free) {
		bm = fusion(bm->prev);
	}
	// fusion with the next block if possible
	// and if no block exists after bm
	if (!(fusion(bm)->next)) {
		if (!(bm->prev))
			global_ptr = NULL;
		brk(bm);
	}
}

void copy_block(pbm_t src, pbm_t dst) {
	int *sdata, *ddata;
	sdata = src->ptr;
	ddata = dst->ptr;
	for(size_t i = 0; i * 4 < src->size; i++) {
		ddata[i] = sdata[i];
	}
}

void *realloc(void *ptr, size_t sz) {
	if (!ptr)
		return malloc(sz);
	if (!valid(ptr)) 
		return NULL;
	
	size_t size = align(sz);
	pbm_t bm = get_block_manager(ptr);
	if (bm->size >= size) {
		if (bm->size - size >= BLOCK_SIZE + 4)
			split_block(bm, size);
	} 
	else {
		if (bm->next && bm->next->free && (bm->next->size + BLOCK_SIZE + bm->size >= size)) {
			fusion(bm);
			if (bm->size - size >= BLOCK_SIZE + 4)
				split_block(bm, size);
		} 
		else {
			void *new_block = malloc(size);
			if (!new_block)
				return NULL;
			pbm_t new_bm = get_block_manager(new_block);
			copy_block(bm, new_bm);
			// donot forget this 
			free(ptr);
			return new_block;
		}
	}
	return ptr;
}
