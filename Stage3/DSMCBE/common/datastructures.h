#ifndef DATASTRUCTURES_H
#define DATASTRUCTURES_H

#ifdef DSMCBE_SPU

//#define SPU_TRACE_MEM

  #ifdef SPU_TRACE_MEM
    extern unsigned int m_balance;
    extern void* __m_malloc(unsigned int x, char* s1, int s2);
    extern void __m_free(void* x, char* s1, int s2);
    extern void* __m_malloc_align(unsigned int x, int y, char* s1, int s2);
    extern void __m_free_align(void* x, char* s1, int s2);

    #define MALLOC(x) __m_malloc(x, __FILE__, __LINE__)
    #define FREE(x) __m_free(x, __FILE__, __LINE__)
    #define MALLOC_ALIGN(x,y) __m_malloc_align(x,y, __FILE__, __LINE__)
    #define FREE_ALIGN(x) __m_free_align(x, __FILE__, __LINE__)
  #else
    void* clear(unsigned long size);
    void* clearAlign(unsigned long size, int base);
    #define MALLOC(x) clear(x)
    #define FREE(x) thread_free(x)
    #define MALLOC_ALIGN(x,y) clearAlign(x,y)
    #define FREE_ALIGN(x) thread_free_align(x)
  #endif /*SPU_TRACE_MEM*/
#else
  #define MALLOC(x) malloc(x)
  #define FREE(x) free(x)
  #define MALLOC_ALIGN(x,y) malloc_align(x,y)
  #define FREE_ALIGN(x) free_align(x)
#endif


/*********************/
/* list implementation */
/*********************/
typedef struct cell *list;

struct cell
{
	void *element;
	list next;
};

extern list cons(void *element, list l);
extern list cdr_and_free(list l);

extern list list_create();
extern void list_destroy(list l);
extern void list_add(void* element, list* l);
extern void list_remove(list* l);

/*********************/
/* key list implementation */
/*********************/

typedef struct keycell *keylist;

struct keycell
{
	void *key;
	void *data;
	keylist next;
};

extern keylist key_cons(void *key, void* data, keylist l);
extern keylist key_cdr_and_free(keylist l);


/*********************/
/* stack implementation */
/*********************/

struct stack
{
	list elements;
};
typedef struct stack *stack;

/* create a new, empty stack */
extern stack stack_create(void);
extern void stack_destroy(stack s);

/* push a new element on top of the stack */
extern void stack_push(stack s, void *element);

/* pop the top element from the stack.  The stack must not be
 empty. */
extern void stack_pop(stack s);

/* return the top element of the stack */
extern void *stack_top(stack s);

/* return a true value if and only if the stack is empty */
extern int stack_empty(stack s);


/*********************/
/* unsorted list implementation */
/*********************/

/* We use unsorted linked lists to implement sets of elements,
 that can handle only equality */

struct ulset
{
	int (*equal)(void *, void *);
	keylist elements;
};

typedef struct ulset *ulset;

/* create an empty ulset */
extern ulset ulset_create(int (*equal)(void *, void *));

extern void ulset_destroy(ulset l);
extern int ulset_empty(ulset l);

/* return a true value iff the element is a member of the ulset */
extern int ulset_member(ulset l, void *key);

/* returns the matching element */
extern void* ulset_get(ulset l, void* key);

/* insert an element into an ulset.  The element must not already
 be a member of the ulset. */
extern void ulset_insert(ulset l, void* key, void *element);

/* delete an element from an ulset.  The element must be
 a member of the ulset. */
extern void ulset_delete(ulset l, void *key);

/* returns the list of elements matching */
extern keylist * ulset_find(keylist *lp, void *key, int (*equal)(void *, void *));

/*********************/
/* sorted list implementation */
/*********************/

struct slset
{
	int (*less)(void *, void *);
	keylist elements;
};

typedef struct slset *slset;

