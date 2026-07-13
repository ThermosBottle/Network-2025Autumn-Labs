#include "rtp.h"
#include "util.h"
#include "seqnum.h"

int rtp_handshake_receiver(int sockfd, struct sockaddr_in *sender_addr_p, uint32_t *seq_num_p);
int rtp_data_receiver(int sockfd, uint32_t *seq_num_p,
                      struct sockaddr_in *sender_addr_p,
                      const char *file_path,
                      uint32_t window_size,
                      int mode);
int rtp_handle_fin(int sockfd,
                   rtp_packet_t *pkt, ssize_t recvlen,
                   struct sockaddr_in *sender_addr);

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        LOG_FATAL("Usage: ./receiver [listen port] [file path] [window size] "
                  "[mode]\n");
    }

    // your code here

    const char *file_path = argv[2];
    uint32_t window_size = (uint32_t)atoi(argv[3]);
    int mode = atoi(argv[4]);
    if (!file_path)
    {
        LOG_FATAL("[Receiver] invalid args\n");
    }
    if (mode != 0 && mode != 1)
    {
        LOG_FATAL("[Receiver] invalid mode %d\n", mode);
    }
    if (window_size <= 0)
    {
        LOG_FATAL("[Receiver] window_size must > 0\n");
    }

    int listen_port = atoi(argv[1]);
    int sockfd;
    struct sockaddr_in recv_addr, sender_addr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        LOG_FATAL("Socket error\n");
    }

    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(listen_port);
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sockfd, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0)
    {
        close(sockfd);
        LOG_FATAL("Bind error\n");
    }
    LOG_MSG("[Receiver] Socket bound on port %d\n", listen_port);

    LOG_MSG("[Receiver] Waiting for SYN...\n");
    uint32_t seq_num = 0;
    if (rtp_handshake_receiver(sockfd, &sender_addr, &seq_num) < 0)
    {
        close(sockfd);
        LOG_FATAL("[Receiver] Handshake failed.\n");
    }
    LOG_MSG("[Receiver] Handshake completed successfully.\n");

    if (rtp_data_receiver(sockfd, &seq_num, &sender_addr, file_path, window_size, mode) < 0)
    {
        close(sockfd);
        LOG_FATAL("[Receiver] Data receiving failed.\n");
    }
    LOG_MSG("[Receiver] Data receiving completed successfully.\n");

    LOG_DEBUG("Receiver: exiting...\n");
    return 0;
}

