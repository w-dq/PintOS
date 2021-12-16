#include <list.h>
#include "devices/block.h"
#include "cache.h"
#include "filesys.h"
#include "threads/malloc.h"
#include "threads/thread.h"

void 
cache_init(void){
    lock_init(cache_lock);
    list_init(cache);
    //thread_create("filesys_cache_writeback", 0, cache_back_loop, NULL); ???
}

struct cache_block* 
seek_cache_block(block_sector_t sector){

} 
/* TBD */
struct cache_block* 
get_cache_block(block_sector_t sector, int dirty){

}
/* insert a new cache block with "sector" and "dirty" */
struct cache_block* 
insert_cache_block(block_sector_t sector, int dirty){

}
/* scan the whole cache, and write back dirty block to disk. 
if if_flushed = true, flush the whole cache, i.e. write back dirty + empty the cache */
void 
scan_cache_write_back(int if_flushed){

}
/* cache write back every "period" time */ 
void 
periodically_cache_write_back(int period){

}