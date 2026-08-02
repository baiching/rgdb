#pragma once

#include <cstdint>
#include <atomic>
#include <vector>

namespace rgdb {
#define PAGE_SIZE 8192
#define TOTAL_PAGES 128
	constexpr auto TOTAL_BYTES = PAGE_SIZE * TOTAL_PAGES;

	/* 3 states of pages */
	enum class PageState
	{
		CLEAN,
		DIRTY,		
		IN_USE
	};

	class PageAllocator {
	public:
		static void* pages;

		struct PageHeader
		{
			bool is_dirty;
			uint8_t* page_ptr;

			uint64_t page_id;
			uint64_t ref_count; /* How many time it has been called */
			uint64_t last_accessed; /* Helps with page eviction logic */
			uint64_t page_type;		/* Data / Index */

			std::atomic<PageState> pstate{ PageState::CLEAN };
		};

	public:
		// it pre-allocates x number of pages per object
		PageAllocator();
		~PageAllocator();

	public:
		/*  returns one page from allocated memory  */
		PageHeader* getPage();

		// Only increases existing size
		//void* resize(void *page_ptr, uint64_t total_size, uint64_t total_blocks);

		/* frees the everything in one shot */
		bool destroyPool();

	};
}