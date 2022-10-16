#include "variables.h"
#include "helpers.h"
#include "predictors.h"

static void gpu_cuda_init(int dev) {
    CUdevice cuDevice;
    CUresult res;

    cuInit(0);
    res = cuDeviceGet(&cuDevice, dev);
    if (res != CUDA_SUCCESS){
        PRINT("cannot acquire device 0\n");
    }

    res = cuCtxCreate(&cuctx, 0, cuDevice);
    if (res != CUDA_SUCCESS){
        PRINT("cannot create context\n");
    }
}

static void gpu_get_cufunc(const char* cubin, char* kname, CUfunction *func) {
    CUmodule cuModule;
    CUresult res;
    res = cuModuleLoad(&cuModule, cubin);
    if (res != CUDA_SUCCESS) {
        PRINT("cannot load module: %d\n", res);
    }

    res = cuModuleGetFunction(func, cuModule, kname);
    if (res != CUDA_SUCCESS){
        PRINT("cannot acquire kernel handle\n");
    }
}

void copy_weights(long **weights, struct GPU_state *state) {
    long *kbuf_weight_0_T_ent;
    long *kbuf_weight_1_T_ent;
    long *kbuf_bias_0_ent;
    long *kbuf_bias_1_ent;

    //initialize variables
	kbuf_weight_0_T_ent = (long*) kava_alloc(256*31*sizeof(long));
    memcpy(kbuf_weight_0_T_ent, weights[0], 256*31*sizeof(long));
    kbuf_weight_1_T_ent = (long*) kava_alloc(256*2*sizeof(long));
    memcpy(kbuf_weight_1_T_ent, weights[1], 256*2*sizeof(long));
    kbuf_bias_0_ent = (long*) kava_alloc(256*sizeof(long));
    memcpy(kbuf_bias_0_ent, weights[2], 256*sizeof(long));
    kbuf_bias_1_ent = (long*) kava_alloc(2*sizeof(long));
    memcpy(kbuf_bias_1_ent, weights[3], 2*sizeof(long));

	//check_error(cuMemAlloc((CUdeviceptr*) &state->d_weight_0_T_ent, sizeof(long) * 256*31), "cuMemAlloc ", __LINE__);
    //check_error(cuMemAlloc((CUdeviceptr*) &state->d_weight_1_T_ent, sizeof(long) * 256*2), "cuMemAlloc ", __LINE__);
    //check_error(cuMemAlloc((CUdeviceptr*) &state->d_bias_0_ent, sizeof(long) * 256), "cuMemAlloc ", __LINE__);
    //check_error(cuMemAlloc((CUdeviceptr*) &state->d_bias_1_ent, sizeof(long) * 2), "cuMemAlloc ", __LINE__);
	check_error(cuMemAlloc((CUdeviceptr*) &state->weights[0], sizeof(long) * 256*31), "cuMemAlloc ", __LINE__);
    check_error(cuMemAlloc((CUdeviceptr*) &state->weights[1], sizeof(long) * 256*2), "cuMemAlloc ", __LINE__);
    check_error(cuMemAlloc((CUdeviceptr*) &state->weights[2], sizeof(long) * 256), "cuMemAlloc ", __LINE__);
    check_error(cuMemAlloc((CUdeviceptr*) &state->weights[3], sizeof(long) * 2), "cuMemAlloc ", __LINE__);

    check_error(cuMemcpyHtoD(state->weights[0], kbuf_weight_0_T_ent, sizeof(long) * 256*31), "cuMemcpyHtoD", __LINE__);
	check_error(cuMemcpyHtoD(state->weights[1], kbuf_weight_1_T_ent, sizeof(long) * 256*2), "cuMemcpyHtoD", __LINE__);
	check_error(cuMemcpyHtoD(state->weights[2], kbuf_bias_0_ent, sizeof(long) * 256), "cuMemcpyHtoD", __LINE__);
	check_error(cuMemcpyHtoD(state->weights[3], kbuf_bias_1_ent, sizeof(long) * 2), "cuMemcpyHtoD", __LINE__);
    kava_free(kbuf_weight_0_T_ent);
    kava_free(kbuf_weight_1_T_ent);
    kava_free(kbuf_bias_0_ent);
    kava_free(kbuf_bias_1_ent);

    state->cast_weights[0] = (long*) state->weights[0];
    state->cast_weights[1] = (long*) state->weights[1];
    state->cast_weights[2] = (long*) state->weights[2];
    state->cast_weights[3] = (long*) state->weights[3];
}

//this function gets the CUfuncs and allocates memory for max_batch_size inputs
void initialize_gpu(const char* cubin_path, int max_batch_size) {
    //intialize kernels
    if (cuctx) {
        return;
    }
    gpu_cuda_init(0);
    gpu_get_cufunc(cubin_path, "_Z28prediction_final_layer_batchPlS_S_S_", &batch_linnos_final_layer_kernel);
    gpu_get_cufunc(cubin_path, "_Z26prediction_mid_layer_batchPlS_S_S_", &batch_linnos_mid_layer_kernel);
    
    check_error(cuMemAlloc((CUdeviceptr*) &d_input_vec_i, sizeof(long) * LEN_INPUT * max_batch_size), "cuMemAlloc ", __LINE__);
    check_error(cuMemAlloc((CUdeviceptr*) &d_mid_res_i, sizeof(long) *LEN_LAYER_0 * max_batch_size), "cuMemAlloc ", __LINE__);
    check_error(cuMemAlloc((CUdeviceptr*) &d_final_res_i, sizeof(long) *LEN_LAYER_1 * max_batch_size *32), "cuMemAlloc ", __LINE__);

    inputs_to_gpu = kava_alloc(LEN_INPUT * max_batch_size * sizeof(long));
    gpu_outputs = kava_alloc(64 * max_batch_size * sizeof(long));
}

void gpu_cuda_cleanup(struct GPU_state *state) {
    int i;
    for(i = 0; i <4 ; i++) {
        cuMemFree(state->weights[i]);
    }

    if (!inputs_to_gpu) {
        kava_free(inputs_to_gpu);
        inputs_to_gpu = 0;
    }
    if (!gpu_outputs) {
        kava_free(gpu_outputs);
        gpu_outputs = 0;
    }
}

void check_malloc(void *p, const char* error_str, int line) {
	if (p == NULL) PRINT("ERROR: Failed to allocate %s (line %d)\n", error_str, line);
}

//this takes one input array (with LEN_INPUT bytes) and
//expands it to N inputs, while converting to longs
void expand_input_n_times(char* input, int n) {
    int b, j;
	for(b = 0 ; b < n; b++) 
		for(j = 0; j < LEN_INPUT; j++)
			inputs_to_gpu[b*31 + j] =  (long) input[j];
}

//pass number of inputs, not bytes
void copy_inputs_to_gpu(u64 n_inputs) {
    cuMemcpyHtoDAsync(d_input_vec_i, inputs_to_gpu, sizeof(long) * LEN_INPUT * n_inputs, 0);
}

void copy_results_from_gpu(u64 n_inputs) {
    cuMemcpyDtoH(gpu_outputs, d_final_res_i, sizeof(long) * 64 * n_inputs);
}