#include "trans.h"
#include "global.h"
#include "kernel.h"
#include "tju_packet.h"

void* send_thread(void *arg) {
    tju_tcp_t* sock = (tju_tcp_t*)arg;
    while(1){ // 盲等 可以考虑用条件变量
        printf("进入send thread\n");
        
        while(sock->send_queue.size == 0){

        }
        
        pthread_mutex_lock(&sock->send_queue.mutex);
        tju_packet_t* pkt = pop(&sock->send_queue);
        pthread_mutex_unlock(&sock->send_queue.mutex);
        send_packet_wrapper(pkt);
        free_packet(pkt);
        printf("send thread\n");

    }
}