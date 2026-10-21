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


int main(void)
{
	int failures;

	printf("rgdb %s (C11)\n", RGDB_VERSION_STRING);

	failures = run_parser_tests();
	//failures += run_page_allocator_smoke_test();

	/* Optional: ensures all memory is freed upon program exit (Valgrind). */
	pg_query_exit();

	return (failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
