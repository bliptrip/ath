#include "at.h"
#include "at_internal.h"

struct at_context_t {
   pflush flush;
   size_t num_streams;
   unsigned char *output_buffers;
   iterator_t *outputbuff_iterators;
   struct at_command_register_t *first;
   void *state;
   unsigned char *input_buffers;
   iterator_t *inputbuff_iterators;
   unsigned char *last_input_buffers;
   iterator_t *lastinbuff_iterators;
   int cmee_level;
   bool echo;
   void* udata; //User-data passed to 'flush' callback
};


struct at_command_register_t {
   const char *tag;
   void (*function)(struct at_function_result*, struct at_function_context_t*, void* udata);
   struct at_command_register_t *next;
   enum AT_CMD_TYPE cmd_type;
   void *udata; //Opaque user data pointer passed to command callback function
};

void at_function_result_init(struct at_function_result *p) {
   p->detailed = "OK";
   p->result = true;
   p->code = 0;
}

void at_flush_output(struct at_context_t *ctx, uint32_t stream_id){
   if (ctx->flush != 0){

      struct range_t range;

      range.begin = ctx->output_buffers + (stream_id * AT_OUTPUT_BUFFER_SIZE);
      range.end = ctx->outputbuff_iterators[stream_id];

      if ( range_is_empty (&range) == false) {
         ctx->flush(&range, ctx->udata, stream_id);
      }
   }

   ctx->outputbuff_iterators[stream_id] = ctx->output_buffers + (stream_id * AT_OUTPUT_BUFFER_SIZE);
}

iterator_t at_get_output_buffer_end_iterator(struct at_context_t *ctx, uint32_t stream_id) {
   return ctx->output_buffers + ((stream_id + 1) * AT_OUTPUT_BUFFER_SIZE);
}

void at_append_char(struct at_context_t *ctx, unsigned char c, uint32_t stream_id){

   if (ctx->outputbuff_iterators[stream_id] == at_get_output_buffer_end_iterator(ctx, stream_id)) {
      at_flush_output(ctx, stream_id);
   }

   *(ctx->outputbuff_iterators[stream_id]) = c;
   ctx->outputbuff_iterators[stream_id] = ctx->outputbuff_iterators[stream_id] + 1;
}

void at_append_text(struct at_context_t *ctx, const char *text, uint32_t stream_id){
   while (*text != 0) {
      at_append_char(ctx, *text++, stream_id);
   }
}

void at_append_range(struct at_context_t *ctx, struct range_t *range, uint32_t stream_id) {
   for (iterator_t it = range->begin; it != range->end; ++it) {
      at_append_char(ctx, *it, stream_id);
   }
}

void at_append_int(struct at_context_t *ctx, int value, uint32_t stream_id){
   char buff[20];
   snprintf(buff, sizeof(buff), "%i", value);
   at_append_text(ctx, buff, stream_id);
}

void at_append_double(struct at_context_t *ctx, double value, uint32_t stream_id){
   char buff[20];
   snprintf(buff, sizeof(buff), "%f", value);
   at_append_text(ctx, buff, stream_id);
}

void at_append_line(struct at_context_t *ctx, const char *text, uint32_t stream_id){
   at_append_text(ctx, text, stream_id);
   at_append_text(ctx, "\r\n", stream_id);
}


void at_return_operation_not_allowed_error(struct at_function_result *r) {
   r->code = 3;
   r->detailed = "Operation not allowed";
   r->result = false;
}

void at_return_operation_not_supported_error(struct at_function_result *r) {
   r->code = 4;
   r->detailed = "Operation not supported";
   r->result = false;
}


void at_return_invalid_index_error(struct at_function_result *r) {
   r->code = 21;
   r->detailed = "Invalid index";
   r->result = false;
}

void at_return_not_found_error(struct at_function_result *r) {
   r->code = 22;
   r->detailed = "Not found";
   r->result = false;
}

void at_text_string_too_long_error(struct at_function_result *r) {
   r->code = 24;
   r->detailed = "Text string too long";
   r->result = false;
}


void at_invalid_chars_error(struct at_function_result *r) {
   r->code = 25;
   r->detailed = "Invalid characters in text string";
   r->result = false;
}


void at_unknown_error(struct at_function_result *r) {
   r->code = 100;
   r->detailed = "Unknown error";
   r->result = false;
}

void at_ok_result(struct at_function_result *r) {
   r->code = 0;
   r->result = true;
   r->detailed = "OK";
}


