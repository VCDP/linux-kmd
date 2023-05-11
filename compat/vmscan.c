#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/memcontrol.h>
struct mem_cgroup *root_mem_cgroup __read_mostly;

void mem_cgroup_update_lru_size(struct lruvec *lruvec, enum lru_list lru,
                                int zid, int nr_pages)
{
        struct mem_cgroup_per_node *mz;
        unsigned long *lru_size;
        long size;

        if (mem_cgroup_disabled())
                return;

        mz = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
        lru_size = &mz->lru_zone_size[zid][lru];

        if (nr_pages < 0)
                *lru_size += nr_pages;

        size = *lru_size;
        if (WARN_ONCE(size < 0,
                "%s(%p, %d, %d): lru_size %ld\n",
                __func__, lruvec, lru, nr_pages, size)) {
                VM_BUG_ON(1);
                *lru_size = 0;
        }

        if (nr_pages > 0)
                *lru_size += nr_pages;
}



static struct mem_cgroup_per_node *
mem_cgroup_page_nodeinfo(struct mem_cgroup *memcg, struct page *page)
{
        int nid = page_to_nid(page);

        return memcg->nodeinfo[nid];
}


struct lruvec *mem_cgroup_page_lruvec(struct page *page, struct pglist_data *pgdat)
{
        struct mem_cgroup_per_node *mz;
        struct mem_cgroup *memcg;
        struct lruvec *lruvec;

        if (mem_cgroup_disabled()) {
                lruvec = &pgdat->lruvec;
                goto out;
        }

        memcg = page->mem_cgroup;
        /*
         * Swapcache readahead pages are added to the LRU - and
         * possibly migrated - before they are charged.
         */
        if (!memcg)
                memcg = root_mem_cgroup;

        mz = mem_cgroup_page_nodeinfo(memcg, page);
        lruvec = &mz->lruvec;
out:
        /*
         * Since a node can be onlined after the mem_cgroup was created,
         * we have to be prepared to initialize lruvec->zone here;
         * and if offlined then reonlined, we need to reinitialize it.
         */
        if (unlikely(lruvec->pgdat != pgdat))
                lruvec->pgdat = pgdat;
        return lruvec;
}

/*
 * page_evictable - test whether a page is evictable
 * @page: the page to test
 *
 * Test whether page is evictable--i.e., should be placed on active/inactive
 * lists vs unevictable list.
 *
 * Reasons page might not be evictable:
 * (1) page's mapping marked unevictable
 * (2) page is part of an mlocked VMA
 *
 */
int page_evictable(struct page *page)
{
        int ret;

        /* Prevent address_space of inode and swap cache from being freed */
        rcu_read_lock();
        ret = !mapping_unevictable(page_mapping(page)) && !PageMlocked(page);
        rcu_read_unlock();
        return ret;
}


/**
 * check_move_unevictable_pages - check pages for evictability and move to appropriate zone lru list
 * @pages:      array of pages to check
 * @nr_pages:   number of pages to check
 *
 * Checks pages for evictability and moves them to the appropriate lru list.
 *
 * This function is only used for SysV IPC SHM_UNLOCK.
 */
void backport_check_move_unevictable_pages(struct page **pages, int nr_pages)
{
        struct lruvec *lruvec;
        struct pglist_data *pgdat = NULL;
        int pgscanned = 0;
        int pgrescued = 0;
        int i;

        for (i = 0; i < nr_pages; i++) {
                struct page *page = pages[i];
                struct pglist_data *pagepgdat = page_pgdat(page);

                pgscanned++;
                if (pagepgdat != pgdat) {
                        if (pgdat)
                                spin_unlock_irq(&pgdat->lru_lock);
                        pgdat = pagepgdat;
                        spin_lock_irq(&pgdat->lru_lock);
                }
                lruvec = mem_cgroup_page_lruvec(page, pgdat);

                if (!PageLRU(page) || !PageUnevictable(page))
                        continue;

                if (page_evictable(page)) {
                        enum lru_list lru = page_lru_base_type(page);

                        VM_BUG_ON_PAGE(PageActive(page), page);
                        ClearPageUnevictable(page);
                        del_page_from_lru_list(page, lruvec, LRU_UNEVICTABLE);
                        add_page_to_lru_list(page, lruvec, lru);
                        pgrescued++;
                }
        }

        if (pgdat) {
                __count_vm_events(UNEVICTABLE_PGRESCUED, pgrescued);
                __count_vm_events(UNEVICTABLE_PGSCANNED, pgscanned);
                spin_unlock_irq(&pgdat->lru_lock);
        }
}
EXPORT_SYMBOL_GPL(backport_check_move_unevictable_pages);

