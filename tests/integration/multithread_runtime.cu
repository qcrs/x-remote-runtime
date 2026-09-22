#include <cuda_runtime.h>

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

enum {
    M1S1_THREADS = 8,
    M1S1_ITERATIONS = 100,
    M1S1_ELEMENTS = 64,
};

static pthread_barrier_t g_start_barrier;

extern "C" __global__
void m1s1_add_kernel(
    float *dst,
    const float *src,
    int n,
    float add)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n)
        dst[i] = src[i] + add;
}

static void report_error(
    int worker,
    int iteration,
    const char *operation,
    cudaError_t rc)
{
    fprintf(stderr,
            "M1_S1_MULTITHREAD_ERROR worker=%d iteration=%d operation=%s rc=%d(%s)\n",
            worker,
            iteration,
            operation,
            (int)rc,
            cudaGetErrorString(rc));
}

static int run_iteration(
    int worker,
    int iteration,
    cudaStream_t stream,
    cudaEvent_t event,
    float *d_src,
    float *d_dst)
{
    float src[M1S1_ELEMENTS];
    float dst[M1S1_ELEMENTS];
    float add = (float)(worker * M1S1_ITERATIONS + iteration);
    cudaError_t rc = cudaSuccess;
    int failed = 0;

    for (int i = 0; i < M1S1_ELEMENTS; ++i) {
        src[i] = (float)(i + 1);
        dst[i] = -999.0f;
    }

    if (stream != NULL) {
        rc = cudaMemcpyAsync(
            d_src,
            src,
            sizeof(src),
            cudaMemcpyHostToDevice,
            stream);
    } else {
        rc = cudaMemcpy(
            d_src,
            src,
            sizeof(src),
            cudaMemcpyHostToDevice);
    }
    if (rc != cudaSuccess) {
        report_error(
            worker,
            iteration,
            stream != NULL ? "cudaMemcpyAsync(H2D)" : "cudaMemcpy(H2D)",
            rc);
        failed = 1;
        goto cleanup;
    }

    m1s1_add_kernel<<<dim3(1, 1, 1), dim3(M1S1_ELEMENTS, 1, 1), 0, stream>>>(
        d_dst,
        d_src,
        M1S1_ELEMENTS,
        add);

    rc = cudaPeekAtLastError();
    if (rc != cudaSuccess) {
        report_error(worker, iteration, "kernel launch", rc);
        failed = 1;
        goto cleanup;
    }

    if (stream != NULL) {
        rc = cudaMemcpyAsync(
            dst,
            d_dst,
            sizeof(dst),
            cudaMemcpyDeviceToHost,
            stream);
        if (rc != cudaSuccess) {
            report_error(worker, iteration, "cudaMemcpyAsync(D2H)", rc);
            failed = 1;
            goto cleanup;
        }

        rc = cudaEventRecord(event, stream);
        if (rc != cudaSuccess) {
            report_error(worker, iteration, "cudaEventRecord", rc);
            failed = 1;
            goto cleanup;
        }

        rc = cudaEventSynchronize(event);
        if (rc != cudaSuccess) {
            report_error(worker, iteration, "cudaEventSynchronize", rc);
            failed = 1;
            goto cleanup;
        }
    } else {
        rc = cudaDeviceSynchronize();
        if (rc != cudaSuccess) {
            report_error(worker, iteration, "cudaDeviceSynchronize", rc);
            failed = 1;
            goto cleanup;
        }

        rc = cudaMemcpy(
            dst,
            d_dst,
            sizeof(dst),
            cudaMemcpyDeviceToHost);
        if (rc != cudaSuccess) {
            report_error(worker, iteration, "cudaMemcpy(D2H)", rc);
            failed = 1;
            goto cleanup;
        }
    }

    for (int i = 0; i < M1S1_ELEMENTS; ++i) {
        float expected = src[i] + add;
        if (fabsf(dst[i] - expected) > 1e-6f) {
            fprintf(stderr,
                    "M1_S1_MULTITHREAD_ERROR worker=%d iteration=%d operation=verify index=%d expected=%f actual=%f\n",
                    worker,
                    iteration,
                    i,
                    expected,
                    dst[i]);
            failed = 1;
            break;
        }
    }

cleanup:
    return failed;
}

typedef struct {
    int worker;
    int failures;
} WorkerArgs;

