#ifndef AT_H
#define AT_H

#include "range.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef AT_TRACE
#ifdef AT_YES_TRACE
extern int at_trace(const char* fmt, ...); //Defined elsewhere to customize how printing is done
#define AT_TRACE_(fmt, ...) at_trace("%s:%d:%s():trace: " fmt "%s\n", __FILE__, __LINE__, __func__, __VA_ARGS__)
#define AT_TRACE(...) AT_TRACE_(__VA_ARGS__, "")
#else
#define AT_TRACE(...)
#endif
#endif

#ifndef AT_DEBUG
#ifndef AT_NO_DEBUG
#define AT_DEBUG_(fmt, ...) \
    at_trace("%s:%d:debug: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define AT_DEBUG(...) AT_DEBUG_(__VA_ARGS__, "")
#else
#define AT_DEBUG(...)
#endif
#endif

#ifndef AT_WARN
#ifndef AT_NO_WARN
#define AT_WARN_(fmt, ...) \
    at_trace("%s:%d:warn: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define AT_WARN(...) AT_WARN_(__VA_ARGS__, "")
#else
#define AT_WARN(...)
#endif
#endif

#ifndef AT_ERROR
#ifndef AT_NO_ERROR
#define AT_ERROR_(fmt, ...) \
    at_trace("%s:%d:error: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define AT_ERROR(...) AT_ERROR_(__VA_ARGS__, "")
#else
#define AT_ERROR(...)
#endif
#endif

// Runtime assertions
#ifndef AT_ASSERT
#ifndef AT_NO_ASSERT
#define AT_ASSERT(test) assert(test)
#else
#define AT_ASSERT(test)
#endif
#endif


#ifndef AT_INPUT_BUFFER_SIZE
#define AT_INPUT_BUFFER_SIZE 64
#endif

#ifndef AT_OUTPUT_BUFFER_SIZE
#define AT_OUTPUT_BUFFER_SIZE 64 
#endif

enum AT_CMD_TYPE {
   AT_STANDALONE_COMMAND = 1,
   AT_ASSIGNMENT_COMMAND = 2,
   AT_STATUS_COMMAND = 3,
   AT_UNKOWN_COMMAND = 4
};

struct at_command_register_t;

struct at_function_result {
   bool result;
   int code;
   const char *detailed;
};

struct at_context_t;

struct at_function_context_t{
   struct at_context_t *context;
   struct range_t parameters;
   uint32_t stream_id;
};

typedef void (*pflush)(struct range_t*, void* udata, uint32_t stream_id);

void at_function_result_init(struct at_function_result *p);
void at_context_init(struct at_context_t **ctx, pflush flush, size_t num_streams, void* udata);

void at_command_add(
      struct at_context_t *ctx,
      const char *tag,
      enum AT_CMD_TYPE cmd_type,
      void (*function)(struct at_function_result*, struct at_function_context_t*, void* udata),
      void* udata);

void at_process_input(
      struct at_context_t *ctx,
      struct range_t *data,
      uint32_t stream_id);

void at_context_free(struct at_context_t *ctx);

void at_flush_output(struct at_context_t *ctx, uint32_t stream_id);

iterator_t at_get_parameter(iterator_t begin, iterator_t end, struct range_t *result);

bool at_get_in_quota_value(struct range_t *range, struct range_t *result);

/* Allows registered AT callback functions to access and set contextual state, if needed */
void *at_get_state (struct at_context_t *context);
void at_set_state (struct at_context_t *context, void* state);

/* Enable/disable echo programmatically */
void at_set_echo(struct at_context_t *ctx, bool echo);

void at_invalid_chars_error(struct at_function_result *r);
void at_unknown_error(struct at_function_result *r);
void at_ok_result(struct at_function_result *r);
void at_text_string_too_long_error(struct at_function_result *r);
void at_return_not_found_error(struct at_function_result *r);
void at_return_invalid_index_error(struct at_function_result *r);
void at_return_operation_not_supported_error(struct at_function_result *r);
void at_return_operation_not_allowed_error(struct at_function_result *r);

void at_add_unsolicited(struct at_context_t *ctx, const char *prefix, const char *text, uint32_t stream_id);
void at_add_unsolicited_line(struct at_context_t *ctx, const char *text, uint32_t stream_id);

void at_append_line(struct at_context_t *ctx, const char *text, uint32_t stream_id);
void at_append_int(struct at_context_t *ctx, int value, uint32_t stream_id);
void at_append_double(struct at_context_t *ctx, double value, uint32_t stream_id);
void at_append_text(struct at_context_t *ctx, const char *text, uint32_t stream_id);
void at_append_range(struct at_context_t *ctx, struct range_t* range, uint32_t stream_id);
void at_append_char(struct at_context_t *ctx, unsigned char c, uint32_t stream_id);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
