#include <linux/sched.h>

/*
 *  Registration functions in fair.c
 */
extern void hack_mllb_register_test_fn(void (*fn_ptr)(void));
extern void hack_mllb_unregister_test_fn(void);
extern void hack_mllb_register_fn(int (*can_migrate_task_fn)(struct task_struct*, struct lb_env*));
extern void hack_mllb_unregister_fn(void);
extern void hack_mllb_lb_struct_to_feature_vec(struct task_struct *p, struct lb_env *env, int* vecs);

// this is linux's original rebalance fn, we export it to so we can test it from here
extern int can_migrate_task_linux(struct task_struct *p, struct lb_env *env);

extern void hack_mllb_lb_struct_to_feature_vec(struct task_struct *p, struct lb_env *env, int* vecs);

// just a test function that we can make the kernel call
void mllb_ping(void) {
    printk(KERN_CRIT "  ! MLLB ping!\n");
}
EXPORT_SYMBOL(mllb_ping);