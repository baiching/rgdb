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
			
		
	}

	bool PageAllocator::destroyPool() {
		/* we should add flush everything before release, to ensure no data gets lost */

		free(pages);

		pages = nullptr;

		return true;
	}
}