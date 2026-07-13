#include "rtp.h"
#include "util.h"
#include "seqnum.h"

#define PAYLOAD_LIMIT 1461

int rtp_handshake_sender(int sockfd, struct sockaddr_in *receiver_addr_p, uint32_t *syn_seq);
int sender_send_data(int sockfd,
                     struct sockaddr_in *receiver_addr,
                     const char *filepath,
                     int window_size,
                     int mode,
                     uint32_t *syn_seq);
int sender_send_fin(int sockfd, struct sockaddr_in *receiver_addr, uint32_t last_seq);

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        LOG_FATAL("Usage: ./sender [receiver ip] [receiver port] [file path] "
                  "[window size] [mode]\n");
    }

    // your code here
    const char *receiver_ip = argv[1];
    int receiver_port = atoi(argv[2]);
    const char *file_path = argv[3];
    if (!file_path)
    {
        LOG_FATAL("[Sender] invalid args\n");
    }
    if (!receiver_ip)
    {
        LOG_FATAL("[Sender] invalid args\n");
    }
    int window_size = atoi(argv[4]);
    // int window_size = 20000;
    int mode = atoi(argv[5]);
    if (window_size <= 0)
    {
        LOG_FATAL("[Sender] invalid window size\n");
    }
    if (mode != 0 && mode != 1)
    {
        LOG_FATAL("[Sender] invalid mode\n");
    }

    int sockfd;
    struct sockaddr_in receiver_addr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        LOG_FATAL("Socket error\n");
        exit(1);
    }

    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(receiver_port);
    if (inet_pton(AF_INET, receiver_ip, &receiver_addr.sin_addr) <= 0)
    {
        LOG_FATAL("[Sender] inet_pton failed");
        exit(1);
    }

    // 发起三次握手,随机初始化序列号
    uint32_t syn_seq = seqnum_init_random(); // 初始序列号
    if (rtp_handshake_sender(sockfd, &receiver_addr, &syn_seq) < 0)
    {
        LOG_FATAL("[Sender] Handshake failed.\n");
        close(sockfd);
        exit(1);
    }
    LOG_MSG("[Sender] Connection established successfully!\n");

    if (sender_send_data(sockfd, &receiver_addr,
                         argv[3],
                         window_size,
                         mode,
                         &syn_seq) < 0)
    {
        LOG_FATAL("[Sender] Data sending failed.\n");
        close(sockfd);
        exit(1);
    }
    LOG_MSG("[Sender] All data packets sent successfully.\n");

    // 发送 FIN 报文进行关闭
    if (sender_send_fin(sockfd, &receiver_addr, syn_seq) < 0)
    {
        LOG_FATAL("[Sender] FIN sending failed.\n");
        close(sockfd);
        exit(1);
    }
    LOG_MSG("[Sender] Connection closed successfully.\n");
    return 0;
}

