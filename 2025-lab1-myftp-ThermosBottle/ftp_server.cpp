#include "myftp_defs.h"

const byte MAGIC_NUMBER[MAGIC_NUMBER_LENGTH] = {0xC1, 0xA1, 0x10, 'f', 't', 'p'};
int request_processing(int sockfd, ClientContext &context);
int handle_open_conn_request(int &sockfd);
int handle_quit_request(int &sockfd);
int handle_ls_request(int &client_fd, ClientContext &context);
int handle_cd_request(int &client_fd, ClientContext &context, int payload_len);
int handle_get(int &client_fd, uint32_t payload_len, const std::filesystem::path &current_dir);
int handle_put(int &sockfd, uint32_t payload_len, const std::filesystem::path &current_dir);
int handle_sha256(int &client_sockfd, uint32_t payload_len, std::filesystem::path &current_dir);

int main(int argc, char *argv[])
{

    if (argc != 3)
    {
        std::cerr << "ERROR: Insufficient arguments. Usage: <ftp_server path> <IP> <PORT>\n";
        return -1;
    }

    // std::cerr << "hello from ftp server" << std::endl;
    const std::string server_ip = argv[1];
    int server_port = atoi(argv[2]);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    // 允许立即重用本地地址（端口），避免程序退出后短时间内 bind 失败
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        std::cerr << "ERROR: setsockopt SO_REUSEADDR failed.\n";
        close(listen_fd);
        return -1;
    }
    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &servaddr.sin_addr);

    if (bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        std::cerr << "ERROR: Failed to bind socket.\n";
        close(listen_fd);
        return -1;
    }

    // 最多16个等待连接的客户端
    if (listen(listen_fd, 16) < 0)
    {
        std::cerr << "ERROR: Failed to listen on socket.\n";
        close(listen_fd);
        return -1;
    }

    std::cout << "[Server] Listening on " << server_ip << ":" << server_port << std::endl;

    // 主循环，接受并处理客户端连接
    while (true)
    {
        sockaddr_in clientaddr{};
        socklen_t clientlen = sizeof(clientaddr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&clientaddr, &clientlen);
        if (client_fd < 0)
            continue;

        std::thread([client_fd]
                    {
        struct ClientContext client_context{
            .sockfd = client_fd,
            .current_dir = std::filesystem::current_path()
        };

        while (true)
        {
            if (request_processing(client_fd, client_context) != 0) break;
        }

        close(client_fd);
        std::cout << "[Server] Connection closed.\n"; })
            .detach();
    }
    // while (true)
    // {
    //     sockaddr_in clientaddr{};
    //     socklen_t clientlen = sizeof(clientaddr);
    //     int client_fd = accept(listen_fd, (struct sockaddr *)&clientaddr, &clientlen);
    //     if (client_fd < 0)
    //     {
    //         std::cerr << "[Server] Failed to accept connection.\n";
    //         continue; // 无需close socket
    //     }

    //     std::cout << "[Server] Accepted new connection." << std::endl;

    //     struct ClientContext client_context{
    //         .sockfd = client_fd,
    //         .current_dir = std::filesystem::current_path()};
    //     // 处理请求循环
    //     while (true)
    //     {
    //         if (request_processing(client_fd, client_context) != 0)
    //         {
    //             std::cerr << "[Server] Error processing request or connection closed. Closing connection.\n";
    //             break;
    //         }
    //     };

    //     close(client_fd);
    //     std::cout << "[Server] Connection closed.\n";
    // }

    close(listen_fd);
    std::cout << "[Server] Listened socket closed.\n";
    return 0;
}

