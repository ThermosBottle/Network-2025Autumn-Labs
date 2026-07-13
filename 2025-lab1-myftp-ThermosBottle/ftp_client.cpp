#include "myftp_defs.h"

int handle_open(const std::string &ip, const int &port, int &sockfd);
int send_open_conn_request(int &sockfd);
int recv_open_conn_reply(int &sockfd);

int handle_ls(int &sockfd);
int send_ls_request(int &sockfd);
int recv_ls_reply(int &sockfd, char *buffer);

int handle_cd(int &sockfd, std::string &dir);
int send_cd_request(int &sockfd, const std::string &dir);
int recv_cd_reply(int &sockfd, const std::string &dir);

int handle_get(int &sockfd, const std::string &file_name);
int handle_put(int &sockfd, const std::string &file_name);

int handle_sha256(int sockfd, const std::string &file_name);

int handle_quit(int &sockfd);
int send_quit_request(int &sockfd);
int recv_quit_reply(int &sockfd);

const byte MAGIC_NUMBER[MAGIC_NUMBER_LENGTH] = {0xC1, 0xA1, 0x10, 'f', 't', 'p'};

int main()
{
    // std::cerr << "hello from ftp client" << std::endl;
    int sockfd = -1;
    std::string server_ip = "disconnected";
    while (true)
    {
        if (sockfd < 0)
            server_ip = "disconnected";
        std::cout << "ftp[" << server_ip << "]> ";
        std::string input;
        std::getline(std::cin, input);
        std::cout << input << std::endl;
        std::istringstream iss(input);
        std::string command;
        iss >> command;

        if (command == "open")
        {
            std::string ip;
            int port;
            if (!(iss >> ip >> port))
            {
                std::cerr << "[Client] Error: missing IP or port. Usage: open <IP> <PORT>.\n";
                continue;
            }
            std::cout << "[Client] Connecting to " << ip << " on port " << port << std::endl;
            if (handle_open(ip, port, sockfd) == 0)
            {
                server_ip = ip + ":" + std::to_string(port);
            }
            else
            {
                std::cerr << "[Client] Error: failed to open connection.\n";
            }
        }
        else if (command == "ls")
        {
            if (sockfd < 0)
            {
                std::cerr << "[Client] Error: Not connected to any server. Use 'open <IP> <PORT>' first.\n";
                continue;
            }
            if (handle_ls(sockfd) != 0)
            {
                std::cerr << "[Client] Error: ls command failed.\n";
            }
        }
        else if (command == "cd")
        {
            if (sockfd < 0)
            {
                std::cerr << "[Client] Error: Not connected to any server. Use 'open <IP> <PORT>' first.\n";
                continue;
            }
            std::string dir;
            if (!(iss >> dir))
            {
                std::cerr << "[Client] Error: missing directory name. Usage: cd <DIRECTORY>.\n";
                continue;
            }
            if (handle_cd(sockfd, dir) != 0)
            {
                std::cerr << "[Client] Error: cd command failed.\n";
            };
        }
        else if (command == "get")
        {
            if (sockfd < 0)
            {
                std::cerr << "[Client] Error: Not connected to any server. Use 'open <IP> <PORT>' first.\n";
                continue;
            }
            std::string file_name;
            if (!(iss >> file_name))
            {
                std::cerr << "[Client] Error: missing file name. Usage: get <FILENAME>.\n";
                continue;
            }
            if (handle_get(sockfd, file_name) != 0)
            {
                std::cerr << "[Client] Error: get command failed.\n";
            };
        }
        else if (command == "put")
        {
            if (sockfd < 0)
            {
                std::cerr << "[Client] Error: Not connected to any server. Use 'open <IP> <PORT>' first.\n";
                continue;
            }
            std::string file_name;
            if (!(iss >> file_name))
            {
                std::cerr << "[Client] Error: missing file name. Usage: put <FILENAME>.\n";
                continue;
            }
            if (handle_put(sockfd, file_name) != 0)
            {
                std::cerr << "[Client] Error: put command failed.\n";
            };
        }
        else if (command == "sha256")
        {
            if (sockfd < 0)
            {
                std::cerr << "[Client] Error: Not connected to any server. Use 'open <IP> <PORT>' first.\n";
                continue;
            }
            std::string file_name;
            if (!(iss >> file_name))
            {
                std::cerr << "[Client] Error: missing file name. Usage: sha256 <FILENAME>.\n";
                continue;
            }
            if (handle_sha256(sockfd, file_name) != 0)
            {
                std::cerr << "[Client] Error: sha256 command failed.\n";
            };
        }
        else if (command == "quit")
        {
            if (sockfd < 0)
            {
                std::cout << "Exiting FTP client." << std::endl;
                exit(0);
            }
            if (handle_quit(sockfd) != 0)
            {
                std::cerr << "[Client] Error: quit command failed.\n";
            }
            else
            {
                server_ip = "disconnected";
            };
        }
        else
        {
            std::cout << "Unknown command: " << command << std::endl;
        }
    }
    return 0;
}

