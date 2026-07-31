#include "storage/page_allocator.hpp"
#include "common/malloc_mi.hpp"
#include <data_structure/concurrentqueue.h>
#include<vector>
#include<atomic>

namespace rgdb {
	moodycamel::ConcurrentQueue<PageAllocator::PageHeader *> clean_pages;
	moodycamel::ConcurrentQueue<uint8_t*> dirty_pages;
	std::atomic<size_t> offset{ 0 };
	void* PageAllocator::pages = nullptr;

	PageAllocator::PageAllocator() {
		PageAllocator::pages = malloc_aligned(PAGE_SIZE * TOTAL_PAGES, PAGE_SIZE);
	}

	std::vector<PageAllocator::PageHeader *> PageAllocator::getPage(const uint64_t num_of_pages) {
		std::vector<PageAllocator::PageHeader*> ph;
		PageAllocator::PageHeader* page;
		ph.reserve(num_of_pages);

		//PageHeader *ph[num_of_pages];
		for (uint64_t i = 0; i < num_of_pages; i++) {
			auto cur = offset.fetch_add(PAGE_SIZE);

			if (cur + PAGE_SIZE <= TOTAL_BYTES)
			{
				page = (PageAllocator::PageHeader*)((uint8_t*)PageAllocator::pages + cur);
				page->page_ptr = (uint8_t*)PageAllocator::pages + cur;
				page->pstate.store(PageState::IN_USE, std::memory_order_relaxed);

				ph.push_back(page);
				continue;
			}

			offset.fetch_sub(PAGE_SIZE); /* It's a temporary measure. Needs fixing, somehow need to stop adding once the buffer is exhausted */
			
			
			if (clean_pages.try_dequeue(page))
			{
				page->pstate = PageState::IN_USE;
				ph.push_back(page);
			}
			/* eviction will go here , we can also do emergency flush on demand to free up some pages */
			
		}

		return ph;
	}
}