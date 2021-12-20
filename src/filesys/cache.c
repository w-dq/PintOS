#include <list.h>
#include "devices/block.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "devices/timer.h"

void 
cache_init(void){
    lock_init(&cache_lock);
    list_init(&cache);
    cache_size = 0; //but it seems nowhere to increment it?
    thread_create("filesys_cache_writeback", 0, periodically_cache_write_back, NULL); //???
}
/* if occupied_count_operation == 1, occupied_count ++. 
   if occupied_count_operation == 0, occupied_count = 0
   if occupied_count_operation > 1, occupied_count += occupied_count_operation */
void 
block_set(struct cache_block* block, block_sector_t sector, 
          int dirty, int reference_bit, int occupied_count_operation){
    
    block->sector = sector;
    block_read(fs_device,block->sector, &block->block_data);
    block->dirty = dirty;
    block->reference_bit= reference_bit;
    if (occupied_count_operation == 1){
        block->occupied_count ++;
    }
    else if(occupied_count_operation == 0){
        block->occupied_count = 0;
    }
    else if(occupied_count_operation > 1){
        block->occupied_count += occupied_count_operation;
    }
}

/* return the cache_block with sector = sector, return null if not in cache */
struct cache_block* 
seek_cache_block(block_sector_t sector){
    struct cache_block* block;
    struct list_elem* e;
    for(e = list_begin(&cache); e!=list_end(&cache); e = list_next(e)){
        block = list_entry(e, struct cache_block, elem);
        if (block->sector == sector) return block;
    }
    return NULL;  // a little different with reference(v)
} 
/* TBD */
struct cache_block* 
get_cache_block(block_sector_t sector, int dirty){
    lock_acquire(&cache_lock);
    struct cache_block* block = get_cache_block(sector);
    if (block!=NULL){
        block->occupied_count ++;
    }
}
/* insert a new cache block with "sector" and "dirty" */
struct cache_block* 
insert_cache_block(block_sector_t sector, int dirty){
    struct cache_block* insert_block;
    struct cache_block* replace_block;
    struct list_elem* e;
    if (cache_size < 64){
        insert_block = malloc(sizeof(struct cache_block));
        insert_block->occupied_count = 0;
        list_push_back(&cache, &insert_block->elem);
        cache_size ++;
    }
    else{
        // choose a block to replace
        for(e = list_begin(&cache); e!=list_end(&cache); e = list_next(e)){
            replace_block = list_entry(e, struct cache_block, elem);
            if (replace_block->occupied_count == 0){
                if (replace_block->reference_bit == 0){
                    if (replace_block->dirty == 1){
                        block_write(fs_device, replace_block->sector, &replace_block->block_data);
                    }
                    insert_block = replace_block;
                    break; //added
                }
                else{
                    replace_block->reference_bit = 0;
                }
            }
        }
    }
    block_set(insert_block, sector, dirty, 1, 1);
    return insert_block;
}
/* scan the whole cache, and write back dirty block to disk. 
if if_flushed = 1, flush the whole cache, i.e. write back dirty + empty the cache */
void 
scan_cache_write_back(int if_flushed){
    lock_acquire(&cache_lock);
    struct list_elem* e;
    struct cache_block* block;
    for(e = list_begin(&cache); e!=list_end(&cache); e = list_next(e)){
        block = list_entry(e, struct cache_block, elem);
        if (block->dirty == 1){
            block_write(fs_device, block->sector, &block->block_data);
            block->dirty = 0;
        }
        if (if_flushed == 1){
            list_remove(&block->elem);
            free(block);
        }
    }
    lock_acquire(&cache_lock);
}
/* cache write back every "period" time */ 
void 
periodically_cache_write_back(int period UNUSED){
    for (int i=INT32_MIN; i <INT32_MAX;i++)
    {   
        if (i > 0) timer_sleep(i);
        if (i <= 0) timer_sleep(-i);
        scan_cache_write_back(0);
    }
}   