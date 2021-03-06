#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "core/type_consts.h"

typedef struct reindexer_buffer {
	int len;
	uint8_t *data;
} reindexer_buffer;

typedef struct reindexer_resbuffer {
	int len;
	int results_flag;
	uintptr_t data;
} reindexer_resbuffer;

typedef struct reindexer_error {
	int code;
	const char *what;
} reindexer_error;

typedef struct reindexer_string {
	void *p;
	int n;
} reindexer_string;

typedef struct reindexer_ret {
	reindexer_error err;
	reindexer_resbuffer out;
} reindexer_ret;

#ifdef __cplusplus
}
#endif
