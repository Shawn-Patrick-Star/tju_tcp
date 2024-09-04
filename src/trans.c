#include "trans.h"
#include "global.h"
#include "kernel.h"
#include "tju_packet.h"

void* sender_thread(void *arg) {
    tju_tcp_t* sock = (tju_tcp_t*)arg;
    while(1){ // 盲等 可以考虑用条件变量
        log_debug("进入send thread\n");
        tju_packet_t* pkt = pop(&sock->send_queue);
        send_packet_wrapper(pkt);
        free_packet(pkt);
        log_debug("sender_thread发送了一个pkt");
    }
    pthread_exit(NULL);
}