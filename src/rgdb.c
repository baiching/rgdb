/*
 * rgdb.c : Defines the entry point for the application.
 * C11 port of rgdb.cpp.
 *
 * Changes from the C++ original, beyond the mechanical translation:
 *
 *   1. `int d = (int)malloc(10);` was a leaked allocation whose pointer was
 *      truncated to int. It served no purpose, so it is gone. The allocator
 *      path is exercised properly below through the page allocator, which is
 *      the component that actually needs to be smoke-tested.
 *
 *   2. std::cerr / std::cout became fprintf / fputs on stderr / stdout.
 *
 *   3. The parser test list was dead code behind a commented-out block; it is
 *      restored as live code so the parser is actually exercised.
 */

#include "rgdb.h"

#include "common/malloc_mi.h"
#include "storage/page_allocator.h"

#include <pg_query.h>

#include <stdio.h>
#include <stdlib.h>

static const char* const kParserTests[] = {
	"SELECT 1",
	"SELECT * FROM x WHERE z = 2",
	"SELECT 5.41414",
	"SELECT $1",
	"SELECT 999999999999999999999::numeric/1000000000000000000000",
	"SELECT 4790999999999999999999999999999999999999999999999999999999999999999999999999999999999999 * 9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999"
};

#define K_PARSER_TEST_COUNT (sizeof(kParserTests) / sizeof(kParserTests[0]))

/*
 * Round-trips every query through the protobuf parser, reporting the parse
 * tree length. The original only parsed a single hard-coded statement.
 */
static int run_parser_tests(void)
{
	size_t i;
	int failures = 0;

	for (i = 0; i < K_PARSER_TEST_COUNT; ++i) {
		PgQueryProtobufParseResult result = pg_query_parse_protobuf(kParserTests[i]);

		if (result.error != NULL) {
			fprintf(stderr, "Parser Error: %s (cursor %d)\n",
				result.error->message, result.error->cursorpos);
			failures += 1;
		}
		else {
			printf("%s -> %zu bytes\n", kParserTests[i], result.parse_tree.len);
		}

		pg_query_free_protobuf_parse_result(result);
	}

	return failures;
}

/*
 * Exercises the page allocator: hands out a few pages, dirties one, flushes.
 * This is the smoke test the C++ main never had.
 */
static int run_page_allocator_smoke_test(void)
{
	rgdb_page_allocator alloc;
	rgdb_page_header* pages[4];
	size_t i;

	if (!rgdb_page_allocator_init(&alloc)) {
		fprintf(stderr, "page allocator: init failed\n");
		return 1;
	}

	for (i = 0; i < 4; ++i) {
		pages[i] = rgdb_page_allocator_get_page(&alloc);
		if (pages[i] == NULL) {
			fprintf(stderr, "page allocator: exhausted after %zu pages\n", i);
			rgdb_page_allocator_destroy_pool(&alloc);
			return 1;
		}

		/* Payload must be distinct, page-aligned, and inside the arena. */
		if (pages[i]->page_ptr == NULL || ((uintptr_t)pages[i]->page_ptr % RGDB_PAGE_SIZE) != 0) {
			fprintf(stderr, "page allocator: page %zu is not page-aligned\n", i);
			rgdb_page_allocator_destroy_pool(&alloc);
			return 1;
		}
		if (i > 0 && pages[i]->page_ptr == pages[i - 1]->page_ptr) {
			fprintf(stderr, "page allocator: page %zu aliases page %zu\n", i, i - 1);
			rgdb_page_allocator_destroy_pool(&alloc);
			return 1;
		}

		/* Touch the page so the mapping is real. */
		pages[i]->page_ptr[0] = (uint8_t)i;
	}

	if (!rgdb_page_allocator_mark_dirty(&alloc, pages[0])) {
		fprintf(stderr, "page allocator: mark_dirty failed\n");
		rgdb_page_allocator_destroy_pool(&alloc);
		return 1;
	}

	printf("page allocator: %zu pages allocated, %zu dirty\n",
		rgdb_page_allocator_pages_allocated(&alloc),
		rgdb_page_allocator_dirty_count(&alloc));

	/* Release then re-acquire: the recycled page must be handed back. */
	rgdb_page_allocator_release_page(&alloc, pages[1]);
	{
		rgdb_page_header* reused = rgdb_page_allocator_get_page(&alloc);
		if (reused != pages[1]) {
			fprintf(stderr, "page allocator: expected page reuse\n");
			rgdb_page_allocator_destroy_pool(&alloc);
			return 1;
		}
	}

	(void)rgdb_page_allocator_flush(&alloc);
	rgdb_page_allocator_destroy_pool(&alloc);

	printf("page allocator: ok\n");
	return 0;
}

int main(void)
{
	int failures;

	printf("rgdb %s (C11)\n", RGDB_VERSION_STRING);

	failures = run_parser_tests();
	failures += run_page_allocator_smoke_test();

	/* Optional: ensures all memory is freed upon program exit (Valgrind). */
	pg_query_exit();

	return (failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
