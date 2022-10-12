#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <linux/vmalloc.h>
#include "predictors.h"
#include "lake_shm.h"

#define SET_SYSCTL_DEBUG 0

extern unsigned long sysctl_lake_enable_linnos;
extern unsigned long sysctl_lake_linnos_debug;

static char *predictor_str = "fake";
module_param(predictor_str, charp, 0444);
MODULE_PARM_DESC(predictor_str, "What predictor to use: fake, cpu, gpu, batchtest");

//adding a model to a device requires:
// 1. include the header with the weights
// 2. put device name in devices
// 3. set the pointers into a new array in weights (dont mess with the ending 0)

#include "sde.h"
#include "weights_header/w_nvme0n1.h"
#include "weights_header/w_nvme1n1.h"
#include "weights_header/w_nvme2n1.h"

const char *devices[] = {
    //"/dev/vdb",
	"/dev/nvme0n1",
	"/dev/nvme1n1",
	"/dev/nvme2n1",
	0
};

static long *weights[][4] = {
	//{weight_0_T_sde, weight_1_T_sde, bias_0_sde, bias_1_sde}
	{weight_0_T_nvme0n1, weight_1_T_nvme0n1, bias_0_nvme0n1, bias_1_nvme0n1},
	{weight_0_T_nvme1n1, weight_1_T_nvme1n1, bias_0_nvme1n1, bias_1_nvme1n1},
	{weight_0_T_nvme2n1, weight_1_T_nvme2n1, bias_0_nvme2n1, bias_1_nvme2n1},
};

//the predictor function to use
bool (*fptr)(char*,int,long**);

bool is_batch_test = false;
void batch_test_attach(void) {
	int i;
	gpu_results = kava_alloc(sizeof(bool)*128);
	window_size_hist = vmalloc(128);
	for (i=0;i<128;i++) window_size_hist[i] = 0;
	init_completion(&batch_barrier);
}
void batch_test_dettach(void) {
	int i;
	for (i=0;i<128;i++) 
		pr_warn("%d:\t%u\n", i, window_size_hist[i]);
	kava_free(gpu_results);
	vfree(window_size_hist);
}

static int parse_arg(void) {
	if (!strcmp("fake", predictor_str)) {
		fptr = fake_prediction_model;
	} else if (!strcmp("cpu", predictor_str)) {
		fptr = cpu_prediction_model;
	}else if (!strcmp("gpu", predictor_str)) {
		//fptr = gpu_prediction_model;
	} else if (!strcmp("batchtest", predictor_str)) {
		is_batch_test = true;
		fptr = batch_test;
	} else {	
		pr_warn("Invalid predictor argument\n");
		return -2;
	}
	return 0;
}

static int attach_to_queue(int idx) {
	struct block_device *dev;
	struct request_queue *q;
	long **wts = weights[idx];

	pr_warn("Attaching to queue on %s\n", devices[idx]);
	dev = blkdev_get_by_path(devices[idx], FMODE_READ|FMODE_WRITE, THIS_MODULE);
	if(IS_ERR(dev)) {
		pr_warn("Error getting dev by path (%s): %ld\n", devices[idx], PTR_ERR(dev));
		return -2;
	}
	q = bdev_get_queue(dev);
	//pr_warn("wt test  %ld %ld %ld %ld \n", wts[0][0], wts[1][0], wts[2][0], wts[3][0]);

	q->weight_0_T = wts[0];
	q->weight_1_T = wts[1];
	q->bias_0 = wts[1];
	q->bias_1 = wts[2];
	q->predictor = fptr;
	q->ml_enabled = true;
	sysctl_lake_enable_linnos = true;
	pr_warn("Attached!\n");
	return 0;
}

static int dettach_queue(int idx) {
	struct block_device *dev;
	struct request_queue *q;

	pr_warn("Dettaching queue on %s\n", devices[idx]);
	dev = blkdev_get_by_path(devices[idx], FMODE_READ|FMODE_WRITE, THIS_MODULE);
	if(IS_ERR(dev)) {
		pr_warn("Error getting dev by path (%s): %ld\n", devices[idx], PTR_ERR(dev));
		return -1;
	}
	q = bdev_get_queue(dev);

	q->ml_enabled = false;
	sysctl_lake_enable_linnos = false;
	usleep_range(100,200);
	q->predictor = 0;
	q->weight_0_T = 0;
	q->weight_1_T = 0;
	q->bias_0 = 0;
	q->bias_1 = 0;
	pr_warn("Dettached!\n");
	return 0;
}

/**
 * Program main
 */
static int __init hook_init(void)
{
	const char *devs;
	int i, err;

	sysctl_lake_linnos_debug = SET_SYSCTL_DEBUG;
	err = parse_arg();
	if(err < 0) return -2;

	if(is_batch_test) batch_test_attach();

	for(devs = devices[0], i=0 ; devs != 0 ; devs = devices[++i]) {
		err = attach_to_queue(i);
		if (err) return err;
	}

	return 0;
}

static void __exit hook_fini(void)
{
	const char *devs;
	int i, err;

	sysctl_lake_linnos_debug = 0;
	for(devs = devices[0], i=0 ; devs != 0 ; devs = devices[++i]){
		err = dettach_queue(i);
		if (err) return;
	}

	if(is_batch_test) batch_test_dettach();
}

module_init(hook_init);
module_exit(hook_fini);

MODULE_AUTHOR("Henrique Fingler");
MODULE_DESCRIPTION("kernel hook for linnos");
MODULE_LICENSE("GPL");
MODULE_VERSION(__stringify(1) "."
               __stringify(0) "."
               __stringify(0) "."
               "0");