int request_processing(int sockfd, ClientContext &context)
{
    // 读取请求头
    myftp_header request{};
    ssize_t n = safe_recv(sockfd, &request, sizeof(request));
    if (n == 0)
    {
        std::cerr << "[Server] Client closed the connection.\n";
        return -1;
    }
    else if (n != sizeof(request))
    {
        std::cerr << "[Server] Failed to read request header.\n";
        return -1;
    }

    // 校验魔数
    if (memcmp(request.m_protocol, MAGIC_NUMBER, MAGIC_NUMBER_LENGTH) != 0)
    {
        std::cerr << "[Server] Invalid protocol magic number.\n";
        return -1;
    }

    // 根据请求类型处理
    switch (request.m_type)
    {
    case OPEN_CONN_REQUEST:
        return handle_open_conn_request(sockfd);

    case LIST_REQUEST:
        return handle_ls_request(sockfd, context);

    case CHANGE_DIR_REQUEST:
    {
        uint32_t payload_len = ntohl(request.m_length) - sizeof(request);
        return handle_cd_request(sockfd, context, payload_len);
    }

    case GET_REQUEST:
    {
        uint32_t payload_len = ntohl(request.m_length) - sizeof(request);
        return handle_get(sockfd, payload_len, context.current_dir);
    }

    case PUT_REQUEST:
    {
        uint32_t payload_len = ntohl(request.m_length) - sizeof(request);
        return handle_put(sockfd, payload_len, context.current_dir);
    }
    case SHA_REQUEST:
    {
        uint32_t payload_len = ntohl(request.m_length) - sizeof(request);
        return handle_sha256(sockfd, payload_len, context.current_dir);
    }
    case QUIT_REQUEST:
        return handle_quit_request(sockfd);

    default:
        std::cerr << "[Server] Unknown request type: "
                  << std::hex << (int)request.m_type << std::dec << "\n";
        return -1;
    }
}

int handle_open_conn_request(int &sockfd)
{
    std::cout << "[Server] Received OPEN_CONN_REQUEST.\n";
    myftp_header header{
        .m_type = OPEN_CONN_REPLY,
        .m_status = STATUS_OK,
        .m_length = htonl(12)};

    // 发送回复
    ssize_t n = safe_send(sockfd, &header, sizeof(header));
    if (n != sizeof(header))
    {
        std::cerr << "[Server] Failed to send OPEN_CONN_REPLY.\n";
        return -1;
    }
    std::cout << "[Server] Sent OPEN_CONN_REPLY, handshake success!\n";
    return 0;
}

int handle_quit_request(int &sockfd)
{
    myftp_header header{
        .m_type = QUIT_REPLY,
        .m_length = htonl(12)};
    // 发送回复
    ssize_t n = safe_send(sockfd, &header, sizeof(header));
    if (n != sizeof(header))
    {
        std::cerr << "[Server] Failed to send QUIT_REPLY.\n";
        return -1;
    }
    std::cout << "[Server] Sent QUIT_REPLY, closing connection.\n";
    return -1; // 返回-1以关闭连接
}

int handle_ls_request(int &client_fd, ClientContext &context)
{
    // 执行系统 ls 命令并读取输出
    std::string command = "ls " + context.current_dir.string();
    FILE *fp = popen(command.c_str(), "r");
    if (!fp)
    {
        std::cerr << "[Server] Failed to execute ls\n";
        return -1;
    }

    char buffer[2048] = {0};
    size_t bytes = fread(buffer, 1, sizeof(buffer) - 1, fp);
    buffer[bytes] = '\0';
    pclose(fp);

    // 构造回复包
    myftp_header reply{
        .m_type = LIST_REPLY,
        .m_status = STATUS_UNUSED,
        .m_length = htonl(sizeof(myftp_header) + strlen(buffer) + 1)};

    // 发送 header + payload
    if (safe_send(client_fd, &reply, sizeof(reply)) != sizeof(reply))
    {
        std::cerr << "[Server] Failed to send LIST_REPLY header\n";
        return -1;
    }
    if (safe_send(client_fd, buffer, strlen(buffer) + 1) != (ssize_t)(strlen(buffer) + 1))
    {
        std::cerr << "[Server] Failed to send LIST_REPLY payload\n";
        return -1;
    }

    std::cout << "[Server] Sent file list (" << strlen(buffer) << " bytes)\n";
    return 0;
}

int handle_cd_request(int &client_fd, ClientContext &context, int payload_len)
{
    // test
    // unsigned char *raw = reinterpret_cast<unsigned char *>(&header);
    // for (int i = 0; i < sizeof(header); ++i)
    //     printf("%02X ", raw[i]);
    // printf("\n");

    std::string dir_name(payload_len, '\0');
    if (safe_recv(client_fd, dir_name.data(), payload_len) <= 0)
    {
        std::cerr << "[Server] Failed to read cd payload." << std::endl;
        return -1;
    }

    // 构造新路径（相对路径）
    std::filesystem::path new_path = context.current_dir / dir_name;
    myftp_header reply{
        .m_type = CHANGE_DIR_REPLY,
        .m_length = htonl(12)};

    if (std::filesystem::exists(new_path) && std::filesystem::is_directory(new_path))
    {
        context.current_dir = std::filesystem::canonical(new_path);
        reply.m_status = STATUS_OK;
        std::cout << "[Server] Changed directory to: " << context.current_dir << std::endl;
    }
    else
    {
        reply.m_status = STATUS_ERR;
        std::cerr << "[Server] Directory does not exist: " << dir_name << std::endl;
    }

    if (safe_send(client_fd, &reply, sizeof(reply)) <= 0)
    {
        std::cerr << "[Server] Failed to send cd reply." << std::endl;
        return -1;
    }

    return 0;
}