void  at_cmee_buildin_status(struct at_function_result *r, struct at_function_context_t *ctx, void* udata){
   at_append_line(ctx->context, "", ctx->stream_id);
   at_append_text(ctx->context, "+CMEE: ", ctx->stream_id);
   at_append_int(ctx->context, ctx->context->cmee_level, ctx->stream_id);
   at_append_line(ctx->context, "", ctx->stream_id);
   at_ok_result(r);
}

static void ate0_buildin_status(struct at_function_result  *r, struct at_function_context_t *ctx, void* udata){
   ctx->context->echo = false;
   at_ok_result(r);
}


static void ate1_buildin_status(struct at_function_result  *r, struct at_function_context_t *ctx, void* udata){
   ctx->context->echo = true;
   at_ok_result(r);
}

iterator_t at_get_parameter(iterator_t begin, iterator_t end, struct range_t *result){

   for (iterator_t i = begin; i != end; ++i){

      if (*i == ',') {
         result->begin = begin;
         result->end = i;
         return i + 1;
      }
   }

   result->begin = begin;
   result->end = end;

   return end;
}


static void at_standalone_buildin(struct at_function_result *r, struct at_function_context_t *ctx, void* udata){
   at_ok_result(r);
}

static void at_cmee_buildin_assignment(
      struct at_function_result *r,
      struct at_function_context_t *ctx,
      void* udata){


   if (range_is_empty(&ctx->parameters)) {
      at_return_operation_not_supported_error(r);
      return;
   }

   if (range_equals(&ctx->parameters, "?") ||
       range_equals(&ctx->parameters, "\"?\"")) {
      at_append_line(ctx->context, "", ctx->stream_id);
      at_append_line(ctx->context, "+CMEE: (0-2)", ctx->stream_id);
      at_ok_result(r);
      return ;
   }

   struct range_t result;

   iterator_t it = at_get_parameter(ctx->parameters.begin, ctx->parameters.end, &result);

   if (it == ctx->parameters.end &&
       (range_is_empty(&result) == false) &&
       range_all_digits(&result) &&
       (range_size(&result) == 1)) {

      int new_cmee;
      if (range_convert_to_int(&result, &new_cmee) && (new_cmee >= 0) && (new_cmee <= 2)) {
         ctx->context->cmee_level = new_cmee;
         at_ok_result(r);
         return ;
      }
   }

   at_return_operation_not_supported_error(r);
   return;
}

void at_command_free(struct at_command_register_t *c){

   if (c->next != 0) {
      at_command_free(c->next);
   }

   free(c);
}


void at_context_free(struct at_context_t *ctx){

   if (ctx->first != 0) {
      at_command_free(ctx->first);
   }

   if (ctx->input_buffers != 0) {
      free (ctx->input_buffers);
   }

   if (ctx->inputbuff_iterators != 0) {
      free (ctx->inputbuff_iterators);
   }

   if (ctx->output_buffers != 0) {
      free (ctx->output_buffers);
   }

   if (ctx->last_input_buffers != 0) {
      free(ctx->last_input_buffers);
   }

   free (ctx);
}

void at_command_init(struct at_command_register_t *c){
   c->tag = 0;
   c->cmd_type = AT_STANDALONE_COMMAND;
   c->next = 0;
   c->function = 0;
   c->udata = NULL;
}

void at_command_add(
      struct at_context_t *ctx,
      const char *tag,
      enum AT_CMD_TYPE cmd_type,
      void (*function)(struct at_function_result*, struct at_function_context_t*, void* udata),
      void* udata) {
   struct at_command_register_t *p = (struct at_command_register_t*)malloc(sizeof(struct at_command_register_t));
   at_command_init(p);
   struct range_t tag_mod;

   p->cmd_type = cmd_type;
   p->function = function;
   p->tag = tag;
   p->next = ctx->first;
   p->udata = udata;
   ctx->first = p;
}

void at_set_echo(struct at_context_t *ctx, bool echo) {
   ctx->echo = echo;
}

