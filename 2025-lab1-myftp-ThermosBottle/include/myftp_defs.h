#ifndef MYFTP_DEFS_H
#define MYFTP_DEFS_H

#include <iostream>
#include <string.h>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <filesystem>
#include <cerrno>
#include <vector>
#include <fstream>
#include <thread>

ssize_t safe_send(int &sockfd, const void *buf, size_t len);
ssize_t safe_recv(int &sockfd, void *buf, size_t len);

#define MAGIC_NUMBER_LENGTH 6
typedef unsigned char byte; // 定义 byte 类型

typedef enum : byte
{
    OPEN_CONN_REQUEST = 0xA1,
    OPEN_CONN_REPLY = 0xA2,
    LIST_REQUEST = 0xA3,
    LIST_REPLY = 0xA4,
    CHANGE_DIR_REQUEST = 0xA5,
    CHANGE_DIR_REPLY = 0xA6,
    GET_REQUEST = 0xA7,
    GET_REPLY = 0xA8,
    PUT_REQUEST = 0xA9,
    PUT_REPLY = 0xAA,
    SHA_REQUEST = 0xAB,
    SHA_REPLY = 0xAC,
    QUIT_REQUEST = 0xAD,
    QUIT_REPLY = 0xAE,
    FILE_DATA = 0xFF
} type;
typedef enum : byte
{
    STATUS_ERR = 0x00,
    STATUS_OK = 0x01,
    STATUS_UNUSED = 0x02
} status;

struct myftp_header
{
    byte m_protocol[MAGIC_NUMBER_LENGTH]{0xC1, 0xA1, 0x10, 'f', 't', 'p'}; /* protocol magic number (6 bytes) */
    type m_type;                                                           /* type (1 byte) */
    status m_status{STATUS_UNUSED};                                        /* status (1 byte) */
    uint32_t m_length;                                                     /* length (4 bytes) in Big endian*/
} __attribute__((packed));

struct ClientContext
{
    int sockfd;
    std::filesystem::path current_dir;
};

#endif // MYFTP_DEFS_H