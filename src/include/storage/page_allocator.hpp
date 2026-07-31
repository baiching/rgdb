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
			uint8_t* page_ptr;

			std::atomic<PageState> pstate{ PageState::CLEAN };
		};

	public:
		// it pre-allocates x number of pages per object
		PageAllocator();
		~PageAllocator();

	public:
		std::vector<PageHeader*> getPage(const uint64_t num_of_pages);

		// Only increases existing size
		void* resize(void *page_ptr, uint64_t total_size, uint64_t total_blocks);

		bool freePage(uint8_t* page);

	};
}