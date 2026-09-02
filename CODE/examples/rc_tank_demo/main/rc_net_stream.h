/**
 * @file rc_net_stream.h
 * @brief TCP 流式读取的纯状态机逻辑 (可主机端纯 C 测试)
 *
 * 将 recv_all 的超时/关闭/错误判定从 socket API 中剥离，
 * 通过依赖注入的读取原语驱动，使核心逻辑可在无 lwIP 的主机环境测试。
 * 生产代码 (rc_net.c) 与 host 测试共享本文件的同一份逻辑。
 */
#ifndef RC_NET_STREAM_H
#define RC_NET_STREAM_H

#include <stdint.h>
#include <stddef.h>

/* 读取原语的结果语义 (对应 recv() + errno 的抽象) */
typedef enum {
    RC_STREAM_READ_DATA = 0,   /* 成功读到 *out_n (>0) 字节 */
    RC_STREAM_READ_TIMEOUT,    /* 超时 (EAGAIN/EWOULDBLOCK)，未读到数据 */
    RC_STREAM_READ_CLOSED,     /* 对端关闭 (recv 返回 0) */
    RC_STREAM_READ_ERROR,      /* 其他不可恢复错误 */
} rc_stream_read_result_t;

/* recv_all 状态机的整体返回码 */
#define RC_STREAM_ALL_CLOSED   (0)   /* 对端关闭 */
#define RC_STREAM_ALL_ERROR    (-1)  /* 读取错误 */
#define RC_STREAM_ALL_TIMEOUT  (-2)  /* 超时且一个字节都未读到 (可重试，不关连接) */

/*
 * 读取原语回调: 尝试把最多 want 字节读入 buf。
 * - 返回 RC_STREAM_READ_DATA 时，*out_n 为本次实际读到的字节数 (1..want)
 * - 其他返回值时，*out_n 语义未定义 (调用方不使用)
 */
typedef rc_stream_read_result_t (*rc_stream_read_fn)(void *ctx,
                                                     uint8_t *buf,
                                                     size_t want,
                                                     size_t *out_n);

/*
 * 完整读取 n 字节的纯状态机。
 *
 * 语义 (与 rc_net.c recv_all 一致):
 *   返回 n  : 成功读满 n 字节
 *   返回 0  : 对端关闭连接 (RC_STREAM_ALL_CLOSED)
 *   返回 -1 : 读取错误       (RC_STREAM_ALL_ERROR)
 *   返回 -2 : 超时且未读到任何数据 (RC_STREAM_ALL_TIMEOUT，可重试)
 *
 * 关键行为:
 *   - 首字节到来前超时 -> 返回 -2 (调用方可重试，不应关闭连接)
 *   - 已读到部分数据后超时 -> 继续等待 (包应连续到达，避免流错位)
 *   - n == 0 时立即返回 0 (无需读取)
 */
static inline int rc_stream_recv_all(rc_stream_read_fn read_fn,
                                     void *ctx,
                                     uint8_t *buf,
                                     size_t n)
{
    size_t received = 0U;

    if (NULL == read_fn || (NULL == buf && n > 0U)) {
        return RC_STREAM_ALL_ERROR;
    }

    while (received < n) {
        size_t got = 0U;
        rc_stream_read_result_t r = read_fn(ctx, buf + received, n - received, &got);

        if (RC_STREAM_READ_DATA == r) {
            /* 防御: 原语声称读到数据但字节数为 0，视为无进展错误 */
            if (0U == got) {
                return RC_STREAM_ALL_ERROR;
            }
            received += got;
        } else if (RC_STREAM_READ_TIMEOUT == r) {
            if (0U == received) {
                return RC_STREAM_ALL_TIMEOUT;  /* 尚无数据，允许重试 */
            }
            /* 已读部分数据，继续等待剩余字节 */
        } else if (RC_STREAM_READ_CLOSED == r) {
            return RC_STREAM_ALL_CLOSED;
        } else {
            return RC_STREAM_ALL_ERROR;
        }
    }

    return (int)received;
}

#endif /* RC_NET_STREAM_H */