void at_context_init(struct at_context_t **ctx, pflush flush, size_t num_streams, void *udata)
{
   *ctx = (struct at_context_t *)malloc(sizeof(struct at_context_t));

   if (ctx == 0)
      return;

   (*ctx)->udata = udata;
   (*ctx)->cmee_level = 0;
   (*ctx)->flush = flush;
   (*ctx)->num_streams = num_streams;
   (*ctx)->echo = true;
   (*ctx)->first = 0;
   (*ctx)->state = 0;
   (*ctx)->input_buffers = (unsigned char *)malloc(AT_INPUT_BUFFER_SIZE * num_streams);
   (*ctx)->output_buffers = (unsigned char *)malloc(AT_OUTPUT_BUFFER_SIZE * num_streams);
   (*ctx)->inputbuff_iterators = (iterator_t *)malloc(num_streams * sizeof(iterator_t));
   (*ctx)->outputbuff_iterators = (iterator_t *)malloc(num_streams * sizeof(iterator_t));
   (*ctx)->last_input_buffers = (unsigned char *)malloc(AT_INPUT_BUFFER_SIZE * num_streams);
   (*ctx)->lastinbuff_iterators = (iterator_t *)malloc(num_streams * sizeof(iterator_t));

   for (int i = 0; i < num_streams; i++)
   {
      (*ctx)->inputbuff_iterators[i] = (*ctx)->input_buffers + (i * AT_INPUT_BUFFER_SIZE);
      (*ctx)->outputbuff_iterators[i] = (*ctx)->output_buffers + (i * AT_OUTPUT_BUFFER_SIZE);
      (*ctx)->lastinbuff_iterators[i] = (*ctx)->last_input_buffers + (i * AT_INPUT_BUFFER_SIZE);
   }

   if ((*ctx)->input_buffers == 0 ||
       (*ctx)->inputbuff_iterators == 0 ||
       (*ctx)->output_buffers == 0 ||
       (*ctx)->outputbuff_iterators == 0 ||
       (*ctx)->last_input_buffers == 0 ||
       (*ctx)->lastinbuff_iterators == 0)
   {

      at_context_free(*ctx);
      *ctx = 0;

      return;
   }

   at_command_add(*ctx, "+cmee", AT_ASSIGNMENT_COMMAND, at_cmee_buildin_assignment, NULL);
   at_command_add(*ctx, "+cmee", AT_STATUS_COMMAND, at_cmee_buildin_status, NULL);
   at_command_add(*ctx, "", AT_STANDALONE_COMMAND, at_standalone_buildin, NULL);

   at_command_add(*ctx, "e0", AT_STANDALONE_COMMAND, ate0_buildin_status, NULL);
   at_command_add(*ctx, "e1", AT_STANDALONE_COMMAND, ate1_buildin_status, NULL);
}

struct range_t get_line(struct range_t *data){

   iterator_t r = range_search_character(data, '\r');

   if (r != data->end) {
      return range_create_it(data->begin, r);
   } else {
      return range_empty();
   }
}

void *at_get_state (struct at_context_t *context) {
   return context->state;
}

void at_set_state (struct at_context_t *context, void* state) {
   context->state = state;
}

bool at_get_in_quota_value(struct range_t *range, struct range_t *result){

   if (range_is_empty(range) == false &&
       *(range->begin) == '\"' && *((range)->end -1) == '\"' ) {

      result->begin = range->begin + 1;
      result->end = range->end - 1;
      return true;
   }

   return false;
}


void split_at_commands(struct range_t *command, void(*ptr)(struct range_t* )){

   bool qt = false;

   iterator_t b_cmd = command->begin;

   for (iterator_t it = command->begin; it != command->end; ++it) {
      if (*it == '"') {
         qt = !qt;
      }

      if (qt == false && *it == ';'){

         struct range_t cmd_range;


         cmd_range.begin = b_cmd;
         cmd_range.end = it;

         cmd_range = range_trim(&cmd_range);

         ptr(&cmd_range);
         b_cmd = it + 1;
      }
   }

   struct range_t cmd_range = range_create_it(b_cmd, command->end);
   cmd_range = range_trim(&cmd_range);
   ptr(&cmd_range);
}

bool get_at_command(struct range_t *input, struct range_t *result){
   AT_TRACE("begin: %p, end: %p, size: %d", input->begin, input->end, range_size(input));

   if ( range_size(input) < 2) {
      return  false;
   };

   unsigned char first = *input->begin;
   unsigned char second = *(input->begin + 1);

   if ((first == 'a' || first == 'A') && (second == 't' || second == 'T')) {
      result->begin = input->begin + 2;
      result->end = input->end;
      return true;
   }

   return false;
}