/* create an empty slset */
extern slset slset_create(int (*less)(void *, void *));
extern void slset_destroy(slset l);
extern int slset_empty(slset l);

/* return a true value iff the element is a member of the slset */
extern int slset_member(slset l, void *key);

/* returns the element with the given key */
extern void* slset_get(slset l, void* key);

/* insert an element into an slset.  The element must not already
 be a member of the slset. */
extern void slset_insert(slset l, void* key, void *element);

/* delete an element from an slset.  The element must be
 a member of the slset. */
extern void slset_delete(slset l, void *key);

/* returns a value indicating if the two elements are equal */
extern int slset_equal(void *e1, void *e2, int (*less)(void *, void *));

/* returns the list entry with the match */
extern keylist* slset_find(keylist *lp, void *key, int (*less)(void *, void *));

/* returns a value indicating if the given list points to the given key */
extern int slset_points_to(keylist *lp, void *key, int (*less)(void *, void *));


/*********************/
/* queue implementation */
/*********************/

struct queue
{
	list head;
	list tail;
};
typedef struct queue *queue;

/* create an empty queue */
extern queue queue_create(void);
extern void queue_destroy(queue q);

/* insert an element at the end of the queue */
extern void queue_enq(queue q, void *element);

/* delete the front element on the queue and return it */
extern void *queue_deq(queue q);

/* return a true value if and only if the queue is empty */
extern int queue_empty(queue q);

/* returns the number of elements in the queue, use only for debugging, runs in O(n) */ 
extern unsigned int queue_count(queue q);

/*********************/
/* double linked list implementation */
/*********************/

typedef struct dcell *dlist;

struct dcell
{
	void *element;
	dlist next;
	dlist prev;
};

extern dlist dcons(void *element, dlist prev, dlist next);
extern dlist create_and_link(void *element, dlist prev, dlist next);
extern void * unlink_and_free(dlist l);

/*********************/
/* double ended queue implementation */
/*********************/

struct dqueue
{
	dlist sentinel;
};
typedef struct dqueue *dqueue;

/* create an empty dqueue */
extern dqueue dq_create(void);
extern void dq_destroy(dqueue q);

/* insert an element at the front of the dqueue */
extern void dq_enq_front(dqueue q, void *element);

/* insert an element at the back of the dqueue */ 
extern void dq_enq_back(dqueue q, void *element);

/* delete an element from the front of the dqueue and return it */
extern void *dq_deq_front(dqueue q);

/* delete an element from the back of the dqueue and return it */
extern void *dq_deq_back(dqueue q);

/* return a true value if and only if the dqueue is empty */
extern int dq_empty(dqueue q);


/*********************/
/* hash table implementation */
/*********************/

struct hashtable
{
	unsigned int count;
	unsigned int fill;
	unsigned int wrapsize;
	slset* buffer;
	int (*less)(void*, void*);
	int (*hash)(void*, unsigned int count);
};


typedef struct hashtable* hashtable;

extern hashtable ht_create(unsigned int size, int (*less)(void *, void *), int (*hash)(void *, unsigned int count));
extern void ht_destroy(hashtable ht);
extern void ht_insert(hashtable ht, void* key, void* data);
extern void ht_delete(hashtable ht, void* key);
extern int ht_member(hashtable ht, void* key);
extern void* ht_get(hashtable ht, void* key);
extern void ht_resize(hashtable ht, unsigned int newsize);

/*********************/
/* hash table itteration */
/*********************/

struct hashtableIterator
{
	hashtable ht;
	keylist kl;
	unsigned int index;
};

typedef struct hashtableIterator* hashtableIterator;

extern hashtableIterator ht_iter_create(hashtable ht);
extern void* ht_iter_get_key(hashtableIterator iter);
extern void* ht_iter_get_value(hashtableIterator iter);
extern int ht_iter_next(hashtableIterator iter);
extern void ht_iter_destroy(hashtableIterator iter);


#endif