int rtp_handshake_sender(int sockfd, struct sockaddr_in *receiver_addr_p, uint32_t *syn_seq)
{

    // receiver_addr这玩意是个指针, 不能直接sizeof
    socklen_t addr_len = sizeof(*receiver_addr_p);
    // printf("[DEBUG] addr_len=%d, expected=%d\n",
    //        addr_len, (int)sizeof(struct sockaddr_in));

    // ---- 第一次握手：发送 SYN ----
    uint32_t seq_num = *syn_seq;
    rtp_header_t syn_pkt = {0};
    syn_pkt.seq_num = seq_num;
    syn_pkt.length = 0;
    syn_pkt.flags = RTP_SYN;
    syn_pkt.checksum = 0;
    // 计算校验和
    syn_pkt.checksum = compute_checksum(&syn_pkt, sizeof(syn_pkt));

    int retry = 0;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT_MS * 1000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        perror("setsockopt");

    while (retry < MAX_RETRY)
    {
        // 注意指针
        ssize_t sent = sendto(sockfd, &syn_pkt, sizeof(syn_pkt), 0,
                              (struct sockaddr *)receiver_addr_p, addr_len);
        LOG_DEBUG("[Sender] sendto returned %zd, errno=%d (%s)\n", sent, errno, strerror(errno));

        // ---- 第二次握手：接受 SYN ACK ----
        rtp_header_t recv_pkt;
        ssize_t n = recvfrom(sockfd, &recv_pkt, sizeof(recv_pkt), 0,
                             (struct sockaddr *)receiver_addr_p, &addr_len);
        // 超时重传
        if (n <= 0)
        {
            LOG_DEBUG("[Sender] Timeout waiting for SYN-ACK, retry %d\n", retry + 1);
            retry++;
            continue;
        }
        // 验证校验和
        u_int32_t old_checksum = recv_pkt.checksum;
        recv_pkt.checksum = 0;
        if (recv_pkt.flags != (RTP_SYN | RTP_ACK))
        {
            LOG_DEBUG("[Sender] Received unexpected packet, retry %d\n", retry + 1);
            retry++;
            continue;
        }
        else if (recv_pkt.seq_num != seq_num + 1)
        {
            LOG_DEBUG("[Sender] Received wrong seq_num, retry %d\n", retry + 1);
            retry++;
            continue;
        }
        else if (compute_checksum(&recv_pkt, sizeof(recv_pkt)) != old_checksum)
        {
            LOG_DEBUG("[Sender] Received corrupted packet, retry %d\n", retry + 1);
            retry++;
            continue;
        }
        else
        {
            LOG_MSG("[Sender] Received packet: seq_num=%u, flags=%u\n", recv_pkt.seq_num, recv_pkt.flags);
            break;
        }
    }

    if (retry == MAX_RETRY)
    {
        LOG_FATAL("[Sender] Failed to establish connection after %d retries in 2nd handshake.\n", MAX_RETRY);
        return -1;
    }

    // ---- 第三次握手：发送 ACK ----
    rtp_header_t ack_pkt = {0};
    ack_pkt.seq_num = seqnum_next(seq_num);
    ack_pkt.length = 0;
    ack_pkt.flags = RTP_ACK;
    ack_pkt.checksum = 0;
    // 计算校验和
    ack_pkt.checksum = compute_checksum(&ack_pkt, sizeof(ack_pkt));

    sendto(sockfd, &ack_pkt, sizeof(ack_pkt), 0,
           (struct sockaddr *)receiver_addr_p, addr_len);

    // 重置重传计数器
    retry = 0;

    LOG_MSG("[Sender] Sent final ACK, waiting 2s to confirm...\n");

    // 等待2秒，看看是否再次收到SYNACK（说明Receiver没收到第三次握手）
    tv.tv_sec = WAIT_AFTER_HANDSHAKE / 1000;
    tv.tv_usec = (WAIT_AFTER_HANDSHAKE % 1000) * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 只进行一次接收尝试，超时即认为成功
    rtp_header_t tmp;
    ssize_t n = recvfrom(sockfd, &tmp, sizeof(tmp), 0,
                         (struct sockaddr *)receiver_addr_p, &addr_len);
    if (n > 0)
    {
        uint32_t old_checksum = tmp.checksum;
        tmp.checksum = 0;
        if (tmp.flags == (RTP_SYN | RTP_ACK) && tmp.seq_num == seq_num + 1 &&
            compute_checksum(&tmp, sizeof(tmp)) == old_checksum)
        {
            LOG_DEBUG("[Sender] Receiver missed ACK, resend.\n");
            sendto(sockfd, &ack_pkt, sizeof(ack_pkt), 0,
                   (struct sockaddr *)receiver_addr_p, addr_len);
        }
    }
    // 无论是否收到包，2秒后都认为握手完成

    // 修改syn_seq
    // *syn_seq = seq_num;
    // 似乎不需要修改，保持x+1

    return 0;
}