int rtp_handshake_receiver(int sockfd, struct sockaddr_in *sender_addr_p, uint32_t *seq_num_p)
{
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 注意指针sizeof问题
    socklen_t addr_len = sizeof(*sender_addr_p);
    rtp_header_t recv_pkt;
    // ---- 第一次握手：等待 SYN ----
    // 指针！！
    ssize_t n = recvfrom(sockfd, &recv_pkt, sizeof(recv_pkt), 0,
                         (struct sockaddr *)sender_addr_p, &addr_len);

    // 验证校验和
    u_int32_t old_checksum = recv_pkt.checksum;
    recv_pkt.checksum = 0;
    if (n <= 0)
    {
        LOG_DEBUG("[Receiver] Timeout waiting for SYN, exit.\n");
        return -1;
    }
    else if (recv_pkt.flags != RTP_SYN)
    {
        LOG_DEBUG("[Receiver] Unexpected packet.\n");
        return -1;
    }
    else if (compute_checksum(&recv_pkt, sizeof(recv_pkt)) != old_checksum)
    {
        LOG_DEBUG("[Receiver] Corrupted SYN packet.\n");
        return -1;
    }

    uint32_t seq_num = recv_pkt.seq_num;
    LOG_MSG("[Receiver] Received SYN, seq=%u\n", seq_num);

    // ---- 第二次握手：发送 SYN-ACK ----
    rtp_header_t synack_pkt = {0};
    synack_pkt.seq_num = seqnum_next(seq_num); // 循环更新序列号
    synack_pkt.length = 0;
    synack_pkt.flags = RTP_SYN | RTP_ACK;
    synack_pkt.checksum = 0;
    // 计算校验和
    synack_pkt.checksum = compute_checksum(&synack_pkt, sizeof(synack_pkt));

    sendto(sockfd, &synack_pkt, sizeof(synack_pkt), 0,
           (struct sockaddr *)sender_addr_p, addr_len);

    // ---- 等待第三次握手 ----
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT_MS * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int retry = 0;

    while (retry < MAX_RETRY)
    {
        rtp_header_t ack_pkt;
        n = recvfrom(sockfd, &ack_pkt, sizeof(ack_pkt), 0,
                     (struct sockaddr *)sender_addr_p, &addr_len);
        old_checksum = ack_pkt.checksum;
        ack_pkt.checksum = 0;
        if (n <= 0)
        {
            LOG_MSG("[Receiver] Timeout, resend SYN-ACK\n");
            sendto(sockfd, &synack_pkt, sizeof(synack_pkt), 0,
                   (struct sockaddr *)sender_addr_p, addr_len);
            retry++;
        }
        else if (ack_pkt.flags == RTP_ACK &&
                 ack_pkt.seq_num == synack_pkt.seq_num &&
                 old_checksum == compute_checksum(&ack_pkt, sizeof(ack_pkt)))
        {
            LOG_MSG("[Receiver] Received final ACK, connection established!\n");
            // 更新全局 seq_num
            *seq_num_p = ack_pkt.seq_num;
            break;
        }
        else
        {
            LOG_DEBUG("[Receiver] Received unexpected packet, ignore.\n");
            retry++;
        }
    }
    if (retry == MAX_RETRY)
    {
        LOG_FATAL("[Receiver] Failed to establish connection after %d retries.\n", MAX_RETRY);
        return -1;
    }

    return 0;
}
int send_ack_packet(int sockfd, struct sockaddr_in *sender_addr_p, uint32_t ack_seq)
{
    rtp_header_t ack_pkt = {0};
    ack_pkt.seq_num = ack_seq;
    ack_pkt.length = 0;
    ack_pkt.flags = RTP_ACK;
    ack_pkt.checksum = 0;
    // 计算校验和
    ack_pkt.checksum = compute_checksum(&ack_pkt, sizeof(ack_pkt));

    socklen_t addr_len = sizeof(*sender_addr_p);
    ssize_t n = sendto(sockfd, &ack_pkt, sizeof(ack_pkt), 0,
                       (struct sockaddr *)sender_addr_p, addr_len);
    if (n != sizeof(ack_pkt))
    {
        LOG_DEBUG("[Receiver] sendto ACK failed: sent %zd bytes\n", n);
        return -1;
    }
    LOG_DEBUG("[Receiver] Sent ACK for expect seq=%u\n", ack_seq);
    return 0;
}

