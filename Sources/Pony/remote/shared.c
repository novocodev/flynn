
#include "platform.h"

#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#include "../ponyrt.h"

#include "../messageq.h"
#include "../scheduler.h"
#include "../actor.h"
#include "../cpu.h"
#include "../alloc.h"
#include "../pool.h"

#include "remote.h"

extern void master_shutdown();
extern void slave_shutdown();

char * BUILD_VERSION_UUID = __TIMESTAMP__;

// Communication between master and slave uses the following format:
//  bytes      meaning
//   [0] U8     type of command this is
//
//  COMMAND_VERSION_CHECK (master -> slave)
//   [1] U8     number of bytes for version uuid
//   [?]        version uuid as string
//
//  COMMAND_CREATE_ACTOR (master -> slave)
//   [1] U8     number of bytes for actor uuid
//   [?]        actor uuid as string
//   [1] U8     number of bytes for actor class name
//   [?]        actor class name
//
//  COMMAND_DESTROY_ACTOR (master -> slave)
//   [1] U8     number of bytes for actor uuid
//   [?]        actor uuid as string
//
//  COMMAND_SEND_MESSAGE (master -> slave)
//   [1] U8     number of bytes for actor uuid
//   [?]        actor uuid as string
//   [0-4]      number of bytes for message data
//   [?]        message data
//
//  COMMAND_SEND_REPLY (master <- slave)
//   [1] U8     number of bytes for actor uuid
//   [?]        actor uuid as string
//   [0-4]      number of bytes for message data
//   [?]        message data
//

// MARK: - COMMANDS

char * read_intcount_buffer(int socketfd, uint32_t * count) {
    if (recv(socketfd, count, sizeof(uint32_t), 0) <= 0) {
        return NULL;
    }
    *count = ntohl(*count);
    
    char * bytes = malloc(*count);
    if (recv(socketfd, bytes, *count, 0) <= 0) {
        return NULL;
    }
    return bytes;
}

bool read_bytecount_buffer(int socketfd, char * dst, size_t max_length) {
    uint8_t count = 0;
    recv(socketfd, &count, 1, 0);
    
    if (count >= max_length) {
        close_socket(socketfd);
        return false;
    }
    recv(socketfd, dst, count, 0);
    return true;
}

uint8_t read_command(int socketfd) {
    uint8_t command = COMMAND_NULL;
    recv(socketfd, &command, 1, 0);
    return command;
}

void send_buffer(int socketfd, char * bytes, size_t length) {
    int bytes_sent;
    while (length > 0) {
        bytes_sent = send(socketfd, bytes, length, 0);
        if (bytes_sent < 0) {
            return;
        }
        bytes += bytes_sent;
        length -= bytes_sent;
    }
}

void send_version_check(int socketfd) {
    char buffer[512];
    int idx = 0;
    
    buffer[idx++] = COMMAND_VERSION_CHECK;
    
    uint8_t uuid_count = strlen(BUILD_VERSION_UUID);
    buffer[idx++] = uuid_count;
    memcpy(buffer + idx, BUILD_VERSION_UUID, uuid_count);
    idx += uuid_count;
        
    send_buffer(socketfd, buffer, idx);
}

void send_create_actor(int socketfd, const char * actorUUID, const char * actorType) {
    char buffer[512];
    int idx = 0;
    
    buffer[idx++] = COMMAND_CREATE_ACTOR;
    
    uint8_t uuid_count = strlen(actorUUID);
    buffer[idx++] = uuid_count;
    memcpy(buffer + idx, actorUUID, uuid_count);
    idx += uuid_count;
    
    uint8_t type_count = strlen(actorType);
    buffer[idx++] = type_count;
    memcpy(buffer + idx, actorType, type_count);
    idx += type_count;
    
    send_buffer(socketfd, buffer, idx);
    
#if REMOTE_DEBUG
    fprintf(stderr, "[%d] master sending create actor to socket\n", socketfd);
#endif
}

void send_destroy_actor(int socketfd, const char * actorUUID) {
    char buffer[512];
    int idx = 0;
    
    buffer[idx++] = COMMAND_DESTROY_ACTOR;
    
    uint8_t uuid_count = strlen(actorUUID);
    buffer[idx++] = uuid_count;
    memcpy(buffer + idx, actorUUID, uuid_count);
    idx += uuid_count;
        
    send_buffer(socketfd, buffer, idx);
    
#if REMOTE_DEBUG
    fprintf(stderr, "[%d] master sending destroy actor to socket\n", socketfd);
#endif
}

void send_message(int socketfd, const char * actorUUID, const char * behaviorType, const void * bytes, uint32_t count) {
    char buffer[512];
    int idx = 0;
    
    buffer[idx++] = COMMAND_SEND_MESSAGE;
    
    uint8_t uuid_count = strlen(actorUUID);
    buffer[idx++] = uuid_count;
    memcpy(buffer + idx, actorUUID, uuid_count);
    idx += uuid_count;
    
    uint8_t behavior_count = strlen(behaviorType);
    buffer[idx++] = behavior_count;
    memcpy(buffer + idx, behaviorType, behavior_count);
    idx += behavior_count;
        
    send_buffer(socketfd, buffer, idx);
    
    uint32_t net_count = htonl(count);
    send(socketfd, &net_count, sizeof(net_count), 0);
    
    send_buffer(socketfd, (char *)bytes, count);
    
#if REMOTE_DEBUG
    fprintf(stderr, "[%d] master sending message to socket\n", socketfd);
#endif
}

void send_reply(int socketfd, const char * actorUUID, const void * bytes, uint32_t count) {
    char buffer[512];
    int idx = 0;
    
    buffer[idx++] = COMMAND_SEND_REPLY;
    
    uint8_t uuid_count = strlen(actorUUID);
    buffer[idx++] = uuid_count;
    memcpy(buffer + idx, actorUUID, uuid_count);
    idx += uuid_count;
            
    send_buffer(socketfd, buffer, idx);
    
    uint32_t net_count = htonl(count);
    send(socketfd, &net_count, sizeof(net_count), 0);
    
    send_buffer(socketfd, (char *)bytes, count);
    
#if REMOTE_DEBUG
    fprintf(stderr, "[%d] slave sending reply to socket\n", socketfd);
#endif
}

// MARK: - SHUTDOWN

void pony_remote_shutdown() {
    master_shutdown();
    slave_shutdown();
}

void close_socket(int fd) {
    shutdown(fd, SHUT_RDWR);
    close(fd);
}