int sender_send_data(int sockfd,
                     struct sockaddr_in *receiver_addr,
                     const char *filepath,
                     int window_size,
                     int mode,
                     uint32_t *syn_seq)
{
    if (window_size > 128)
    {
        window_size = 128;
    }
    FILE *fp = fopen(filepath, "rb");
    if (!fp)
    {
        LOG_FATAL("Sender fopen failed: %s\n", strerror(errno));
    }

    rtp_packet_t *packets = NULL;
    uint8_t *acked = NULL; // 0=未确认 1=已确认
    uint8_t *used = NULL;  // 0=未使用 1=已使用
    int pkt_count = 0;

    packets = malloc(sizeof(rtp_packet_t) * window_size);
    acked = calloc(window_size, sizeof(uint8_t));
    used = calloc(window_size, sizeof(uint8_t));

    // 滑动窗口初始化
    struct timeval last_update;                // 用于超时重传
    uint32_t base_seq = seqnum_next(*syn_seq); // 第一个数据包与之前SYN结束相同 seq = x+1
    uint32_t nextseqnum = base_seq;
    int head = 0; // 窗口头序号在数组中的索引

    // 设置 socket 超时，用于接收 ACK
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT_MS * 1000; // 100ms 一次 recvfrom
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    // 当前时刻
    uint8_t timer_stopped = 0;

    uint8_t isReadOver = 0;
    uint8_t isTimeout = 0;
    uint8_t unackedCnt = 0;
    uint32_t ackedCnt = 0;
    uint32_t retransmit_count = 0; // 重传次数计数器
    // 主循环：直到文件发送完并且所有在飞的包都被确认
    while (!isReadOver || unackedCnt > 0)
    {
        LOG_DEBUG("[Sender] Main loop: base_seq=%d, pkt_count=%d, window_size=%d, unackedCnt=%d, retransmit_count=%u\n", base_seq, pkt_count, window_size, unackedCnt, retransmit_count);

        if (retransmit_count > MAX_RETRY)
        {
            free(packets);
            free(acked);
            free(used);
            LOG_FATAL("[Sender] Exceeded maximum retransmits (%u). Terminating.\n", retransmit_count);
        }
        // 1. 发送窗口内所有未发送/未确认的数据包
        if (isTimeout)
        {
            // 超时重传,重传原窗口内所有未确认的包
            for (uint32_t i = 0; i < ((nextseqnum - base_seq) & SEQNUM_MASK); i++)
            {
                uint32_t idx = (head + i) % window_size;
                if (mode == 0)
                {
                    // GBN模式下，直接重传整个窗口内的包
                    rtp_packet_t *p = &packets[idx];
                    if (sendto(sockfd, p,
                               sizeof(rtp_header_t) + p->rtp.length,
                               0,
                               (struct sockaddr *)receiver_addr,
                               sizeof(*receiver_addr)) < 0)
                    {
                        free(packets);
                        free(acked);
                        free(used);
                        LOG_FATAL("Sender sendto failed: %s\n", strerror(errno));
                    }

                    LOG_MSG("[Sender][Timeout Retransmit] Resending packet[%d]: seq=%u, length=%u, current retransmit_count=%u\n", idx,
                            p->rtp.seq_num, p->rtp.length, retransmit_count);
                }
                else if (mode == 1)
                {
                    // SR模式下，只重传窗口内未确认的包
                    if (!acked[idx])
                    {
                        rtp_packet_t *p = &packets[idx];
                        if (sendto(sockfd, p,
                                   sizeof(rtp_header_t) + p->rtp.length,
                                   0,
                                   (struct sockaddr *)receiver_addr,
                                   sizeof(*receiver_addr)) < 0)
                        {
                            free(packets);
                            free(acked);
                            free(used);
                            LOG_FATAL("Sender sendto failed: %s\n", strerror(errno));
                        }

                        LOG_MSG("[Sender][Timeout Retransmit] Resending packet[%d]: seq=%u, length=%u, current retransmit_count=%u\n", idx,
                                p->rtp.seq_num, p->rtp.length, retransmit_count);
                    }
                }
            }
            isTimeout = 0;
            // 启动计时器
            gettimeofday(&last_update, NULL);
        }
        while (nextseqnum != (base_seq + window_size) % (SEQNUM_MASK + 1))
        {
            // 循环数组
            uint32_t offset = (nextseqnum - base_seq) & SEQNUM_MASK;
            uint32_t idx = (head + offset) % window_size;
            if (!acked[idx])
            {
                rtp_packet_t *p = &packets[idx];
                // 未使用，填充数据包
                if (!used[idx])
                {
                    size_t n = fread(p->payload, 1, PAYLOAD_LIMIT, fp);
                    if (n == 0)
                    {
                        isReadOver = 1;
                        LOG_DEBUG("[Sender] Reached EOF, current unackedCnt=%d\n", unackedCnt);
                        // 不创建空包，立即跳出发送循环，等待未确认包被ACK
                        break;
                    }
                    if (n < 0)
                    {
                        free(packets);
                        free(acked);
                        free(used);
                        LOG_FATAL("Sender fread failed\n");
                    }

                    p->rtp.seq_num = nextseqnum;
                    p->rtp.length = (uint16_t)n;
                    p->rtp.flags = RTP_DAT;
                    p->rtp.checksum = 0;

                    p->rtp.checksum = compute_checksum(p, sizeof(rtp_header_t) + n);

                    pkt_count++;
                    LOG_DEBUG("[Sender] Made new packet, pkt_count=%d\n", pkt_count);

                    used[idx] = 1; // 标记为已使用
                    unackedCnt++;  // 增加未确认计数
                }

                if (sendto(sockfd, p,
                           sizeof(rtp_header_t) + p->rtp.length,
                           0,
                           (struct sockaddr *)receiver_addr,
                           sizeof(*receiver_addr)) < 0)
                {
                    free(packets);
                    free(acked);
                    free(used);
                    LOG_FATAL("Sender sendto failed: %s\n", strerror(errno));
                }

                LOG_MSG("[Sender] Sending packet[%d]: seq=%u, length=%u, current retransmit_count=%u\n", idx,
                        p->rtp.seq_num, p->rtp.length, retransmit_count);

                if (base_seq == nextseqnum)
                {
                    // 启动计时器
                    gettimeofday(&last_update, NULL);
                }
                nextseqnum = seqnum_next(nextseqnum);
            }
        }

        // 2. 接收 ACK
        uint8_t buf[1472];
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);

        while (1)
        {
            ssize_t rn = recvfrom(sockfd, buf, sizeof(buf), 0,
                                  (struct sockaddr *)&src, &slen);

            if (rn > 0)
            {
                rtp_packet_t *ack = (rtp_packet_t *)buf;

                LOG_DEBUG("[Sender] Received packet: seq=%u, flags=0x%02x, length=%u\n",
                          ack->rtp.seq_num, ack->rtp.flags, ack->rtp.length);

                // 校验和检查
                uint32_t old = ack->rtp.checksum;
                ack->rtp.checksum = 0;
                uint32_t now = compute_checksum(ack, sizeof(rtp_header_t));
                if (old != now)
                {
                    LOG_DEBUG("[Sender] Corrupted ACK packet, ignore\n");
                    ack->rtp.checksum = old;
                    continue;
                }

                if (!(ack->rtp.flags & RTP_ACK))
                {
                    LOG_DEBUG("[Sender] Incorrect ACK packet (flags=0x%02x), ignore\n", ack->rtp.flags);
                    ack->rtp.checksum = old;
                    continue;
                }

                uint32_t ack_seq = ack->rtp.seq_num;
                LOG_MSG("[Sender] Got ACK %u, base_seq=%u, pkt_count=%d\n", ack_seq, base_seq, pkt_count);

                // GBN: 根据标准 rdt 3.0 协议处理 ACK
                if (mode == 0)
                {
                    if (!seqnum_before(ack_seq, base_seq))
                    {
                        // 根据序列号直接计算包的索引，考虑回绕
                        uint32_t offset = (ack_seq - base_seq) & SEQNUM_MASK;
                        // 滑动窗口
                        // 按照题目约定，resender 返回下一个期望包的seq
                        for (uint32_t i = 0; i < offset; i++)
                        {
                            uint32_t slide_idx = (head + i) % window_size;
                            used[slide_idx] = 0;  // 释放使用标记
                            unackedCnt--;         // 减少未确认计数
                            ackedCnt++;           // 统计已确认包数
                            retransmit_count = 0; // 重置重传计数器
                            LOG_DEBUG("[Sender][GBN] Sliding window, freeing packet[%u]\n", slide_idx);
                        }
                        LOG_DEBUG("[Sender][GBN] base_seq=%u -> new base_seq=%u, nextseqnum=%u, ackedCnt=%u\n",
                                  base_seq, ack_seq, nextseqnum, ackedCnt);
                        // 循环移动 head
                        head = (head + offset) % window_size;
                        // 更新base_seq
                        // 按照题目约定，resender 返回下一个期望包的seq
                        base_seq = ack_seq;
                        if (base_seq == nextseqnum)
                        {
                            // 停止计时器，发送下一波
                            // TODO
                            timer_stopped = 1;
                            LOG_DEBUG("[Sender][GBN] All packets acked, stop timer\n");
                        }
                        else
                        {
                            retransmit_count++; // 重传整个窗口需要重试计数器加一
                            // 重启计时器
                            gettimeofday(&last_update, NULL);
                        }
                    }
                    else
                    {
                        LOG_DEBUG("[Sender][GBN] Duplicate ACK %u received, ignore\n", ack_seq);
                    }
                }
                // SR: 仅确认 ack_seq 对应的包
                else
                {
                    if (seq_in_window(ack_seq, base_seq, window_size))
                    {
                        // 根据序列号直接计算包的索引，考虑回绕
                        uint32_t offset = (ack_seq - base_seq) & SEQNUM_MASK;
                        uint32_t idx = (head + offset) % window_size;
                        uint32_t old_base = base_seq;
                        if (!acked[idx])
                        {
                            acked[idx] = 1;
                            unackedCnt--; // 减少未确认计数
                            ackedCnt++;   // 统计已确认包数
                            LOG_DEBUG("[Sender][SR] Marked packet[%u] (seq=%u) as acked\n", idx, ack_seq);
                        }
                        else
                        {
                            LOG_DEBUG("[Sender][SR] Duplicate ACK for packet[%u] (seq=%u), ignore\n", idx, ack_seq);
                        }
                        // 尝试滑动窗口
                        uint8_t isMoved = 0;
                        while (acked[head])
                        {
                            retransmit_count = 0;            // 重置重传计数器
                            acked[head] = 0;                 // 清空该槽，腾出空间
                            used[head] = 0;                  // 释放使用标记
                            head = (head + 1) % window_size; // 循环移动 head
                            base_seq = seqnum_next(base_seq);
                            isMoved = 1;
                        }
                        if (isMoved)
                        {
                            LOG_DEBUG("[Sender][SR] base_seq %u -> %u, nextseqnum=%u, ackedCnt=%u\n",
                                      old_base, base_seq, nextseqnum, ackedCnt);
                            retransmit_count = 0;
                        }
                        else if (!isMoved)
                        {
                            LOG_DEBUG("[Sender][SR] base_seq %u unchanged, nextseqnum=%u, ackedCnt=%u\n",
                                      old_base, nextseqnum, ackedCnt);
                        }
                        if (base_seq == nextseqnum)
                        {
                            // 停止计时器，发送下一波
                            // TODO
                            timer_stopped = 1;
                            LOG_DEBUG("[Sender][SR] All packets acked, stop timer\n");
                        }
                        // 窗口移动直接重启计时器
                        else if (isMoved)
                        {
                            // 重启计时器
                            gettimeofday(&last_update, NULL);
                        }
                    }
                    else
                    {
                        LOG_DEBUG("[Sender][SR] ACK %u out of window [%u, %u), ignore\n",
                                  ack_seq, base_seq, (base_seq + window_size) & SEQNUM_MASK);
                    }
                }
            }
            else if (rn < 0)
            {
                LOG_DEBUG("[Sender] recvfrom error or timeout: errno=%d (%s)\n", errno, strerror(errno));
            }

            // 3. 超时检查：窗口没有移动
            if (timer_stopped)
            {
                timer_stopped = 0;
                LOG_DEBUG("[Sender] Window All Clear, Again\n");
                // 直接回到循环开始
                break;
            }
            struct timeval nowt;
            gettimeofday(&nowt, NULL);

            long diff_ms =
                (nowt.tv_sec - last_update.tv_sec) * 1000 +
                (nowt.tv_usec - last_update.tv_usec) / 1000;

            if (diff_ms >= TIMEOUT_MS)
            {
                LOG_DEBUG("[Sender] Time Out, Retransmit (diff_ms=%ld, base_seq=%u)\n", diff_ms, base_seq);
                //! 重要
                isTimeout = 1;
                retransmit_count++; // 每次超时都增加重传计数器
                // 直接回到循环开始，重新发送窗口内的包
                break;
            }
        }
    }

    free(packets);
    free(acked);
    free(used);

    // 更新最后一个数据包的序号（用于后续 FIN 报文）
    // nextseqnum 是下一个要发送的序列号，所以最后发送的是 nextseqnum - 1
    *syn_seq = (nextseqnum - 1) & SEQNUM_MASK;

    LOG_MSG("[Sender] All data sent. Last data packet seq=%u\n", *syn_seq);
    return 0;
}

