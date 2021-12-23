#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include "devices/block.h"

struct cache_block{
    uint8_t block_data[BLOCK_SECTOR_SIZE];           /* data from disk */
    block_sector_t sector;                           /* sector index on disk */
    struct list_elem elem;                           /* list elem */

    int dirty;                                       /* if dirty dirty = 1, else dirty = 0 */
    int reference_bit;                               /* used for clock_algorithm. 1/0 */
    int occupied;                                    /* number of being occupied */
};

 /* cache in form of list */
struct list cache;     
 /* the especial lock for cache */                             
struct lock cache_lock;    
/* the size of cache list, it should be no greater than 64 */
int cache_size;
/* the amount of used cache blocks */                   
int occuppied_cache_block_amount;                    

/* initialize the cache in the beginning */
void cache_init(void);                  
/* get a cache block */
struct cache_block* get_cache_block(block_sector_t, int);
/* release a cache block*/
void release_cache_block(struct cache_block*);

/* scan the whole cache, and write back dirty block to disk. 
if if_flushed = true, flush the whole cache, i.e. write back dirty + empty the cache */
void scan_cache_write_back(int if_flushed);

#endif /* filesys/cache.h */