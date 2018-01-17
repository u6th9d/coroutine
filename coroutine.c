#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#include <ucontext.h>

/**默认共享栈1M*/
#define STACK_SIZE (1024*1024)
/**默认协程数量*/
#define DEFAULT_COROUTINE 16

struct coroutine;

struct schedule {
    /**协程共享栈空间*/
    char stack[STACK_SIZE];
    /**主协程上下文*/
    ucontext_t main;
    /**协程容器包含的协程数量*/
    int nco;
    /**协程容器容量*/
    int cap;
    /**正在运行的协程id*/
    int running;
    /**协程容器*/
    struct coroutine **co;
};

struct coroutine {
    /**协程函数*/
    coroutine_func func;
    /**函数参数*/
    void *ud;
    /**运行上下文*/
    ucontext_t ctx;
    /**调度器*/
    struct schedule * sch;
    /**用于备份栈的内存大小*/
    ptrdiff_t cap;
    /**备份栈的大小*/
    ptrdiff_t size;
    /**协程运行状态*/
    int status;
    /**协程栈的备份*/
    char *stack;
};

/**创建一个协程*/
struct coroutine * _co_new(struct schedule *S, coroutine_func func, void *ud) {
    struct coroutine * co = (struct coroutine *)malloc(sizeof(*co));
    co->func = func;
    co->ud = ud;
    co->sch = S;
    co->cap = 0;
    co->size = 0;
    co->status = COROUTINE_READY;
    co->stack = NULL;
    return co;
}

/**销毁一个协程*/
void _co_delete(struct coroutine *co) {
    free(co->stack);
    free(co);
}

/**创建协程调度器*/
struct schedule * coroutine_open(void) {
    struct schedule *S = (struct schedule *)malloc(sizeof(*S));
    S->nco = 0;
    S->cap = DEFAULT_COROUTINE;
    S->running = -1;
    S->co = (struct coroutine **)malloc(sizeof(struct coroutine *) * S->cap);
    memset(S->co, 0, sizeof(struct coroutine *) * S->cap);
    return S;
}

/**销毁协程调度器*/
void coroutine_close(struct schedule *S) {
    int i;
    for (i = 0; i<S->cap; i++) {
        struct coroutine * co = S->co[i];
        if (co) {
            _co_delete(co);
        }
    }
    free(S->co);
    S->co = NULL;
    free(S);
}

/**向调度器添加一个协程*/
int coroutine_new(struct schedule *S, coroutine_func func, void *ud) {
    struct coroutine *co = _co_new(S, func, ud);
    if (S->nco >= S->cap) {	/**2倍扩容*/
        int id = S->cap;
        S->co = (struct coroutine **)realloc(S->co, S->cap * 2 * sizeof(struct coroutine *));
        memset(S->co + S->cap, 0, sizeof(struct coroutine *) * S->cap);
        S->co[S->cap] = co;
        S->cap *= 2;
        ++S->nco;
        return id;
    }
    else {	/**插入*/
        int i;
        for (i = 0; i<S->cap; i++) {
            int id = (i + S->nco) % S->cap;
            if (S->co[id] == NULL) {
                S->co[id] = co;
                ++S->nco;
                return id;
            }
        }
    }
    assert(0);
    return -1;
}

static void mainfunc(uint32_t low32, uint32_t hi32) {
    uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32);
    struct schedule *S = (struct schedule *)ptr;
    int id = S->running;
    struct coroutine *C = S->co[id];
    /**协程函数在这里被执行*/
    C->func(S, C->ud);
    _co_delete(C);
    S->co[id] = NULL;
    --S->nco;
    S->running = -1;
}

/**运行指定id的协程*/
void coroutine_resume(struct schedule * S, int id) {
    assert(S->running == -1);
    assert(id >= 0 && id < S->cap);
    struct coroutine *C = S->co[id];
    if (C == NULL)
        return;
    int status = C->status;
    uintptr_t ptr = (uintptr_t)S;
    switch (status) {
    case COROUTINE_READY:
        getcontext(&C->ctx);
        C->ctx.uc_stack.ss_sp = S->stack;
        C->ctx.uc_stack.ss_size = STACK_SIZE;
        C->ctx.uc_link = &S->main;
        S->running = id;
        C->status = COROUTINE_RUNNING;
        /**构建协程上下文*/
        makecontext(&C->ctx, (void(*)(void)) mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr >> 32));
        swapcontext(&S->main, &C->ctx);
        break;
    case COROUTINE_SUSPEND:
        /**拷贝栈空间(栈是向下增长的)*/
        memcpy(S->stack + STACK_SIZE - C->size, C->stack, C->size);
        S->running = id;
        C->status = COROUTINE_RUNNING;
        /**协程切换*/
        swapcontext(&S->main, &C->ctx);
        break;
    default:
        assert(0);
    }
}

/**备份挂起协程的栈内容*/
static void _save_stack(struct coroutine *C, char *top) {
    char dummy = 0;
    assert(top - &dummy <= STACK_SIZE);
    if (C->cap < top - &dummy) {
        free(C->stack);
        C->cap = top - &dummy;
        C->stack = (char *)malloc(C->cap);
    }
    C->size = top - &dummy;
    memcpy(C->stack, &dummy, C->size);
}

/**当前运行协程主动挂起*/
void coroutine_yield(struct schedule * S) {
    int id = S->running;
    assert(id >= 0);
    struct coroutine * C = S->co[id];
    assert((char *)&C > S->stack);
    _save_stack(C, S->stack + STACK_SIZE);
    C->status = COROUTINE_SUSPEND;
    S->running = -1;
    swapcontext(&C->ctx, &S->main);
}

/**协程运行状态*/
int coroutine_status(struct schedule * S, int id) {
    assert(id >= 0 && id < S->cap);
    if (S->co[id] == NULL) {
        return COROUTINE_DEAD;
    }
    return S->co[id]->status;
}

/**当前正在运行的协程id*/
int coroutine_running(struct schedule * S) {
    return S->running;
}
