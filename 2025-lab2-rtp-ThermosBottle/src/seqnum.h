#ifndef SEQNUM_H
#define SEQNUM_H

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SEQNUM_BITS 30
#define SEQNUM_MAX (1U << SEQNUM_BITS) // 2^30
#define SEQNUM_MASK (SEQNUM_MAX - 1)   // 0x3FFFFFFF

    // 初始化随机序列号
    static inline uint32_t seqnum_init_random()
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        srand(tv.tv_sec ^ tv.tv_usec);
        return rand() & SEQNUM_MASK;
    }

    // 初始化指定起点序列号
    static inline uint32_t seqnum_init(uint32_t start)
    {
        return start & SEQNUM_MASK;
    }

    // 获取下一个序列号（自动回绕）
    static inline uint32_t seqnum_next(uint32_t seq)
    {
        return (seq + 1) & SEQNUM_MASK; // 自动回绕
    }

    // 判断 seq2 是否在 seq1 之后（考虑回绕）
    static inline int seqnum_after(uint32_t seq1, uint32_t seq2)
    {
        return ((int32_t)(seq2 - seq1)) > 0;
    }

    // 判断 seq2 是否在 seq1 之前（考虑回绕）
    static inline int seqnum_before(uint32_t seq1, uint32_t seq2)
    {
        return ((int32_t)(seq1 - seq2)) < 0;
    }

    // 判断两个序列号是否相等
    static inline int seqnum_equal(uint32_t seq1, uint32_t seq2)
    {
        return seq1 == seq2;
    }
    // 判断 seq 是否在 [base, base + win) 范围内
    static inline int seq_in_window(uint32_t seq, uint32_t base, uint32_t win)
    {
        uint32_t diff = (seq - base) & SEQNUM_MASK;
        return diff < win;
    }
    // 判断 seq 是否在 [start, end]（SR window 需要闭区间判断）
    // 允许 wrap-around
    static inline int seqnum_in_range(uint32_t seq, uint32_t start, uint32_t end)
    {
        if (start <= end)
            return seq >= start && seq <= end;
        else
            return seq >= start || seq <= end; // wrap case
    }

#ifdef __cplusplus
}
#endif

#endif // SEQNUM_H