static bool is_character(unsigned char c){
   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static enum AT_CMD_TYPE at_get_cmd_type(struct range_t *tag, struct range_t *command){

   if (tag->end == command->end) {
      return AT_STANDALONE_COMMAND;
   } else if ( *(tag->end) == '=' && ((range_size(tag) + 1) < range_size(command)))  {
      return AT_ASSIGNMENT_COMMAND;
   } else if ( *(tag->end) == '?' && ((range_size(tag) + 1) == range_size(command))) {
      return AT_STATUS_COMMAND;
   } else {
      return AT_UNKOWN_COMMAND;
   }
}

struct range_t at_get_tag(struct range_t *range) {

   for (iterator_t it = range->begin; it != range->end; ++it) {

      if (it == range->begin) {
         if (*it == '+' || *it == '&' || *it == '^'|| is_character(*it))
            continue;
      } else {
         if (is_character(*it) || is_digit(*it))
            continue;
      }

      struct range_t result;

      result.begin = range->begin;
      result.end = it;

      return result;
   }

   return *range;
}

static struct at_command_register_t *at_find_command_register(
      struct at_context_t *ctx,
      struct range_t tag,
      enum AT_CMD_TYPE cmd_type) {

   struct at_command_register_t *p = ctx->first;

   while (p != 0){
      if (range_equals(&tag, p->tag) && p->cmd_type == cmd_type)
         return p;

      p = p->next;
   }

   return 0;
}

static void at_append_ok(struct at_context_t *ctx, uint32_t stream_id) {
   at_append_line(ctx, "", stream_id);
   at_append_line(ctx, "OK", stream_id);
   at_append_line(ctx, "", stream_id);
}

static void at_append_error(struct at_context_t *ctx, uint32_t stream_id) {
   at_append_line(ctx, "", stream_id);
   at_append_line(ctx, "ERROR", stream_id);
   at_append_line(ctx, "", stream_id);
}


static struct at_function_result at_process_command(
      struct at_context_t *ctx,
      struct range_t *command,
      uint32_t stream_id) {

   struct range_t tag = at_get_tag(command);

   enum AT_CMD_TYPE cmd_type = at_get_cmd_type(&tag, command);

   AT_TRACE("ctx: %p, command->begin: %p, command->end: %p, tag.begin: %p, tag.end: %p, type: %d", ctx, command->begin, command->end, tag.begin, tag.end, cmd_type);


   if (cmd_type != AT_UNKOWN_COMMAND) {

      range_lowercase(&tag);

      struct at_command_register_t *reg_ptr = at_find_command_register(
               ctx,
               tag,
               cmd_type
      );

      if (reg_ptr != 0) {

         struct at_function_context_t fctx;
         fctx.context = ctx;
         fctx.stream_id = stream_id;
         range_init(&fctx.parameters);

         if (cmd_type == AT_ASSIGNMENT_COMMAND) {
            fctx.parameters.begin = tag.end + 1;
            fctx.parameters.end = command->end;
         }

         struct at_function_result result;

         at_function_result_init(&result);

         result.code = 100;
         result.detailed = "Unknown error";
         result.result = false;

         reg_ptr->function(&result, &fctx, reg_ptr->udata);

         return result;
      }
   }

   struct at_function_result r;
   at_return_operation_not_supported_error(&r);
   return r;
}


static struct at_function_result at_process_chunk(
      struct at_context_t *ctx,
      struct range_t *data,
      bool first_chunk,
      uint32_t stream_id) {

   AT_TRACE("ctx = %p, begin = %p, end = %p, first_chunk = %d", ctx, data->begin, data->end, first_chunk);

   if (first_chunk) {
      struct range_t r;
      if (get_at_command(data, &r)){
         return at_process_command(ctx, &r, stream_id);
      } else {
         struct at_function_result r;
         at_return_operation_not_supported_error(&r);
         return r;
      }
   } else {
      return at_process_command(ctx, data, stream_id);
   }
}

static void at_process_commands(
      struct at_context_t *ctx,
      struct range_t *line,
      uint32_t stream_id) {

   bool qt = false;
   bool first_chunk = true;
   iterator_t b_cmd = line->begin;
   struct at_function_result result;
   at_function_result_init (&result);

   AT_TRACE("ctx = %p, begin = %p, end = %p", ctx, line->begin, line->end);

   for (iterator_t it = line->begin; it != line->end; ++it) {

      if (*it == '"') {
         qt = !qt;
      }

      if (qt == false && *it == ';') {

         struct range_t cmd_range;

         cmd_range.begin = b_cmd;
         cmd_range.end = it;

         cmd_range = range_trim(&cmd_range);

         result = at_process_chunk(ctx, &cmd_range, first_chunk, stream_id);

         if (result.result == false)
            break;

         first_chunk = false;
         b_cmd = it + 1;
      }
   }

   if (result.result == true) {
      struct range_t cmd_range = range_create_it(b_cmd, line->end);
      cmd_range = range_trim(&cmd_range);
      result = at_process_chunk(ctx, &cmd_range, first_chunk, stream_id);
   }

   if (result.result == true) {
      at_append_ok(ctx, stream_id);
   } else {

      switch (ctx->cmee_level){

      case 0:
         at_append_error(ctx, stream_id);
         break;
      case 1:
         at_append_line(ctx, "", stream_id);
         at_append_text(ctx, "+CME ERROR: ", stream_id);
         at_append_int(ctx, result.code, stream_id);
         at_append_line(ctx, "", stream_id);
         break;
      case 2:
         at_append_line(ctx, "", stream_id);
         at_append_text(ctx, "+CME ERROR: ", stream_id);
         at_append_line(ctx, result.detailed, stream_id);
      }
   }

   at_flush_output(ctx, stream_id);
}


void at_process_line(
      struct at_context_t *ctx,
      struct range_t *line,
      uint32_t stream_id) {

   struct range_t trimmed_line =  range_trim(line);
   
   AT_TRACE("ctx = %p, begin = %p, end = %p", ctx, line->begin, line->end);

   if ( range_is_empty(&trimmed_line) || range_size(&trimmed_line) < 2) {
      return;
   }

   at_process_commands(ctx, &trimmed_line, stream_id);
}

static iterator_t at_get_input_buffer_end_iterator(
      struct at_context_t *ctx,
      uint32_t stream_id) {

   return ctx->input_buffers + ((stream_id+1) * AT_INPUT_BUFFER_SIZE);
}

void at_process_input(
      struct at_context_t *ctx,
      struct range_t *data,
      uint32_t stream_id ) {

   unsigned char* input_buffer = ctx->input_buffers + (stream_id * AT_INPUT_BUFFER_SIZE);
   unsigned char* last_input_buffer = ctx->last_input_buffers + (stream_id * AT_INPUT_BUFFER_SIZE);

   AT_TRACE("ctx = %p, begin = %p, end = %p", ctx, data->begin, data->end);

   if ( range_is_empty(data)) {
      return;
   }

   if (ctx->echo) {
      at_append_range(ctx, data, stream_id);
      at_flush_output(ctx, stream_id);
   }

   for (iterator_t i = data->begin; i != data->end; ++i) {
      AT_TRACE("inputbuff_iterator=%p, input_buffer=%p, i = %p, *i = %c", ctx->inputbuff_iterators[stream_id], input_buffer, i, *i);
      if ( *i == '\r' && ctx->inputbuff_iterators[stream_id] != input_buffer) {
         struct range_t line = get_range_by_iterators(input_buffer, ctx->inputbuff_iterators[stream_id]);
         at_process_line( ctx, &line, stream_id );

         ctx->lastinbuff_iterators[stream_id] = ctx->last_input_buffers + (stream_id * AT_INPUT_BUFFER_SIZE);

         for (iterator_t i = input_buffer; i != ctx->inputbuff_iterators[stream_id]; ++i) {
            *(ctx->lastinbuff_iterators[stream_id]) = *i;
            ctx->lastinbuff_iterators[stream_id] = ctx->lastinbuff_iterators[stream_id] + 1;
         }

         ctx->inputbuff_iterators[stream_id] = input_buffer;
         continue;
      }

      if (*i == '/') {

         struct range_t line = get_range_by_iterators(input_buffer, ctx->inputbuff_iterators[stream_id]);

         if ( (range_size(&line) == 1) && ( *input_buffer == 'A' || *input_buffer == 'a' )) {

            struct range_t lline = get_range_by_iterators(last_input_buffer, ctx->lastinbuff_iterators[stream_id]);

            if (range_is_empty(&lline) == false) {
               at_process_line(ctx, &lline, stream_id);
            }

            ctx->inputbuff_iterators[stream_id] = input_buffer;
            continue;
         }
      }

      if ( ctx->inputbuff_iterators[stream_id] == at_get_input_buffer_end_iterator(ctx, stream_id)) {
         // Silently discard input buffer, on buffer overflow
         ctx->inputbuff_iterators[stream_id] = input_buffer;
         continue;
      }

      *(ctx->inputbuff_iterators[stream_id]) = *i;
      ctx->inputbuff_iterators[stream_id] = ctx->inputbuff_iterators[stream_id] + 1;
   }
}

void at_add_unsolicited(struct at_context_t *ctx, const char *prefix, const char *text, uint32_t stream_id){
   at_append_line(ctx, "", stream_id);
   at_append_text(ctx, "+", stream_id);
   at_append_text(ctx, prefix, stream_id);
   at_append_text(ctx, ": ", stream_id);
   at_append_line(ctx, text, stream_id);
   at_flush_output(ctx, stream_id);
}

void at_add_unsolicited_line(struct at_context_t *ctx, const char *text, uint32_t stream_id) {
   at_append_line(ctx, "", stream_id);
   at_append_line(ctx, text, stream_id);
   at_flush_output(ctx, stream_id);
}
