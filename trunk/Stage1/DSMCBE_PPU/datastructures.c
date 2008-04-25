#include "datastructures.h"
#include <assert.h>
#include <stdlib.h>
  
/*********************/
/* list implementation */
/*********************/

keylist key_cons(void *key, void* data, keylist l)
{
	keylist temp = malloc(sizeof(struct keycell));
	temp->data = data;
	temp->key = key; 
	temp->next = l;
	return temp;
}

keylist key_cdr_and_free(keylist l)
{
	keylist temp = l->next; 
	free(l);
	return temp;
}


list cons(void *element, list l)
{
	list temp = malloc(sizeof(struct cell));
	temp -> element = element;
	temp -> next = l;
	return temp;
}

list cdr_and_free(list l)
{
	list temp = l -> next; 
	free(l);
	return temp;
}

list list_create()
{
	list l = malloc(sizeof(struct cell));
	l->next = NULL;
	return l;
}

void list_add(void* element, list* l)
{
	*l = cons(element, *l);
}

void list_remove(list* l)
{
	*l = cdr_and_free(*l);
}

/*********************/
/* stack implementation */
/*********************/

stack stack_create(void)
{
	stack temp = malloc(sizeof(struct stack));
	temp -> elements = NULL;
	return temp;
}

void stack_push(stack s, void *element)
{
	s -> elements = cons(element, s -> elements);
}

int stack_empty(stack s)
{
	return s -> elements == NULL;
}

void stack_pop(stack s)
{
	assert(!stack_empty(s));
	s -> elements = cdr_and_free(s -> elements);
}

void * stack_top(stack s)
{
	assert(!stack_empty(s));
	return s -> elements -> element;
}


/*********************/
/* unsorted list implementation */
/*********************/


ulset ulset_create(int (*equal)(void *, void *))
{
	ulset l = malloc(sizeof(struct ulset));
	l -> elements = NULL;
	l -> equal = equal;
	return l;
}

keylist * ulset_find(keylist *lp, void *key, int (*equal)(void *, void *))
{
	for(; *lp && !equal((*lp)->key, key); lp = &((*lp)->next))
		;
	return lp;
}

int ulset_member(ulset l, void *key)
{
	keylist *lp = ulset_find(&(l->elements), key, l->equal);
	return (*lp) != NULL;
}

void* ulset_get(ulset l, void *key)
{
	keylist *lp = (keylist*)ulset_member(l ,key);
	if ((*lp))
		return NULL;
	else
		return (*lp)->data;

}

void ulset_insert(ulset l, void *key, void* data)
{
	keylist *lp = ulset_find(&(l -> elements), key, l -> equal);
	assert(!(*lp));
	*lp = key_cons(key, data, *lp);
}

void ulset_delete(ulset l, void *key)
{
	keylist *lp = ulset_find(&(l -> elements), key, l -> equal);
	assert(*lp);
	*lp = key_cdr_and_free(*lp);
}

/*********************/
/* sorted list implementation */
/*********************/

slset slset_create(int (*less)(void *, void *))
{
	slset l = malloc(sizeof(struct slset));
	l->elements = NULL;
	l->less = less;
	return l;
}

keylist* slset_find(keylist *lp, void *key, int (*less)(void *, void *))
{
	for(; *lp && less((*lp) -> key, key); lp = &((*lp) -> next))
		;
    return lp;
}

int slset_equal(void *e1, void *e2, int (*less)(void *, void *))
{
	return !less(e1, e2) && !less(e2, e1);
}

int slset_points_to(keylist *lp, void *key, int (*less)(void *, void *))
{
	return *lp && slset_equal((*lp)->key, key, less);
}

int slset_member(slset l, void *key)
{
	keylist *lp = slset_find(&(l->elements), key, l->less);
    return slset_points_to(lp, key, l->less);
}

void* slset_get(slset l, void *key)
{
	keylist *lp = slset_find(&(l->elements), key, l->less);
	if (slset_points_to(lp, key, l->less))
		return (*lp)->data;
	else
		return NULL;
}

void slset_insert(slset l, void* key, void *data)
{
	keylist *lp = slset_find(&(l->elements), key, l->less);
	assert(!slset_points_to(lp, key, l->less));
	*lp = key_cons(key, data, *lp);
}

void slset_delete(slset l, void *key)
{
	keylist *lp = slset_find(&(l->elements), key, l->less);
	assert(slset_points_to(lp, key, l->less));
	*lp = key_cdr_and_free(*lp);
}