int sender_send_fin(int sockfd, struct sockaddr_in *receiver_addr, uint32_t last_seq)
{
    socklen_t addrlen = sizeof(*receiver_addr);

    // 第一次挥手：构造 FIN 报文
    rtp_header_t fin_hdr;
    memset(&fin_hdr, 0, sizeof(fin_hdr));

    fin_hdr.flags = RTP_FIN;
    fin_hdr.length = 0;
    fin_hdr.seq_num = seqnum_next(last_seq); // 在最后一个数据报文的基础上增加1
    fin_hdr.checksum = 0;
    fin_hdr.checksum = compute_checksum(&fin_hdr, sizeof(fin_hdr));

    LOG_MSG("[Sender] Sending FIN, seq=%u\n", fin_hdr.seq_num);

    // 设置接收超时，用于等待 FIN+ACK
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT_MS * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t recvbuf[1500];

    // 第一次挥手：发送 FIN + 等待 FIN+ACK
    for (int attempt = 0; attempt < MAX_RETRY; attempt++)
    {
        // 发送 FIN 报文
        ssize_t n = sendto(sockfd, &fin_hdr, sizeof(fin_hdr), 0,
                           (struct sockaddr *)receiver_addr, addrlen);
        if (n < 0)
        {
            LOG_DEBUG("[Sender] sendto FIN failed: errno=%d (%s)\n", errno, strerror(errno));
            continue;
        }

        LOG_DEBUG("[Sender] FIN sent, waiting for FIN+ACK (attempt %d/%d)...\n", attempt + 1, MAX_RETRY);

        // 等待 FIN+ACK 回复
        ssize_t rn = recvfrom(sockfd, recvbuf, sizeof(recvbuf), 0,
                              (struct sockaddr *)receiver_addr, &addrlen);

        if (rn < 0)
        {
            // 超时或出错，准备重试
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                LOG_DEBUG("[Sender] Timeout waiting for FIN+ACK, will retry\n");
                continue;
            }
            LOG_DEBUG("[Sender] recvfrom error: errno=%d (%s)\n", errno, strerror(errno));
            continue;
        }

        // 检查接收到的数据
        if ((size_t)rn < sizeof(rtp_header_t))
        {
            LOG_DEBUG("[Sender] Received packet too small, discard\n");
            continue;
        }

        rtp_header_t *recv_hdr = (rtp_header_t *)recvbuf;

        // 校验和检查
        uint32_t old = recv_hdr->checksum;
        recv_hdr->checksum = 0;
        uint32_t calc = compute_checksum(recv_hdr, sizeof(rtp_header_t));
        recv_hdr->checksum = old;
        if (old != calc)
        {
            LOG_DEBUG("[Sender] Received FIN+ACK checksum mismatch, discard\n");
            continue;
        }

        // 检查是否是 FIN+ACK，且 seq_num 和 FIN 相同
        if ((recv_hdr->flags & RTP_FIN) && (recv_hdr->flags & RTP_ACK) &&
            recv_hdr->seq_num == fin_hdr.seq_num)
        {
            LOG_MSG("[Sender] Received FIN+ACK, seq=%u. Disconnect success.\n", recv_hdr->seq_num);
            return 0;
        }
        else
        {
            LOG_DEBUG("[Sender] Received unexpected packet: flags=0x%02x, seq=%u, discard\n",
                      recv_hdr->flags, recv_hdr->seq_num);
            continue;
        }
    }

    LOG_FATAL("[Sender] Failed to disconnect after %d retries.\n", MAX_RETRY);
    return -1;
}
