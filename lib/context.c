#include <grpc-c/grpc-c.h>

#include "context.h"
#include "trace.h"
#include "metadata_array.h"
#include "stream_ops.h"

#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* __cplusplus */


/*
 * Create and initialize context
 */
grpc_c_context_t * grpc_c_context_init(grpc_c_method_t *method, int is_client)
{
    grpc_c_context_t *context = grpc_malloc(sizeof(grpc_c_context_t));
    if (context == NULL) {
		return NULL;
    }
	
    memset(context, 0, sizeof(grpc_c_context_t));
    
    context->method_url    = gpr_strdup(method->method_url);
    context->method_funcs  = method->funcs;
    context->deadline      = gpr_inf_future(GPR_CLOCK_MONOTONIC);

	context->is_client = is_client;
	
	if (is_client) {
		context->writer = grpc_c_stream_writer_init(method->client_streaming);
		context->reader = grpc_c_stream_reader_init(method->server_streaming);
		context->type.client.callback = (grpc_c_client_callback_t )method->handler;
	}else {
		context->writer = grpc_c_stream_writer_init(method->server_streaming);
		context->reader = grpc_c_stream_reader_init(method->client_streaming);
		context->type.server.callback = (grpc_c_service_callback_t )method->handler;
	}

	context->status = grpc_c_status_init(is_client);
	context->send_init_metadata = grpc_c_initial_metadata_init(1);
	context->recv_init_metadata = grpc_c_initial_metadata_init(0);

	gpr_mu_init(&context->lock);
	gpr_cv_init(&context->shutdown);

    context->state  = GRPC_C_STATE_NEW;

    return context;
}



/*
 * Free the context object
 */
void grpc_c_context_free (grpc_c_context_t *context)
{
	if (context->call) {
		grpc_call_cancel(context->call, NULL);
		grpc_call_unref(context->call);
	}

	grpc_c_initial_metadata_destory(context->send_init_metadata);
	grpc_c_initial_metadata_destory(context->recv_init_metadata);

	grpc_c_stream_reader_destory(context->reader);
	grpc_c_stream_writer_destory(context->writer);

	grpc_c_status_destory(context->status);

	gpr_mu_destroy(&context->lock);

    gpr_free(context);
}


int grpc_c_read(grpc_c_context_t *context, void **content, uint32_t flags, long timeout) {
	int ret;
	grpc_byte_buffer * output = NULL;
	
	gpr_mu_lock(&context->lock);
	if ( context->state != GRPC_C_STATE_RUN ) {
		gpr_mu_unlock(&context->lock);
		return GRPC_C_ERR_FAIL;
	}

	ret = grpc_c_stream_read(context->call, context->reader, &output, flags, timeout);
	gpr_mu_unlock(&context->lock);

	if ( ret ) {
		return ret;
	}

	if ( context->is_client ) {
		*content = context->method_funcs->output_unpacker(context, output);
	}else {
		*content = context->method_funcs->input_unpacker(context, output);
	}

	return GRPC_C_OK;	
}

int grpc_c_write(grpc_c_context_t *context, void *output, uint32_t flags, long timeout) {

	int ret;
	grpc_byte_buffer * input = NULL;

	gpr_mu_lock(&context->lock);
	if ( context->state != GRPC_C_STATE_RUN ) {
		gpr_mu_unlock(&context->lock);
		return GRPC_C_ERR_FAIL;
	}

	grpc_c_send_initial_metadata(context->call, context->send_init_metadata, timeout);

	if ( context->is_client ) {
		context->method_funcs->input_packer(output, &input);
	}else {
		context->method_funcs->output_packer(output, &input);
	}

	ret = grpc_c_stream_write(context->call, context->writer, input, flags, timeout);

	gpr_mu_unlock(&context->lock);

	return ret;
}

int grpc_c_write_done(grpc_c_context_t *context, uint32_t flags, long timeout) {

	int ret;

	gpr_mu_lock(&context->lock);
	if ( context->state != GRPC_C_STATE_RUN ) {
		gpr_mu_unlock(&context->lock);
		return GRPC_C_ERR_FAIL;
	}

	ret = grpc_c_stream_write_done(context->call, context->writer, flags, timeout);

	gpr_mu_unlock(&context->lock);

	return ret;
}


int grpc_c_client_finish(grpc_c_context_t *context, grpc_c_status_t *status, uint32_t flags) {
	int ret;
	
	gpr_mu_lock(&context->lock);
	if ( context->state != GRPC_C_STATE_RUN ) {
		gpr_mu_unlock(&context->lock);
		return GRPC_C_ERR_FAIL;
	}

	ret = grpc_c_stream_write_done(context->call, context->writer, flags, -1);
	if ( ret != GRPC_C_OK ) {
		gpr_mu_unlock(&context->lock);
		return ret;
	}
	
	ret = grpc_c_status_recv(context->call, context->status, status, flags);

	context->state = GRPC_C_STATE_DONE;

	gpr_mu_unlock(&context->lock);

	return ret;
}

int grpc_c_server_finish(grpc_c_context_t *context, grpc_c_status_t *status, uint32_t flags) {
	int ret;
	
	gpr_mu_lock(&context->lock);
	if ( context->state != GRPC_C_STATE_RUN ) {
		gpr_mu_unlock(&context->lock);
		return GRPC_C_ERR_FAIL;
	}

	ret = grpc_c_send_initial_metadata(context->call, context->send_init_metadata, -1);
	if ( ret != GRPC_C_OK ) {
		gpr_mu_unlock(&context->lock);
		return ret;
	}

	ret = grpc_c_status_send(context->call, context->status, status, flags);
	gpr_mu_unlock(&context->lock);
	
	return ret;
}

/*
 * Extracts the value for a key from the metadata array. Returns NULL if given
 * key is not present
 */
int grpc_c_get_metadata_by_key(grpc_c_context_t *context, const char *key, char *value, size_t len)
{
    return 0;
}


/*
 * Returns value for given key fro initial metadata array
 */
int grpc_c_get_initial_metadata_by_key(grpc_c_context_t *context, const char *key, char *value, size_t len)
{	
    return 0;
}

/*
 * Returns value for given key from trailing metadata array
 */
int grpc_c_get_trailing_metadata_by_key(grpc_c_context_t *context, const char *key, char *value, size_t len)
{
    return 0;
}

/*
 * Adds given key value pair to metadata array. Returns 0 on success and 1 on
 * failure
 */
int grpc_c_add_metadata (grpc_c_context_t *context, const char *key, const char *value)
{


	return GRPC_C_OK;
}

/*
 * Adds given key value pair to initial metadata array. Returns 0 on success
 * and 1 on failure
 */
int grpc_c_add_initial_metadata (grpc_c_context_t *context, const char *key, const char *value)
{

	return GRPC_C_OK;
}

/*
 * Adds given key value pair to trailing metadata array. Returns 0 on success
 * and 1 on failure
 */
int grpc_c_add_trailing_metadata (grpc_c_context_t *context, const char *key, const char *value)
{

	return GRPC_C_OK;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

				  
