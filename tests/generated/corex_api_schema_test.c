/* Generated test skeleton: positive and bounded-negative schema checks. */
#include "corex_api_schema.h"
#include <stdio.h>
int main(void) {
    if (COREX_GENERATED_API_COUNT != 3u ||
        corex_generated_validate_payload(OP_SYNC, 0) != 0 ||
        corex_generated_validate_payload(OP_STREAM_QUERY, 8) != 0 ||
        corex_generated_validate_payload(OP_EVENT_QUERY, 7) == 0 ||
        corex_generated_validate_payload(999u, 0) == 0)
        return 1;
    puts("M2_S5_GENERATED_SCHEMA_TEST=PASS");
    return 0;
}