int rtp_data_receiver(int sockfd, uint32_t *seq_num_p,
                      struct sockaddr_in *sender_addr_p,
                      const char *file_path,
                      uint32_t window_size,
                      int mode)
{
    // sockfd: 已建立的 UDP socket，用于 recvfrom/sendto。
    // sender_addr_p: 指向 sender 的地址。
    // file_path: 接收写入的文件路径。
    // window_size: 滑动窗口大小。
    // mode: 0 => 回退N(Go-Back-N)；1 => 选择重传(Selective Repeat)。

    if (window_size > 128)
    {
        window_size = 128;
    }

    FILE *fp = fopen(file_path, "wb");
    if (!fp)
    {
        LOG_FATAL("[Receiver] fopen failed: %s\n", strerror(errno));
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        LOG_FATAL("[Receiver] setsockopt SO_RCVTIMEO failed: %s\n", strerror(errno));
        return -1;
    }

    // 窗口与缓存（仅在选择重传时需要缓存）
    uint32_t seq_num = *seq_num_p;
    // ？？？疑似不需要更新下一个
    // 确实不需要更新下一个
    uint32_t expected = seq_num; // 当前期望接收的 seq

    // 为 SR 分配缓存结构
    char **payload_buf = NULL;
    uint16_t *payload_len = NULL;
    uint8_t *received = NULL;
    uint32_t head = 0; // 窗口头序号在数组中的索引
    if (mode == 1)
    { // SR 选择重传
        payload_buf = calloc(window_size, sizeof(char *));
        payload_len = calloc(window_size, sizeof(uint16_t));
        received = calloc(window_size, sizeof(uint8_t));
        if (!payload_buf || !payload_len || !received)
        {
            LOG_DEBUG("[Receiver] memory alloc failed\n");
            fclose(fp);
            free(payload_buf);
            free(payload_len);
            free(received);
            return -1;
        }
    }

    size_t max_packet_size = sizeof(rtp_packet_t);
    rtp_packet_t *recvbuf = (rtp_packet_t *)malloc(max_packet_size);
    if (!recvbuf)
    {
        LOG_DEBUG("[Receiver] malloc failed\n");
        fclose(fp);
        free(payload_buf);
        free(payload_len);
        free(received);
        return -1;
    }

    uint32_t ackedCnt = 0;
    // 循环接收数据包
    while (1)
    {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        ssize_t n = recvfrom(sockfd, recvbuf, max_packet_size, 0,
                             (struct sockaddr *)&from, &fromlen);
        if (n < 0)
        {
            LOG_DEBUG("[Receiver] recvfrom error: Connection closed. errno=%d (%s)\n", errno, strerror(errno));
            continue;
        }
        else if ((size_t)n < sizeof(rtp_header_t))
        {
            LOG_DEBUG("[Receiver] recvfrom too small: %zd bytes, discard\n", n);
            continue;
        }

        // 校验 checksum：先保存收到的校验和，然后置 0 计算校验
        // 结构体指针（指向紧密打包的 RtpPacket）
        rtp_header_t *hdr = &recvbuf->rtp;
        uint32_t old_checksum = hdr->checksum;
        uint32_t pkt_seq = hdr->seq_num;
        uint16_t data_len = hdr->length;
        uint8_t flags = hdr->flags;

        // 验证实际接收的数据长度是否与预期一致
        uint32_t expected_len = data_len + sizeof(rtp_header_t);
        if (n != expected_len)
        {
            LOG_DEBUG("[Receiver] packet length mismatch: expected=%u, actual=%zd, discard\n", expected_len, n);
            continue;
        }

        // 计算校验和（暂时将checksum置0）
        hdr->checksum = 0;
        uint32_t new_checksum = compute_checksum((uint8_t *)recvbuf, expected_len);
        // 恢复原始checksum值
        hdr->checksum = old_checksum;

        // 验证校验和
        if (old_checksum != new_checksum)
        {
            LOG_DEBUG("[Receiver] checksum mismatch: calc=%u recv=%u, discard\n", new_checksum, old_checksum);
            continue;
        }

        // 检查是否是 FIN 包
        // sender全部确认才会发送FIN
        if (flags & RTP_FIN)
        {
            LOG_MSG("[Receiver] Received FIN packet\n");
            rtp_handle_fin(sockfd, recvbuf, n, &from);
            break;
        }

        // 只处理数据报
        if (flags != RTP_DAT)
        {
            LOG_DEBUG("[Receiver] non-data packet received (flags=0x%04x), ignore\n", flags);
            continue;
        }

        LOG_MSG("[Receiver] got data pkt seq=%u len=%u\n", pkt_seq, data_len);

        if (mode == 0)
        { // Go-Back-N (回退N) 逻辑
            if (seqnum_equal(pkt_seq, expected))
            {
                if (data_len > 0)
                {
                    size_t written = fwrite(recvbuf->payload, 1, data_len, fp);
                    if (written != data_len)
                    {
                        LOG_DEBUG("[Receiver] fwrite mismatch: want %u wrote %zu\n", data_len, written);
                        // 继续，但可视为错误
                    }
                    fflush(fp);
                }
                // GBN返回下一个期望seq
                expected = seqnum_next(expected);
                // GBN 不缓存后续乱序数据，直接发送 ACK = expected (下一个期望)
                send_ack_packet(sockfd, sender_addr_p, expected);
                ackedCnt++;
                LOG_DEBUG("[Receiver][GBN] consumed seq=%u, new expected=%u, acked count=%u\n", pkt_seq, expected, ackedCnt);
            }
            else if (seqnum_before(pkt_seq, expected))
            {
                // 已确认的包，重新发送 ACK (期望不变)
                LOG_DEBUG("[Receiver][GBN] got previously acked seq=%u, resend ACK expected=%u\n", pkt_seq, expected);
                send_ack_packet(sockfd, sender_addr_p, expected);
            }
            else
            {
                LOG_DEBUG("[Receiver][GBN] got out-of-order seq=%u (expected=%u), discard and resend ACK\n", pkt_seq, expected);
                send_ack_packet(sockfd, sender_addr_p, expected);
            }
        }
        else
        { // mode == 1 : 选择重传
            uint32_t rcvbase = expected;
            uint32_t win_start = rcvbase;
            uint32_t win_end = (rcvbase + window_size - 1) & SEQNUM_MASK;
            uint32_t reack_start = (rcvbase - window_size) & SEQNUM_MASK;
            uint32_t reack_end = (rcvbase - 1) & SEQNUM_MASK;

            if (seqnum_in_range(pkt_seq, win_start, win_end))
            {
                // 在窗口内，缓存数据
                uint32_t offset = (pkt_seq - expected) & SEQNUM_MASK;
                uint32_t idx = (head + offset) % window_size;
                if (!received[idx])
                {
                    // 缓存该 payload
                    if (data_len > 0)
                    {
                        payload_buf[idx] = malloc(data_len);
                        if (!payload_buf[idx])
                        {
                            LOG_DEBUG("[Receiver][SR] malloc failed for seq=%u len=%u\n", pkt_seq, data_len);
                            // 可选择退出或跳过该包（此处跳过）
                            send_ack_packet(sockfd, sender_addr_p, pkt_seq); // 仍发 ACK 表示收到了报头
                            continue;
                        }
                        memcpy(payload_buf[idx], recvbuf->payload, data_len);
                        payload_len[idx] = data_len;
                    }
                    else
                    {
                        payload_buf[idx] = NULL;
                        payload_len[idx] = 0;
                    }
                    // 标记已接收
                    received[idx] = 1;
                    LOG_DEBUG("[Receiver][SR] buffered seq=%u at index=%u\n", pkt_seq, idx);
                }
                // 非空缓存
                else
                {
                    LOG_DEBUG("[Receiver][SR] received duplicate seq=%u, will ack\n", pkt_seq);
                }
                // 对 SR：ACK 就是确认收到该 seq
                send_ack_packet(sockfd, sender_addr_p, pkt_seq);

                // 如果 pkt_seq == recv_base, idx = head , 把连续的已缓存数据写入文件并滑动窗口
                uint32_t advance = 0;
                while (received[head])
                {
                    // 写入 payload_buf[head]
                    if (payload_len[head] > 0 && payload_buf[head])
                    {
                        size_t written = fwrite(payload_buf[head], 1, payload_len[head], fp);
                        if (written != payload_len[head])
                        {
                            LOG_DEBUG("[Receiver][SR] fwrite mismatch: want %u wrote %zu\n", payload_len[head], written);
                        }
                        fflush(fp);
                        // 清空缓存
                        free(payload_buf[head]);
                        payload_buf[head] = NULL;
                        payload_len[head] = 0;
                    }
                    // 清空 received 标记
                    received[head] = 0;

                    // 位移
                    head = (head + 1) % window_size;
                    expected = seqnum_next(expected);
                    advance++;
                }
                if (advance)
                {
                    LOG_DEBUG("[Receiver][SR] advanced expected by %u, new expected=%u\n", advance, expected);
                }
            }
            else if (seqnum_in_range(pkt_seq, reack_start, reack_end))
            {
                // 事件 2：已经 ACK 过的旧包，重发 ACK
                LOG_DEBUG("[Receiver][SR] got already acked seq=%u, resend ACK %u\n", pkt_seq, pkt_seq);
                send_ack_packet(sockfd, sender_addr_p, pkt_seq);
                continue;
            }
            else
            {
                // 事件 3：其他情况丢弃
                LOG_DEBUG("[Receiver][SR] pkt_seq %u out of window [%u, %u) and reack [%u, %u), discard\n",
                          pkt_seq, win_start, win_end, reack_start, reack_end);
                continue;
            }
        }
    }

    // 循环结束后统一释放 recvbuf
    free(recvbuf);

    // 释放 SR 资源
    if (mode == 1)
    {
        if (payload_buf)
        {
            for (uint32_t i = 0; i < window_size; ++i)
            {
                if (payload_buf[i])
                    free(payload_buf[i]);
            }
            free(payload_buf);
        }
        free(payload_len);
        free(received);
    }

    fclose(fp);
    return 0;
}