/*********************/
/* queue implementation */
/*********************/

queue queue_create(void)
{
	queue q = malloc(sizeof(struct queue));
	q -> head = q -> tail = cons(NULL, NULL);
	return q;
}

int queue_empty(queue q)
{
	return q -> head == q -> tail;
}

void queue_enq(queue q, void *element)
{
	q -> tail -> next = cons(NULL, NULL);
	q -> tail -> element = element;
	q -> tail = q -> tail -> next;
}

void * queue_deq(queue q)
{
	assert(!queue_empty(q));
	{
		void *temp = q -> head -> element;
		q -> head = cdr_and_free(q -> head);
		return temp;
	}
}

/*********************/
/* double linked list implementation */
/*********************/

dlist dcons(void *element, dlist prev, dlist next)
{
	dlist temp = malloc(sizeof(struct dcell));
	temp -> element = element;
	temp -> prev = prev;
	temp -> next = next;
	return temp;
}

dlist create_and_link(void *element, dlist prev, dlist next)
{
	dlist temp = dcons(element, prev, next);
	prev -> next = temp;
	next -> prev = temp;
	return temp;
}

void * unlink_and_free(dlist l)
{
	void *temp = l -> element;
	l -> next -> prev = l -> prev;
	l -> prev -> next = l -> next;
	free(l);
	return temp;
}

/*********************/
/* double ended queue implementation */
/*********************/



dqueue dq_create(void)
{
	dqueue q = malloc(sizeof(struct dqueue));
	q -> sentinel = dcons(NULL, NULL, NULL);
	q -> sentinel -> next = q -> sentinel -> prev = q -> sentinel;
	return q;
}

int dq_empty(dqueue q)
{
	return q -> sentinel -> next == q -> sentinel;
}

void dq_enq_front(dqueue q, void *element)
{
	create_and_link(element, q -> sentinel, q -> sentinel -> next);
}

void dq_enq_back(dqueue q, void *element)
{
	create_and_link(element, q -> sentinel -> prev, q -> sentinel);
}

void * dq_deq_front(dqueue q)
{
	assert(!dq_empty(q));
	return unlink_and_free(q -> sentinel -> next);
}

void * dq_deq_back(dqueue q)
{
	assert(!dq_empty(q));
	return unlink_and_free(q -> sentinel -> prev);
}


/*********************/
/* hash table implementation */
/*********************/
hashtable ht_create(unsigned int size, int (*less)(void *, void *), int (*hash)(void *, unsigned int size))
{
	unsigned int i;
	hashtable ht;
	
	if (size < 2)
		size = 2;

	ht = malloc(sizeof(struct hashtable));
	ht->count = size;
	ht->fill = 0;
	ht->less = less;
	ht->hash = hash;

	ht->buffer = malloc(sizeof(void*) * size);
	for(i = 0; i < size; i++)
		ht->buffer[i] = slset_create(ht->less);

	return ht;
}

void ht_insert(hashtable ht, void* key, void* data)
{
	if (((ht->fill + 1) * 2) > ht->count)
		ht_resize(ht, ht->count * 2);

	slset_insert(ht->buffer[ht->hash(key, ht->count)], key, data);
	ht->fill++;
}

void ht_delete(hashtable ht, void* key)
{
	slset_delete(ht->buffer[ht->hash(key, ht->count)], key);
	ht->fill--;
	if (ht->fill * 3 < ht->count)
		ht_resize(ht, ht->count / 2);
}

int ht_member(hashtable ht, void* key)
{
	return slset_member(ht->buffer[ht->hash(key, ht->count)], key);
}

void* ht_get(hashtable ht, void* key)
{
	return slset_get(ht->buffer[ht->hash(key, ht->count)], key);
}

void ht_resize(hashtable ht, unsigned int newsize)
{
	unsigned int i;
	keylist kl;
	hashtable newtable;

	newtable = ht_create(newsize, ht->less, ht->hash);
	for(i = 0; i < ht->count; i++)
	{
		kl = ht->buffer[i]->elements;

		while(kl != NULL)
		{
			ht_insert(newtable, kl->key, kl->data);
			kl = kl->next;
		}
		free(ht->buffer[i]);
	}
	free(ht->buffer);

	ht->buffer = newtable->buffer;
	ht->count = newtable->count;
	ht->fill = newtable->fill;
	
	free(newtable);
}

