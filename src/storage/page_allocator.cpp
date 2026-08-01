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

	PageAllocator::PageHeader *PageAllocator::getPage() {
		PageAllocator::PageHeader* page;
		
		size_t old_offset = offset.load(std::memory_order_acquire);
		size_t new_offset;

		do
		{
			if (old_offset + PAGE_SIZE > TOTAL_BYTES)
			{
				return nullptr;
			}
			new_offset = old_offset + PAGE_SIZE;

		} while (!offset.compare_exchange_weak(old_offset, new_offset,
				std::memory_order_release, std::memory_order_relaxed));

		page = (PageAllocator::PageHeader*)((uint8_t*)PageAllocator::pages + new_offset);
		page->page_ptr = (uint8_t*)PageAllocator::pages + new_offset;
		page->pstate.store(PageState::IN_USE, std::memory_order_relaxed);

		return page;

			//if (cur + PAGE_SIZE <= TOTAL_BYTES)
			//{
			//	page = (PageAllocator::PageHeader*)((uint8_t*)PageAllocator::pages + cur);
			//	page->page_ptr = (uint8_t*)PageAllocator::pages + cur;
			//	page->pstate.store(PageState::IN_USE, std::memory_order_relaxed);

			//	ph.push_back(page);
			//	continue;
			//}

			//offset.fetch_sub(PAGE_SIZE); /* It's a temporary measure. Needs fixing, somehow need to stop adding once the buffer is exhausted */
			//
			//
			//if (clean_pages.try_dequeue(page))
			//{
			//	page->pstate = PageState::IN_USE;
			//	ph.push_back(page);
			//}
			/* eviction will go here , we can also do emergency flush on demand to free up some pages */
			
		
	}
}