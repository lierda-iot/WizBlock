/**
 * @file test_rc_net_stream.c
 * @brief 测试 rc_net_stream.h 的 recv_all 超时状态机逻辑
 *
 * 覆盖场景:
 *   1. 首字节到来前超时 -> 返回 -2 (可重试)
 *   2. 已读部分数据后超时 -> 继续等待直到凑齐
 *   3. 对端关闭 -> 返回 0
 *   4. 读取错误 -> 返回 -1
 *   5. 正常读取 -> 返回 n
 */

#include "../main/rc_net_stream.h"
#include <stdint.h>
#include <stddef.h>

/* ========== Mock 读取原语 ========== */

typedef struct {
    rc_stream_read_result_t *sequence;  /* 结果序列 */
    size_t *data_sizes;                 /* 每次返回的字节数 (仅 _DATA 时有效) */
    size_t seq_len;                     /* 序列长度 */
    size_t call_count;                  /* 已调用次数 */
} mock_reader_t;

static rc_stream_read_result_t mock_read(void *ctx, uint8_t *buf, size_t want, size_t *out_n)
{
    mock_reader_t *mock = (mock_reader_t *)ctx;
    if (mock->call_count >= mock->seq_len) {
        return RC_STREAM_READ_ERROR;  /* 超出预期调用次数 */
    }

    rc_stream_read_result_t result = mock->sequence[mock->call_count];
    if (result == RC_STREAM_READ_DATA) {
        size_t got = mock->data_sizes[mock->call_count];
        if (got > want) got = want;  /* 防御：不能超过请求字节数 */
        *out_n = got;
        /* 模拟写入数据 (填充递增字节) */
        for (size_t i = 0; i < got; i++) {
            buf[i] = (uint8_t)(mock->call_count * 10 + i);
        }
    }
    mock->call_count++;
    return result;
}

/* ========== 断言宏 ========== */

static int s_test_failed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        s_test_failed = 1; \
        return; \
    } \
} while (0)

/* ========== 测试用例 ========== */

static void test_timeout_no_data(void)
{
    /* 场景: 首字节到来前超时，应返回 -2 (允许重试) */
    rc_stream_read_result_t seq[] = { RC_STREAM_READ_TIMEOUT };
    mock_reader_t mock = { .sequence = seq, .data_sizes = NULL, .seq_len = 1, .call_count = 0 };

    uint8_t buf[10];
    int result = rc_stream_recv_all(mock_read, &mock, buf, 10);
    ASSERT(result == RC_STREAM_ALL_TIMEOUT);  /* -2 */
    ASSERT(mock.call_count == 1);
}

static void test_timeout_after_partial_data(void)
{
    /* 场景: 读到部分数据后超时，状态机应继续等待而非返回 -2 */
    rc_stream_read_result_t seq[] = {
        RC_STREAM_READ_DATA,    /* 读到 3 字节 */
        RC_STREAM_READ_TIMEOUT, /* 超时，但已有数据，应继续 */
        RC_STREAM_READ_DATA,    /* 再读到 7 字节，凑齐 10 */
    };
    size_t sizes[] = { 3, 0, 7 };
    mock_reader_t mock = { .sequence = seq, .data_sizes = sizes, .seq_len = 3, .call_count = 0 };

    uint8_t buf[10];
    int result = rc_stream_recv_all(mock_read, &mock, buf, 10);
    ASSERT(result == 10);  /* 成功读满 */
    ASSERT(mock.call_count == 3);
}

static void test_peer_closed(void)
{
    /* 场景: 对端关闭连接，返回 0 */
    rc_stream_read_result_t seq[] = { RC_STREAM_READ_CLOSED };
    mock_reader_t mock = { .sequence = seq, .data_sizes = NULL, .seq_len = 1, .call_count = 0 };

    uint8_t buf[10];
    int result = rc_stream_recv_all(mock_read, &mock, buf, 10);
    ASSERT(result == RC_STREAM_ALL_CLOSED);  /* 0 */
    ASSERT(mock.call_count == 1);
}

static void test_read_error(void)
{
    /* 场景: 读取错误，返回 -1 */
    rc_stream_read_result_t seq[] = { RC_STREAM_READ_ERROR };
    mock_reader_t mock = { .sequence = seq, .data_sizes = NULL, .seq_len = 1, .call_count = 0 };

    uint8_t buf[10];
    int result = rc_stream_recv_all(mock_read, &mock, buf, 10);
    ASSERT(result == RC_STREAM_ALL_ERROR);  /* -1 */
    ASSERT(mock.call_count == 1);
}

static void test_successful_read(void)
{
    /* 场景: 正常读取，一次凑齐 */
    rc_stream_read_result_t seq[] = { RC_STREAM_READ_DATA };
    size_t sizes[] = { 10 };
    mock_reader_t mock = { .sequence = seq, .data_sizes = sizes, .seq_len = 1, .call_count = 0 };

    uint8_t buf[10];
    int result = rc_stream_recv_all(mock_read, &mock, buf, 10);
    ASSERT(result == 10);
    ASSERT(mock.call_count == 1);
    ASSERT(buf[0] == 0 && buf[9] == 9);  /* 验证数据填充 */
}

static void test_fragmented_read(void)
{
    /* 场景: 分片读取，多次凑齐 */
    rc_stream_read_result_t seq[] = {
        RC_STREAM_READ_DATA,  /* 2 字节 */
        RC_STREAM_READ_DATA,  /* 3 字节 */
        RC_STREAM_READ_DATA,  /* 5 字节，总共 10 */
    };
    size_t sizes[] = { 2, 3, 5 };
    mock_reader_t mock = { .sequence = seq, .data_sizes = sizes, .seq_len = 3, .call_count = 0 };

    uint8_t buf[10];
    int result = rc_stream_recv_all(mock_read, &mock, buf, 10);
    ASSERT(result == 10);
    ASSERT(mock.call_count == 3);
}

static void test_zero_length(void)
{
    /* 场景: 请求读取 0 字节，应立即返回 0 (无需调用原语) */
    mock_reader_t mock = { .sequence = NULL, .data_sizes = NULL, .seq_len = 0, .call_count = 0 };

    uint8_t buf[1];
    int result = rc_stream_recv_all(mock_read, &mock, buf, 0);
    ASSERT(result == 0);
    ASSERT(mock.call_count == 0);  /* 未调用读取原语 */
}

static void test_invalid_params(void)
{
    /* 场景: 空指针参数，返回错误 */
    uint8_t buf[10];
    int result = rc_stream_recv_all(NULL, NULL, buf, 10);
    ASSERT(result == RC_STREAM_ALL_ERROR);

    mock_reader_t mock = { .sequence = NULL, .data_sizes = NULL, .seq_len = 0, .call_count = 0 };
    result = rc_stream_recv_all(mock_read, &mock, NULL, 10);
    ASSERT(result == RC_STREAM_ALL_ERROR);
}

/* ========== 主函数 ========== */

int main(void)
{
    test_timeout_no_data();
    test_timeout_after_partial_data();
    test_peer_closed();
    test_read_error();
    test_successful_read();
    test_fragmented_read();
    test_zero_length();
    test_invalid_params();

    return s_test_failed;
}
