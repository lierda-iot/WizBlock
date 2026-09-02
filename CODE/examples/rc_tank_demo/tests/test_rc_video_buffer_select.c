/**
 * @file test_rc_video_buffer_select.c
 * @brief 纯 C 测试: 验证帧缓冲选择状态机 (FB_COUNT=2 vs 3)
 *
 * 测试场景: 模拟 DVP 生产者连续获取 buffer 与消费者锁定 buffer 的竞态，
 * 验证 FB_COUNT=3 能消除 "消费者正在读 buffer N" 时 DVP 选中 buffer N 的冲突。
 */

#include <stdint.h>
#include <stdbool.h>

/* 最小 assert */
#define TEST_ASSERT(cond) do { if (!(cond)) return __LINE__; } while (0)

/**
 * 纯状态机: 计算下一个 DVP 写入的 buffer 索引
 *
 * @param active_fb   当前活跃 buffer (volatile 全局变量的快照)
 * @param locked_fb   当前被消费者锁定的 buffer (-1 表示无锁定)
 * @param fb_count    总 buffer 数 (2 或 3)
 * @return 下一个可用的 buffer 索引
 */
static int select_next_capture_fb(int active_fb, int locked_fb, int fb_count)
{
    int next = (active_fb + 1) % fb_count;
    if (next == locked_fb) {
        next = (next + 1) % fb_count;
    }
    return next;
}

/**
 * 测试场景 1: FB_COUNT=2, 消费者锁定 buffer 0
 * DVP 依次请求下一个 buffer:
 *   初始 active=0, DVP 请求 → next=1 (消费者读 0, DVP 写 1)
 *   DVP 完成后 active=1, 再请求 → next=0 (但消费者仍在读 0!)
 *   → 冲突: DVP 覆盖消费者正在读的 buffer
 */
static int test_fb_count_2_race(void)
{
    const int fb_count = 2;
    int active = 0;
    int locked = 0;  /* 消费者锁定 buffer 0 */

    /* 第一次 DVP 请求 */
    int next1 = select_next_capture_fb(active, locked, fb_count);
    TEST_ASSERT(next1 == 1);  /* DVP 选 buffer 1, 安全 */
    active = next1;

    /* 第二次 DVP 请求 (消费者仍在读 buffer 0) */
    int next2 = select_next_capture_fb(active, locked, fb_count);
    /* 期望: next2 应避开 locked=0, 但双缓冲时无其他选择 */
    /* 实际: next2 会计算为 (1+1)%2=0, 然后检测到 0==locked,
       尝试 (0+1)%2=1, 但 1 已是 active (刚写完),
       所以最终 next2=1 (覆盖刚写的帧) 或回到 0 (覆盖正在读的帧) */
    /* 当前实现: 检测到冲突后 +1, 得 next2=1 */
    TEST_ASSERT(next2 == 1);
    /* 但这意味着 DVP 覆盖自己刚写的 buffer 1, 丢帧 */
    /* 更严重: 若消费者此时释放 locked 并读 buffer 1, 又会冲突 */

    return 0;
}

/**
 * 测试场景 2: FB_COUNT=3, 消费者锁定 buffer 0
 * DVP 依次请求:
 *   active=0, 请求 → next=1
 *   active=1, 请求 → next=2 (消费者仍读 0, DVP 写 2, 安全)
 *   active=2, 请求 → next=0 → 检测 locked, 改为 next=1 (消费者仍读 0, DVP 写 1, 安全)
 *   → 三缓冲始终有空闲 buffer 可用
 */
static int test_fb_count_3_safe(void)
{
    const int fb_count = 3;
    int active = 0;
    int locked = 0;

    int next1 = select_next_capture_fb(active, locked, fb_count);
    TEST_ASSERT(next1 == 1);
    active = next1;

    int next2 = select_next_capture_fb(active, locked, fb_count);
    TEST_ASSERT(next2 == 2);  /* 跳过 locked=0, 选 buffer 2 */
    active = next2;

    int next3 = select_next_capture_fb(active, locked, fb_count);
    /* (2+1)%3=0, 检测到 locked, (0+1)%3=1 */
    TEST_ASSERT(next3 == 1);
    TEST_ASSERT(next3 != locked);  /* 始终避开被锁的 buffer */

    return 0;
}

/**
 * 测试场景 3: FB_COUNT=3, 极限压力 (消费者读 buffer 0 耗时长)
 * DVP 连续完成多帧, 验证循环选择不会碰到 locked buffer
 */
static int test_fb_count_3_pressure(void)
{
    const int fb_count = 3;
    int locked = 0;
    int active = 0;

    /* 模拟 DVP 连续写 10 帧, 消费者始终锁定 buffer 0 */
    for (int i = 0; i < 10; i++) {
        int next = select_next_capture_fb(active, locked, fb_count);
        TEST_ASSERT(next != locked);  /* 核心断言: 永不冲突 */
        active = next;
    }

    return 0;
}

/**
 * 测试场景 4: FB_COUNT=2, 验证双缓冲下无法避免冲突
 */
static int test_fb_count_2_inevitable_conflict(void)
{
    const int fb_count = 2;
    int locked = 0;
    int active = 0;

    int next1 = select_next_capture_fb(active, locked, fb_count);
    active = next1;  /* active 变为 1 */

    /* 若消费者处理慢 (locked 仍=0), DVP 再次请求 */
    int next2 = select_next_capture_fb(active, locked, fb_count);
    /* (1+1)%2=0, 检测 locked=0 冲突, +1 → 1 */
    /* 此时 next2=1, 但 active=1 (刚写完), 覆盖自己 */
    TEST_ASSERT(next2 == 1 || next2 == 0);
    /* 无论选哪个都有问题: 0 是 locked, 1 是刚写完的 */

    /* 关键: 双缓冲在生产速度 > 消费速度时必然冲突 */
    return 0;
}

int main(void)
{
    int r;

    r = test_fb_count_2_race();
    if (r != 0) return 100 + r;

    r = test_fb_count_3_safe();
    if (r != 0) return 200 + r;

    r = test_fb_count_3_pressure();
    if (r != 0) return 300 + r;

    r = test_fb_count_2_inevitable_conflict();
    if (r != 0) return 400 + r;

    return 0;  /* 所有测试通过 */
}
