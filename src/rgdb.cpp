// rgdb.cpp : Defines the entry point for the application.
//

#include "rgdb.h"

#include <pg_query.h>


#include <stdio.h>
#include <stdlib.h>

size_t testCount = 6;
const char* tests[] = {
  "SELECT 1",
  "SELECT * FROM x WHERE z = 2",
  "SELECT 5.41414",
  "SELECT $1",
  "SELECT 999999999999999999999::numeric/1000000000000000000000",
  "SELECT 4790999999999999999999999999999999999999999999999999999999999999999999999999999999999999 * 9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999"
};

int main() {

    const char* sql = "SELECT * FROM users WHERE age > 30;";

    // Test a basic parser footprint
    //PgQueryParseResult result = pg_query_parse(sql);
    PgQueryProtobufParseResult result = pg_query_parse_protobuf(sql);

    if (result.error) {
        std::cerr << "Parser Error: " << result.error->message << std::endl;
    }
    else {
        
        std::cout << result.parse_tree.len << std::endl;
    }

    pg_query_free_protobuf_parse_result(result);

    //PgQueryParseResult result;
    //size_t i;

    //for (i = 0; i < testCount; i++) {
    //    result = pg_query_parse(tests[i]);

    //    if (result.error) {
    //        printf("error: %s at %d\n", result.error->message, result.error->cursorpos);
    //    }
    //    else {
    //        printf("%s\n", result.parse_tree);
    //    }

    //    pg_query_free_parse_result(result);
    //}

    // Optional, this ensures all memory is freed upon program exit (useful when running Valgrind)
    pg_query_exit();

    return 0;
}