// 建立连接，返回 socket 描述符
int handle_open(const std::string &ip, const int &port, int &sockfd)
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr); // c++string to cstring

    if (connect(sockfd, (sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        std::cerr << "[Client] Failed to establish TCP connection.\n";
        close(sockfd);
        sockfd = -1;
        return -1;
    }

    std::cout << "[Client] TCP connection established.\n";

    // 发送OPEN_CONN_REQUEST
    if (send_open_conn_request(sockfd) < 0)
    {
        std::cerr << "[Client] Failed to send OPEN_CONN_REQUEST. Socket closed.\n";
        close(sockfd);
        sockfd = -1;
        return -1;
    }

    // 等待OPEN_CONN_REPLY
    if (recv_open_conn_reply(sockfd) < 0)
    {
        std::cerr << "[Client] Failed to receive OPEN_CONN_reply. Socket closed.\n";
        close(sockfd);
        sockfd = -1;
        return -1;
    }

    std::cout << "[Client] MyFTP handshake success.\n";
    return 0;
}

int send_open_conn_request(int &sockfd)
{
    myftp_header header{
        .m_type = OPEN_CONN_REQUEST,
        .m_status = STATUS_UNUSED, // 未使用字段
        .m_length = htonl(12)};

    // 发送header
    ssize_t sent = safe_send(sockfd, &header, sizeof(header));
    if (sent != sizeof(header))
        return -1;

    std::cout << "[Client] Sent OPEN_CONN_REQUEST (" << sent << " bytes)." << std::endl;
    return 0;
}
int recv_open_conn_reply(int &sockfd)
{
    myftp_header reply{};

    // 接收报文
    ssize_t n = safe_recv(sockfd, &reply, sizeof(reply));
    if (n == 0)
    {
        std::cerr << "[Client] Server closed the connection.\n";
        return -1;
    }
    else if (n != sizeof(reply))
    {
        std::cerr << "[Client] Failed to read full header. Not a MyFTP server?\n";
        return -1;
    }

    // 校验魔数
    if (memcmp(reply.m_protocol, MAGIC_NUMBER, MAGIC_NUMBER_LENGTH) != 0)
    {
        std::cerr << "[Client] Invalid protocol magic number.\n";
        return -1;
    }

    // 校验类型
    if (reply.m_type != OPEN_CONN_REPLY)
    {
        std::cerr << "[Client] Unexpected reply type.\n";
        return -1;
    }

    // 校验状态
    if (reply.m_status != STATUS_OK)
    {
        std::cerr << "[Client] Server refused connection.\n";
        return -1;
    }

    std::cout << "[Client] Received OPEN_CONN_REPLY, handshake success!\n";
    return 0;
}

int handle_ls(int &sockfd)
{
    char buffer[2048]{0};
    if (send_ls_request(sockfd) != 0)
        return -1;
    if (recv_ls_reply(sockfd, buffer) != 0)
        return -1;
    // 打印文件列表
    std::cout << "[Client] Server files:\n"
              << buffer << std::endl;
    return 0;
}

int send_ls_request(int &sockfd)
{
    // 发送 LIST_REQUEST
    myftp_header request{
        .m_type = LIST_REQUEST,
        .m_status = STATUS_UNUSED,
        .m_length = htonl(12)};
    int n = safe_send(sockfd, &request, sizeof(request));
    if (n != sizeof(request))
    {
        std::cerr << "[Client] Failed to send LIST_REQUEST.\n";
        return -1;
    }
    return 0;
}

int recv_ls_reply(int &sockfd, char *buffer)
{
    // 接收 LIST_REPLY
    myftp_header reply{};
    ssize_t n = safe_recv(sockfd, &reply, sizeof(reply));
    if (n != sizeof(reply))
    {
        std::cerr << "[Client] Failed to read LIST_REPLY header.\n";
        return -1;
    }

    // 校验魔数和类型
    if (memcmp(reply.m_protocol, MAGIC_NUMBER, MAGIC_NUMBER_LENGTH) != 0 ||
        reply.m_type != LIST_REPLY)
    {
        std::cerr << "[Client] Invalid LIST_REPLY\n";
        return -1;
    }

    // 读取 payload
    uint32_t payload_len = ntohl(reply.m_length) - sizeof(myftp_header);
    if (payload_len == 1) // '\0' only
    {
        std::cout << "[Client] No files in server directory.\n";
        return 0;
    }

    if (payload_len > 2048)
        payload_len = 2048; // 防止超长

    n = safe_recv(sockfd, buffer, payload_len);
    if (n <= 0)
    {
        std::cerr << "[Client] Failed to read LIST_REPLY payload.\n";
        return -1;
    }

    return 0;
}

int handle_cd(int &sockfd, std::string &dir)
{
    for (char c : dir)
    {
        if (c == '\0')
            break;
        if (!std::isalnum(static_cast<unsigned char>(c)))
        {
            std::cout << "[Client] Invalid character in directory name: " << c << std::endl;
            return -1;
        }
    }
    if (send_cd_request(sockfd, dir) < 0)
    {
        return -1;
    }
    if (recv_cd_reply(sockfd, dir) < 0)
    {
        return -1;
    }
    return 0;
}
int send_cd_request(int &sockfd, const std::string &dir)
{
    // 发送 CHANGE_DIR_REQUEST
    myftp_header header{
        .m_type = CHANGE_DIR_REQUEST,
        .m_status = STATUS_UNUSED,
        .m_length = htonl(12 + dir.size() + 1)}; // +1 for null terminator

    // test
    // std::cout << ntohl(header.m_length) << std::endl;
    // unsigned char *raw = reinterpret_cast<unsigned char *>(&header);
    // for (int i = 0; i < sizeof(header); ++i)
    //     printf("%02X ", raw[i]);
    // printf("\n");

    if (safe_send(sockfd, &header, sizeof(header)) != sizeof(header))
    {
        std::cerr << "Error: failed to send cd request\n";
        return -1;
    }
    if (safe_send(sockfd, dir.c_str(), dir.size() + 1) != (ssize_t)(dir.size() + 1))
    {
        std::cerr << "Error: failed to send cd payload\n";
        return -1;
    }
    return 0;
}
int recv_cd_reply(int &sockfd, const std::string &dir)
{
    // 接收 CHANGE_DIR_REPLY
    myftp_header reply{};
    ssize_t n = safe_recv(sockfd, &reply, sizeof(reply));
    if (n <= 0)
    {
        std::cerr << "Error: failed to receive cd reply\n";
        return -1;
    }
    // 校验魔数和类型
    if (memcmp(reply.m_protocol, MAGIC_NUMBER, MAGIC_NUMBER_LENGTH) != 0 ||
        reply.m_type != CHANGE_DIR_REPLY)
    {
        std::cerr << "[Client] Invalid CHANGE_DIR_REPLY\n";
        return -1;
    }

    // 判断状态
    if (reply.m_status == 1)
    {
        std::cout << "Changed directory to: " << dir << "\n";
    }
    else
    {
        std::cerr << "Directory invalid or not found: " << dir << "\n";
    }

    return 0;
}

int handle_get(int &sockfd, const std::string &file_name)
{
    myftp_header header{
        .m_type = GET_REQUEST,
        .m_length = htonl(sizeof(myftp_header) + file_name.size() + 1)};

    // 发送 header + payload
    if (safe_send(sockfd, &header, sizeof(header)) != sizeof(header))
    {
        std::cerr << "[Client] Failed to send GET_REQUEST header\n";
        return -1;
    }
    if (safe_send(sockfd, file_name.c_str(), file_name.size() + 1) != (ssize_t)(file_name.size() + 1))
    {
        std::cerr << "[Client] Failed to send GET_REQUEST payload\n";
        return -1;
    }

    myftp_header reply{};
    if (safe_recv(sockfd, &reply, sizeof(reply)) != sizeof(reply))
    {
        std::cerr << "[Client] Failed to receive GET_REPLY\n";
        return -1;
    }
    if (memcmp(reply.m_protocol, MAGIC_NUMBER, MAGIC_NUMBER_LENGTH) != 0 ||
        reply.m_type != GET_REPLY)
    {
        std::cerr << "[Client] Invalid GET_REPLY\n";
        return -1;
    }
    if (reply.m_status == 0)
    {
        std::cerr << "[Client] Server reports file not found\n";
        return -1;
    }

    myftp_header file_header{};
    if (safe_recv(sockfd, &file_header, sizeof(file_header)) != sizeof(file_header))
    {
        std::cerr << "[Client] Failed to receive FILE_DATA header\n";
        return -1;
    }
    if (file_header.m_type != FILE_DATA)
    {
        std::cerr << "[Client] Unexpected file data type\n";
        return -1;
    }

    uint32_t file_size = ntohl(file_header.m_length) - sizeof(file_header);
    std::vector<char> file_buf(file_size);

    size_t received = 0;
    while (received < file_size)
    {
        ssize_t n = safe_recv(sockfd, file_buf.data() + received, file_size - received);
        if (n <= 0)
        {
            std::cerr << "[Client] Failed to receive file payload\n";
            return -1;
        }
        received += n;
    }

    FILE *fp = fopen(file_name.c_str(), "wb");
    if (!fp)
    {
        std::cerr << "[Client] Failed to open local file for writing\n";
        return -1;
    }
    fwrite(file_buf.data(), 1, file_size, fp);
    fclose(fp);

    std::cout << "[Client] File downloaded successfully: " << file_name << " (" << file_size << " bytes)\n";
    return 0;
}

int handle_put(int &sockfd, const std::string &file_name)
{
    std::ifstream ifs(file_name, std::ios::binary);
    if (!ifs)
    {
        std::cerr << "[Client] File does not exist: " << file_name << std::endl;
        return -1;
    }

    std::vector<char> file_data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    myftp_header header{
        .m_type = PUT_REQUEST,
        .m_status = STATUS_UNUSED,
        .m_length = htonl(sizeof(header) + file_name.size() + 1)};

    if (safe_send(sockfd, &header, sizeof(header)) != sizeof(header))
    {
        std::cerr << "[Client] Failed to send PUT_REQUEST\n";
        return -1;
    }
    if (safe_send(sockfd, file_name.c_str(), file_name.size() + 1) != (ssize_t)(file_name.size() + 1))
    {
        std::cerr << "[Client] Failed to send PUT_REQUEST payload\n";
        return -1;
    }

    myftp_header reply{};
    if (safe_recv(sockfd, &reply, sizeof(reply)) != sizeof(reply) || reply.m_type != PUT_REPLY)
    {
        std::cerr << "[Client] Failed to receive PUT_REPLY\n";
        return -1;
    }

    myftp_header file_hdr{
        .m_type = FILE_DATA,
        .m_status = STATUS_UNUSED,
        .m_length = htonl(sizeof(file_hdr) + file_data.size())

    };

    if (safe_send(sockfd, &file_hdr, sizeof(file_hdr)) != sizeof(file_hdr))
    {
        std::cerr << "[Client] Failed to send FILE_DATA header\n";
        return -1;
    }

    if (!file_data.empty() &&
        safe_send(sockfd, file_data.data(), file_data.size()) != (ssize_t)file_data.size())
    {
        std::cerr << "[Client] Failed to send FILE_DATA payload\n";
        return -1;
    }

    std::cout << "[Client] File uploaded successfully: " << file_name << " (" << file_data.size() << " bytes)\n";
    return 0;
}
int handle_sha256(int sockfd, const std::string &file_name)
{

    myftp_header header{
        .m_type = SHA_REQUEST,
        .m_status = STATUS_UNUSED,
        .m_length = htonl(sizeof(header) + file_name.size() + 1)};
    if (safe_send(sockfd, &header, sizeof(header)) <= 0)
        return -1;
    if (safe_send(sockfd, file_name.c_str(), file_name.size() + 1) <= 0)
        return -1;

    myftp_header reply{};
    if (safe_recv(sockfd, &reply, sizeof(reply)) <= 0)
    {
        std::cerr << "[Client] Error: failed to receive SHA_REPLY.\n";
        return -1;
    }

    if (reply.m_type != SHA_REPLY)
    {
        std::cerr << "[Client] Error: invalid reply type.\n";
        return -1;
    }

    if (reply.m_status == 0)
    {
        std::cerr << "[Client] Error: file not found on server.\n";
        return -1;
    }

    myftp_header data_hdr{};
    if (safe_recv(sockfd, &data_hdr, sizeof(data_hdr)) <= 0)
    {
        std::cerr << "[Client] Error: failed to receive FILE_DATA header.\n";
        return -1;
    }

    if (data_hdr.m_type != FILE_DATA)
    {
        std::cerr << "[Client] Error: invalid FILE_DATA type.\n";
        return -1;
    }

    size_t payload_len = data_hdr.m_length - sizeof(data_hdr);
    std::vector<char> buffer(payload_len);
    if (safe_recv(sockfd, buffer.data(), payload_len) <= 0)
    {
        std::cerr << "[Client] Error: failed to receive sha256 payload.\n";
        return -1;
    }

    std::cout << "[Client] SHA256:\n"
              << buffer.data() << std::endl;
    return 0;
}

int handle_quit(int &sockfd)
{
    if (send_quit_request(sockfd) < 0)
        return -1;
    if (recv_quit_reply(sockfd) < 0)
        return -1;

    close(sockfd);
    sockfd = -1; // 标记已关闭
    return 0;
}

int send_quit_request(int &sockfd)
{
    myftp_header header{
        .m_type = QUIT_REQUEST,
        .m_status = STATUS_UNUSED,
        .m_length = htonl(12)};

    // 发送请求
    ssize_t n = safe_send(sockfd, &header, sizeof(header));
    if (n != sizeof(header))
    {
        std::cerr << "[Client] Failed to send QUIT_REQUEST.\n";
        return -1;
    }

    return 0;
}

int recv_quit_reply(int &sockfd)
{
    myftp_header reply{};
    ssize_t n = safe_recv(sockfd, &reply, sizeof(reply));
    if (n != sizeof(reply))
    {
        std::cerr << "[Client] Failed to read QUIT_REPLY.\n";
        return -1;
    }

    // 校验魔数
    if (memcmp(reply.m_protocol, MAGIC_NUMBER, MAGIC_NUMBER_LENGTH) != 0)
    {
        std::cerr << "[Client] Invalid protocol magic number.\n";
        return -1;
    }

    // 校验类型
    if (reply.m_type != 0xAE)
    { // QUIT_REPLY
        std::cerr << "[Client] Unexpected reply type.\n";
        return -1;
    }

    std::cout << "[Client] Received QUIT_REPLY, closing connection.\n";
    return 0;
}

ssize_t safe_send(int &sockfd, const void *buf, size_t len)
{
    size_t total_sent = 0;
    const char *data = static_cast<const char *>(buf);

    while (total_sent < len)
    {
        ssize_t n = send(sockfd, data + total_sent, len - total_sent, MSG_NOSIGNAL);

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
            std::cerr << "[Client] Error: Connection closed by peer" << std::endl;
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
                perror("[Client] Error: read error");
                return -1;
            }
        }
    }

    return static_cast<ssize_t>(total); // 已成功读取 len 字节
}