static void *worker_main(void *opaque)
{
    WorkerArgs *args = (WorkerArgs *)opaque;
    int worker = args->worker;

    int barrier_rc = pthread_barrier_wait(&g_start_barrier);
    if (barrier_rc != 0 && barrier_rc != PTHREAD_BARRIER_SERIAL_THREAD) {
        fprintf(stderr,
                "M1_S1_MULTITHREAD_ERROR worker=%d operation=barrier rc=%d\n",
                worker,
                barrier_rc);
        args->failures++;
        return NULL;
    }

    cudaError_t rc = cudaMalloc(NULL, 1);
    if (rc != cudaErrorInvalidValue ||
        cudaPeekAtLastError() != cudaErrorInvalidValue ||
        cudaGetLastError() != cudaErrorInvalidValue ||
        cudaPeekAtLastError() != cudaSuccess) {
        fprintf(stderr,
                "M1_S1_MULTITHREAD_ERROR worker=%d operation=tls_last_error\n",
                worker);
        args->failures++;
    }

    cudaStream_t private_stream = NULL;
    cudaEvent_t private_event = NULL;

    rc = cudaStreamCreate(&private_stream);
    if (rc != cudaSuccess) {
        report_error(worker, -1, "cudaStreamCreate", rc);
        args->failures++;
        return NULL;
    }

    rc = cudaEventCreate(&private_event);
    if (rc != cudaSuccess) {
        report_error(worker, -1, "cudaEventCreate", rc);
        args->failures++;
        (void)cudaStreamDestroy(private_stream);
        return NULL;
    }

    float *d_src = NULL;
    float *d_dst = NULL;

    rc = cudaMalloc((void **)&d_src, M1S1_ELEMENTS * sizeof(float));
    if (rc != cudaSuccess) {
        report_error(worker, -1, "cudaMalloc(src)", rc);
        args->failures++;
        (void)cudaEventDestroy(private_event);
        (void)cudaStreamDestroy(private_stream);
        return NULL;
    }

    rc = cudaMalloc((void **)&d_dst, M1S1_ELEMENTS * sizeof(float));
    if (rc != cudaSuccess) {
        report_error(worker, -1, "cudaMalloc(dst)", rc);
        args->failures++;
        (void)cudaFree(d_src);
        (void)cudaEventDestroy(private_event);
        (void)cudaStreamDestroy(private_stream);
        return NULL;
    }

    for (int iteration = 0; iteration < M1S1_ITERATIONS; ++iteration) {
        cudaStream_t stream = (iteration % 2 == 0) ? private_stream : NULL;
        if (run_iteration(
                worker,
                iteration,
                stream,
                private_event,
                d_src,
                d_dst) != 0) {
            args->failures++;
            break;
        }
    }

    if (cudaFree(d_dst) != cudaSuccess)
        args->failures++;
    if (cudaFree(d_src) != cudaSuccess)
        args->failures++;
    if (cudaEventDestroy(private_event) != cudaSuccess)
        args->failures++;
    if (cudaStreamDestroy(private_stream) != cudaSuccess)
        args->failures++;
    return NULL;
}

int main(void)
{
    pthread_t threads[M1S1_THREADS];
    WorkerArgs args[M1S1_THREADS];
    int created = 0;

    if (pthread_barrier_init(&g_start_barrier, NULL, M1S1_THREADS) != 0) {
        fprintf(stderr, "M1_S1_MULTITHREAD_ERROR operation=barrier_init\n");
        return 2;
    }

    for (int i = 0; i < M1S1_THREADS; ++i) {
        args[i].worker = i;
        args[i].failures = 0;
        if (pthread_create(&threads[i], NULL, worker_main, &args[i]) != 0) {
            fprintf(stderr,
                    "M1_S1_MULTITHREAD_ERROR operation=thread_create worker=%d\n",
                    i);
            break;
        }
        created++;
    }

    if (created != M1S1_THREADS) {
        for (int i = 0; i < created; ++i)
            (void)pthread_cancel(threads[i]);
        for (int i = 0; i < created; ++i)
            (void)pthread_join(threads[i], NULL);
        (void)pthread_barrier_destroy(&g_start_barrier);
        return 3;
    }

    for (int i = 0; i < M1S1_THREADS; ++i)
        (void)pthread_join(threads[i], NULL);

    (void)pthread_barrier_destroy(&g_start_barrier);

    int failures = 0;
    for (int i = 0; i < M1S1_THREADS; ++i)
        failures += args[i].failures;

    printf("M1_S1_MULTITHREAD_THREADS=%d ITERATIONS=%d failures=%d\n",
           M1S1_THREADS,
           M1S1_ITERATIONS,
           failures);
    if (failures != 0) {
        printf("M1_S1_MULTITHREAD_RESULT=FAIL\n");
        return 1;
    }

    printf("M1_S1_MULTITHREAD_RESULT=PASS\n");
    return 0;
}