int handle_get(int &client_fd, uint32_t payload_len, const std::filesystem::path &current_dir)
{

    std::vector<char> buffer(payload_len);
    if (safe_recv(client_fd, buffer.data(), payload_len) != (ssize_t)payload_len)
    {
        std::cerr << "[Server] Failed to read GET_REQUEST payload\n";
        return 0;
    }

    std::string file_name(buffer.data());
    std::filesystem::path file_path = current_dir / file_name;

    myftp_header reply{
        .m_type = GET_REPLY,
        .m_length = htonl(sizeof(reply))};

    if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path))
    {
        reply.m_status = STATUS_ERR; // 文件不存在
        safe_send(client_fd, &reply, sizeof(reply));
        std::cerr << "[Server] File not found: " << file_name << std::endl;
        return 0;
    }

    reply.m_status = STATUS_OK; // 文件存在
    if (safe_send(client_fd, &reply, sizeof(reply)) != sizeof(reply))
    {
        std::cerr << "[Server] Failed to send GET_REPLY\n";
        return 0;
    }

    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs)
    {
        std::cerr << "[Server] Failed to open file: " << file_name << std::endl;
        return 0;
    }

    std::vector<char> file_data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    myftp_header file_hdr{
        .m_type = FILE_DATA,
        .m_status = STATUS_UNUSED,
        .m_length = htonl(sizeof(myftp_header) + file_data.size())};

    if (safe_send(client_fd, &file_hdr, sizeof(file_hdr)) != sizeof(file_hdr))
    {
        std::cerr << "[Server] Failed to send FILE_DATA header\n";
        return 0;
    }

    if (file_data.empty() || safe_send(client_fd, file_data.data(), file_data.size()) != (ssize_t)file_data.size())
    {
        std::cerr << "[Server] Failed to send FILE_DATA payload\n";
        return 0;
    }

    std::cout << "[Server] Sent file: " << file_name << " (" << file_data.size() << " bytes)\n";
    return 0;
}

int handle_put(int &sockfd, uint32_t payload_len, const std::filesystem::path &current_dir)
{
    std::string file_name(payload_len, '\0');
    if (safe_recv(sockfd, file_name.data(), payload_len) != (ssize_t)payload_len)
    {
        std::cerr << "[Server] Failed to read file name\n";
        return 0;
    }

    std::cout << "[Server] Client requests to upload file: " << file_name << std::endl;

    myftp_header reply{
        .m_type = PUT_REPLY,
        .m_length = htonl(sizeof(reply))};

    if (safe_send(sockfd, &reply, sizeof(reply)) != sizeof(reply))
    {
        std::cerr << "[Server] Failed to send PUT_REPLY\n";
        return -1;
    }

    myftp_header file_hdr{};
    if (safe_recv(sockfd, &file_hdr, sizeof(file_hdr)) != sizeof(file_hdr))
    {
        std::cerr << "[Server] Failed to read FILE_DATA header\n";
        return -1;
    }

    if (file_hdr.m_type != FILE_DATA)
    {
        std::cerr << "[Server] Unexpected message type, expect FILE_DATA\n";
        return 0;
    }

    uint32_t file_total_len = ntohl(file_hdr.m_length);
    size_t file_size = file_total_len - sizeof(file_hdr);
    std::cout << "[Server] Receiving file data (" << file_size << " bytes)...\n";

    std::vector<char> file_buf(file_size);
    size_t bytes_read = 0;
    while (bytes_read < file_size)
    {
        ssize_t n = safe_recv(sockfd, file_buf.data() + bytes_read, file_size - bytes_read);
        if (n <= 0)
        {
            std::cerr << "[Server] File data read error\n";
            return 0;
        }
        bytes_read += n;
    }

    file_name = current_dir / file_name;
    std::cout << file_name << std::endl;

    std::ofstream ofs(file_name, std::ios::binary | std::ios::trunc);
    if (!ofs)
    {
        std::cerr << "[Server] Cannot open file for writing: " << file_name << std::endl;
        return 0;
    }

    ofs.write(file_buf.data(), file_buf.size());
    ofs.close();

    std::cout << "[Server] File saved successfully: " << file_name
              << " (" << file_buf.size() << " bytes)\n";

    return 0;
}