int rtp_handle_fin(int sockfd,
                   rtp_packet_t *pkt, ssize_t recvlen,
                   struct sockaddr_in *sender_addr)
{
    uint32_t fin_seq = pkt->rtp.seq_num;

    LOG_MSG("[Receiver] Got FIN, seq=%u\n", fin_seq);

    // 构造 FIN|ACK 回复给 Sender
    rtp_packet_t reply;
    memset(&reply, 0, sizeof(reply));

    reply.rtp.seq_num = fin_seq;
    reply.rtp.length = 0;
    reply.rtp.flags = RTP_FIN | RTP_ACK;
    reply.rtp.checksum = 0;

    reply.rtp.checksum = compute_checksum((uint8_t *)&reply, sizeof(rtp_header_t));

    ssize_t n = sendto(sockfd, &reply, sizeof(rtp_header_t), 0,
                       (struct sockaddr *)sender_addr, sizeof(*sender_addr));

    if (n < 0)
    {
        LOG_FATAL("[Receiver] Failed to send FIN|ACK\n");
    }

    LOG_MSG("[Receiver] Sent FIN|ACK, seq=%u\n", fin_seq);

    // 第二阶段：等待 Sender 确认退出
    struct timeval tv;
    tv.tv_sec = WAIT_AFTER_HANDSHAKE / 1000;
    tv.tv_usec = (WAIT_AFTER_HANDSHAKE % 1000) * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[1472];
    struct sockaddr_in src;
    socklen_t slen = sizeof(src);

    while (1)
    {
        ssize_t m = recvfrom(sockfd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&src, &slen);

        if (m < 0)
        {
            // 超时 => 认为 Sender 已退出
            LOG_MSG("[Receiver] FIN wait timeout: assume Sender has stopped.\n");
            break;
        }

        // 验证接收数据长度
        if ((size_t)m < sizeof(rtp_header_t))
        {
            LOG_DEBUG("[Receiver] Received invalid FIN packet: size too small\n");
            continue;
        }

        rtp_packet_t *p = (rtp_packet_t *)buf;

        // Sender 在关闭阶段不会再发其他包，若发来 FIN，重发 FIN|ACK
        if (p->rtp.flags & RTP_FIN)
        {
            LOG_MSG("[Receiver] Received duplicate FIN, resend FIN|ACK\n");
            sendto(sockfd, &reply, sizeof(rtp_header_t), 0,
                   (struct sockaddr *)sender_addr, sizeof(*sender_addr));
            continue;
        }

        // 其他类型忽略，但不再处理
        LOG_DEBUG("[Receiver] Ignored packet after FIN\n");
    }

    LOG_MSG("[Receiver] Connection closed gracefully.\n");
    return 0;
}