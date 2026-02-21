#ifndef __xenon_soc_xenon_power_h
#define __xenon_soc_xenon_power_h


#ifdef __cplusplus
extern "C" {
#endif

void xenon_thread_startup(void);

#define XENON_SPEED_1_3  4
#define XENON_SPEED_1_2  3
#define XENON_SPEED_3_2  2
#define XENON_SPEED_FULL 1

void xenon_make_it_faster(int speed);
int  xenon_run_thread_task(int thread, void *stack, void *task);
int xenon_is_thread_task_running(int thread);
void xenon_sleep_thread(int thread);
void xenon_set_single_thread_mode();
int xenon_get_speed();

#ifdef __cplusplus
};
#endif

#endif