int handle_sha256(int &client_sockfd, uint32_t payload_len, std::filesystem::path &current_dir)
{

    std::vector<char> buffer(payload_len);
    if (safe_recv(client_sockfd, buffer.data(), payload_len) <= 0)
    {
        std::cerr << "[Server] Error: failed to read SHA_REQUEST payload.\n";
        return -1;
    }

    std::string file_name(buffer.data());
    std::filesystem::path file_path = current_dir / file_name;

    myftp_header reply{
        .m_type = SHA_REPLY,
        .m_status = STATUS_OK,
        .m_length = htonl(12)};

    if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path))
    {
        reply.m_status = STATUS_ERR;
        write(client_sockfd, &reply, 12);
        std::cout << "[Server] Error: file not found: " << file_path << "\n";
        return 0;
    }

    // 文件存在，发送 SHA_REPLY（status=1）
    if (write(client_sockfd, &reply, 12) <= 0)
    {
        std::cout << "[Server] Error: failed to send SHA_REPLY.\n";
        return 0;
    }

    // 使用 popen 调用 sha256sum 命令
    std::string command = "sha256sum " + file_path.string();
    FILE *fp = popen(command.c_str(), "r");
    if (!fp)
    {
        std::cerr << "[Server] Error: failed to execute sha256sum.\n";
        return 0;
    }

    std::vector<char> sha256;
    char cbuffer[256];

    while (fgets(cbuffer, sizeof(cbuffer), fp))
    {
        sha256.insert(sha256.end(), cbuffer, cbuffer + strlen(cbuffer));
    }
    sha256.push_back('\0');
    pclose(fp);

    // 发送 FILE_DATA
    myftp_header data{
        .m_type = FILE_DATA,
        .m_status = STATUS_UNUSED,
        .m_length = htonl(12 + sha256.size())};
    if (safe_send(client_sockfd, &data, 12) < 0)
    {
        std::cerr << "[Server] Error: failed to send FILE_DATA header.\n";
        return -1;
    }
    if (safe_send(client_sockfd, sha256.data(), sha256.size()) < 0)
    {
        std::cerr << "[Server] Error: failed to send SHA256 payload.\n";
        return -1;
    }

    std::cout << "[Server] Sent SHA256 of " << sha256.data() << std::endl;
    return 0;
}

ssize_t safe_send(int &sockfd, const void *buf, size_t len)
{
    size_t total_sent = 0;
    const char *data = static_cast<const char *>(buf);

    while (total_sent < len)
    {
        ssize_t n = write(sockfd, data + total_sent, len - total_sent);

        if (n > 0)
        {
            total_sent += n;
        }
        else if (n == 0)
        {
            // 终止连接
            std::cerr << "[Client] Error: connection closed by peer\n";
            close(sockfd);
            sockfd = -1;
            return -1;
        }
        else
        {
            if (errno == EINTR)
                continue; // 被信号中断，重试
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue; // 非阻塞写缓冲区满，稍后重试
            else if (errno == EPIPE)
            {
                std::cerr << "[Client] Error: broken pipe (peer closed connection)\n";
                close(sockfd);
                sockfd = -1;
                return -1;
            }
            else
            {
                std::cerr << "[Client] Error: " << std::strerror(errno) << std::endl;
                close(sockfd);
                sockfd = -1;
                return -1;
            }
        }
    }

    return static_cast<ssize_t>(total_sent);
}

ssize_t safe_recv(int &sockfd, void *buf, size_t len)
{
    size_t total = 0; // 已经接收的字节数
    char *ptr = static_cast<char *>(buf);

    while (total < len)
    {
        ssize_t n = read(sockfd, ptr + total, len - total);

        if (n > 0)
        {
            total += n; // 正常读取
        }
        else if (n == 0)
        {
            // 对端关闭连接
            std::cerr << "[Server] Error: Connection closed by peer" << std::endl;
            close(sockfd);
            sockfd = -1;
            return total; // 返回已经接收的字节数
        }
        else
        {
            // n < 0 出错
            if (errno == EINTR)
            {
                // 被信号中断，继续读取
                continue;
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            else
            {
                perror("[Server] Error: read error");
                return -1;
            }
        }
    }

    return static_cast<ssize_t>(total); // 已成功读取 len 字节
}