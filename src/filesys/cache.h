#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include "devices/block.h"

struct cache_block{
    uint8_t block_data[BLOCK_SECTOR_SIZE];           /* data from disk */
    block_sector_t sector;                           /* sector index on disk */
    struct list_elem elem;                           /* list elem */

    int dirty;                                       /* if dirty dirty = 1, else dirty = 0 */
    int refernce_bit;                                /* used for clock_algorithm */
    int occupied_count;                              /* number of being occupied */
};

 /* cache in form of list */
struct list cache;     
 /* the especial lock for cache */                             
struct lock cache_lock;          
/* the amount of used cache blocks */                   
int occuppied_cache_block_amount;                    

/* initialize the cache in the beginning */
void cache_init(void);                  
/* return the cache_block with sector = sector, return null if not in cache */
struct cache_block* seek_cache_block(block_sector_t sector);   
/* TBD */
struct cache_block* get_cache_block(block_sector_t sector, int dirty);
/* insert a new cache block with "sector" and "dirty" */
struct cache_block* insert_cache_block(block_sector_t sector, int dirty);
/* scan the whole cache, and write back dirty block to disk. 
if if_flushed = true, flush the whole cache, i.e. write back dirty + empty the cache */
void scan_cache_write_back(int if_flushed);
/* cache write back every "period" time */ 
void periodically_cache_write_back(int period) 

#endif /* filesys/cache.h */