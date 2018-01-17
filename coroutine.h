#ifndef C_COROUTINE_H
#define C_COROUTINE_H

/**协程已结束*/
#define COROUTINE_DEAD 0
/**协程已就绪*/
#define COROUTINE_READY 1
/**协程已执行*/
#define COROUTINE_RUNNING 2
/**协程已挂起*/
#define COROUTINE_SUSPEND 3

/**调度器*/
struct schedule;

/**协程函数类型*/
typedef void(*coroutine_func)(struct schedule *, void *ud);

struct schedule * coroutine_open(void);
void coroutine_close(struct schedule *);

int coroutine_new(struct schedule *, coroutine_func, void *ud);
void coroutine_resume(struct schedule *, int id);
int coroutine_status(struct schedule *, int id);
int coroutine_running(struct schedule *);
void coroutine_yield(struct schedule *);

#endif
