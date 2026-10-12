#include "corex_runtime_context.h"

static CorexRuntimeContext g_runtime_context = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .callback_condition = PTHREAD_COND_INITIALIZER,
    .fd = -1,
    .next_req_id = 1,
    .lifecycle = COREX_RUNTIME_UNINITIALIZED,
    .next_session_generation = 1,
    .registration_generation_next = 1,
};

CorexRuntimeContext *corex_runtime_context_get(void)
{
    return &g_runtime_context;
}

void corex_runtime_context_lock(CorexRuntimeContext *context)
{
    (void)pthread_mutex_lock(&context->mutex);
    /* The single-context correctness boundary also gates subsequent GPU
     * submissions until a queued client host function has finished. */
    while (context->callback_pending)
        (void)pthread_cond_wait(&context->callback_condition, &context->mutex);
}

void corex_runtime_context_unlock(CorexRuntimeContext *context)
{
    (void)pthread_mutex_unlock(&context->mutex);
}
