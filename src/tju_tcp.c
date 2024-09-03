#include <pthread.h>

#include "tju_tcp.h"
#include "XQueue.h"
#include "trans.h"
#include "log.h"
// TCP 初始序列号 正常情况下应该是随机的
#define INIT_CLIENT_SEQ 0
#define INIT_SERVER_SEQ 0


/*
创建 TCP socket 
初始化对应的结构体
设置初始状态为 CLOSED
*/
tju_tcp_t* tju_socket(){
    tju_tcp_t* sock = (tju_tcp_t*)malloc(sizeof(tju_tcp_t));
    sock->state = CLOSED;
    
    initQueue(&(sock->send_queue));

    pthread_mutex_init(&(sock->recv_lock), NULL);
    sock->received_buf = NULL;
    sock->received_len = 0;
    
    if(pthread_cond_init(&sock->wait_cond, NULL) != 0){
        perror("ERROR condition variable not set\n");
        exit(-1);
    }

    sock->window.wnd_send = (sender_window_t*)malloc(sizeof(sender_window_t));
    sock->window.wnd_recv = (receiver_window_t*)malloc(sizeof(receiver_window_t));
    
    return sock;
}

/*
绑定监听的地址 包括ip和端口
*/
int tju_bind(tju_tcp_t* sock, tju_sock_addr bind_addr){
    sock->bind_addr = bind_addr;
    return 0;
}

/*
被动打开 监听bind的地址和端口
设置socket的状态为LISTEN
注册该socket到内核的监听socket哈希表
*/
int tju_listen(tju_tcp_t* sock){
    initQueue(&(sock->full_conn_queue));
    initQueue(&(sock->half_conn_queue));

    sock->state = LISTEN;
    int hashval = cal_hash(sock->bind_addr.ip, sock->bind_addr.port, 0, 0);
    listen_socks[hashval] = sock;
    return 0;
}


/**
 * @brief 接受连接
 * 
 * @param listen_sock 监听socket
 * @return tju_tcp_t*
 * 返回与客户端通信用的socket
 * 
 * 这里返回的socket一定是已经完成3次握手建立了连接的socket
 * 
 * 因为只要该函数返回, 用户就可以马上使用该socket进行send和recv
 */
tju_tcp_t* tju_accept(tju_tcp_t* listen_sock){
    // 如果new_conn的创建过程放到了tju_handle_packet中 那么accept怎么拿到这个new_conn呢
    // 在linux中 每个listen socket都维护一个已经完成连接的socket队列
    // 每次调用accept 实际上就是取出这个队列中的一个元素
    // 队列为空,则阻塞 
    while(listen_sock->full_conn_queue.size == 0); // 阻塞

    tju_tcp_t* new_conn = pop(&listen_sock->full_conn_queue);

    // 将新的conn放到内核建立连接的socket哈希表中
    int hashval = cal_hash(new_conn->established_local_addr.ip, new_conn->established_local_addr.port, 
                           new_conn->established_remote_addr.ip, new_conn->established_remote_addr.port);
    established_socks[hashval] = new_conn;

    return new_conn;
}


/*
    连接到服务端
    该函数以一个socket为参数
    调用函数前, 该socket还未建立连接
    函数正常返回后, 该socket一定是已经完成了3次握手, 建立了连接
    因为只要该函数返回, 用户就可以马上使用该socket进行send和recv
*/
int tju_connect(tju_tcp_t* sock, tju_sock_addr target_addr){

    sock->established_remote_addr = target_addr;

    tju_sock_addr local_addr;
    local_addr.ip = inet_network("172.17.0.2");
    local_addr.port = 5678; // 连接方进行connect连接的时候 内核中是随机分配一个可用的端口
    sock->established_local_addr = local_addr;

    sock->window.wnd_send->base = INIT_CLIENT_SEQ;
    sock->window.wnd_send->nextseq = INIT_CLIENT_SEQ;

    int hashval = cal_hash(local_addr.ip, local_addr.port, 0, 0);
    listen_socks[hashval] = sock; 

    // client发送SYN报文
    tju_packet_t* pkt = create_packet(
        sock->established_local_addr.port, sock->established_remote_addr.port,
        sock->window.wnd_send->nextseq, 0, 
        DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, 
        SYN_FLAG_MASK, 1, 0,
        NULL, 0
    );
    send_packet_wrapper(pkt);
    free_packet(pkt);
    sock->state = SYN_SENT;
    sock->window.wnd_send->nextseq += 1; // SYN 报文虽然没有有效载荷 但它仍然被视为一个有效的 TCP 段 序列号需要递增以反映下一个 TCP 段的开始
    printf("客户端发送了第一次挥手SYN报文\n");
    // 这里也不能直接建立连接 需要经过三次握手
    // 实际在linux中 connect调用后 会进入一个while循环
    // 循环跳出的条件是socket的状态变为ESTABLISHED 表面看上去就是 正在连接中 阻塞
    // 而状态的改变在别的地方进行 在我们这就是tju_handle_packet
    while(sock->state != ESTABLISHED); // 阻塞
    // 将建立了连接的socket放入内核 已建立连接哈希表中
    listen_socks[hashval] = NULL;
    hashval = cal_hash(local_addr.ip, local_addr.port, target_addr.ip, target_addr.port);
    established_socks[hashval] = sock;
    printf("客户端完成了三次握手, 连接建立\n");

    return 0;
}

int tju_send(tju_tcp_t* sock, const void *buffer, int len){
    if(sock->state != ESTABLISHED && sock->state != CLOSE_WAIT){
        perror("ERROR: socket is not in ESTABLISHED or CLOSE_WAIT state\n");
        return -1;
    }

    // 这里当然不能直接简单地调用sendToLayer3
    int count = len / MAX_DLEN;
    for(int i = 0; i <= count; i++){
        int send_len = (i == count) ? len % MAX_DLEN : MAX_DLEN;// 很巧妙的写法         
        char* data = malloc(len);
        memcpy(data, buffer + i*MAX_DLEN, len);

        // char* msg = create_packet_buf(
        //     sock->established_local_addr.port, sock->established_remote_addr.port, 
        //     sock->window.wnd_send->nextseq, 0, 
        //     DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN+send_len,
        //     NO_FLAG, 1, 0, 
        //     data, send_len);
        tju_packet_t* pkt = create_packet(
            sock->established_local_addr.port, sock->established_remote_addr.port,
            sock->window.wnd_send->nextseq, 0,
            DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN+send_len,
            NO_FLAG, 1, 0,
            data, send_len);
        send_packet_wrapper(pkt);
        free_packet(pkt);
        free(data);
        sock->window.wnd_send->nextseq += send_len;
    }
    log_debug("调用send发送了%d个报文\n", count+1);
    return 0;
}
int tju_recv(tju_tcp_t* sock, void *buffer, int len){
    
    while(sock->received_len<=0){
        // 阻塞
    }
    log_debug("通过阻塞获得了数据\n");
    while(pthread_mutex_lock(&(sock->recv_lock)) != 0); // 加锁

    int read_len = 0;
    if (sock->received_len >= len){ // 从中读取len长度的数据
        read_len = len;
    }else{
        read_len = sock->received_len; // 读取sock->received_len长度的数据(全读出来)
    }

    memcpy(buffer, sock->received_buf, read_len);

    if(read_len < sock->received_len) { // 还剩下一些
        char* new_buf = malloc(sock->received_len - read_len);
        memcpy(new_buf, sock->received_buf + read_len, sock->received_len - read_len);
        free(sock->received_buf);
        sock->received_len -= read_len;
        sock->received_buf = new_buf;
    }else{
        free(sock->received_buf);
        sock->received_buf = NULL;
        sock->received_len = 0;
    }
    pthread_mutex_unlock(&(sock->recv_lock)); // 解锁

    return 0;
}

// 接收到pkt统一流程: expect_seq=seq+1 -> seq=nextseq | ack=expect_seq -> nextseq=seq+报文长度
// 应该有比较expect_seq和seq的大小的操作

int tju_handle_packet(tju_tcp_t* sock, char* pkt){
    
    uint32_t data_len = get_plen(pkt) - DEFAULT_HEADER_LEN;
    uint16_t src_port = get_src(pkt);
    uint16_t dst_port = get_dst(pkt);
    uint16_t seq = get_seq(pkt);
    uint16_t ack = get_ack(pkt);
    uint16_t flags = get_flags(pkt);
    log_debug("收到报文, 状态:%u, seq:%d, ack:%d, flags:%d, data_len:%d\n", sock->state, seq, ack, flags, data_len);
    switch (sock->state)
    {
/*---------------------------------三次握手--------------------------------------------*/
    case LISTEN: // server收到SYN报文 这里的socket是监听socket
        if(flags == SYN_FLAG_MASK){
            log_debug("服务端LISTEN状态下收到SYN报文, 发送SYN+ACK 变为SYN_RECV\n");
            // 初始化监听socket的窗口序号
            sock->window.wnd_send->base = INIT_SERVER_SEQ;
            sock->window.wnd_send->nextseq = INIT_SERVER_SEQ;

            
            sock->window.wnd_recv->expect_seq = seq + 1; 

            // server发送 应答ACK + 请求SYN 
            tju_packet_t* pkt = create_packet(
                dst_port, src_port, // 本地测试改成sock->established_local_addr.port才能过 真是离谱
                sock->window.wnd_send->nextseq, sock->window.wnd_recv->expect_seq, 
                DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, 
                SYN_FLAG_MASK | ACK_FLAG_MASK, 1, 0,
                NULL, 0
            );
            // printPacket(pkt);
            send_packet_wrapper(pkt);
            free_packet(pkt);

            sock->window.wnd_send->nextseq += 1; 
            sock->state = SYN_RECV;
        }   
        else{
            sock->state = CLOSED;
            printf("LISTEN状态下收到非SYN报文, 丢弃\n");
        }
        break;

    case SYN_SENT: // client收到SYN+ACK报文 这里的socket是client_conn_socket
        if(flags == (SYN_FLAG_MASK | ACK_FLAG_MASK)){
            log_debug("客户端SYN_SENT状态下收到SYN+ACK报文, 发送ACK 变为ESTABLISH\n");

            
            sock->window.wnd_recv->expect_seq = seq + 1;

            // client发送 应答ACK
            tju_packet_t* pkt = create_packet(
                sock->established_local_addr.port, sock->established_remote_addr.port,
                sock->window.wnd_send->nextseq, sock->window.wnd_recv->expect_seq,
                DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, 
                ACK_FLAG_MASK, 1, 0,
                NULL, 0
            );
            send_packet_wrapper(pkt);
            // free_packet(pkt);

            sock->window.wnd_send->nextseq += 1;
            sock->state = ESTABLISHED;
        }
        else{
            sock->state = CLOSED;
            printf("SYN_SENT状态下收到非SYN+ACK报文, 丢弃\n");
        }
        break;

    case SYN_RECV: // server收到ACK报文 这里的socket是监听socket
        if(flags == ACK_FLAG_MASK){
            log_debug("服务端SYN_RECV状态下收到ACK报文, 新建连接, 重新回到LISTEN状态 新建立的sock\n");
            
            sock->window.wnd_send->nextseq = INIT_SERVER_SEQ;
            sock->window.wnd_recv->expect_seq = INIT_SERVER_SEQ;
            
            tju_tcp_t* new_conn = tju_socket(); // 创建新的socket用于通信
            new_conn->state = ESTABLISHED; 
            new_conn->established_local_addr = sock->bind_addr;
            new_conn->established_remote_addr.ip = inet_network("172.17.0.2");
            new_conn->established_remote_addr.port = src_port;
            new_conn->window.wnd_send->nextseq = sock->window.wnd_send->nextseq; 
            new_conn->window.wnd_recv->expect_seq = seq + 1;

            push(&sock->full_conn_queue, new_conn);
            sock->state = LISTEN; // 监听socket重新回到LISTEN状态
        }
        else{
            sock->state = CLOSED;
            printf("SYN_RECV状态下收到非ACK报文, 丢弃\n");
        }
        break;
/*---------------------------------四次挥手--------------------------------------------*/
    case FIN_WAIT_1:
        if(flags == ACK_FLAG_MASK){
            sock->window.wnd_recv->expect_seq = seq + 1;
            sock->state = FIN_WAIT_2;
            log_debug("FIN_WAIT_1状态下收到ACK报文, 状态变为FIN_WAIT_2");
        }
        else if(flags == (FIN_FLAG_MASK | ACK_FLAG_MASK) | flags == FIN_FLAG_MASK){
            
            sock->window.wnd_recv->expect_seq = seq + 1;

            // 发送 应答ACK
            tju_packet_t* pkt = create_packet(
                sock->established_local_addr.port, sock->established_remote_addr.port,
                sock->window.wnd_send->nextseq, sock->window.wnd_recv->expect_seq,
                DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN,
                ACK_FLAG_MASK, 1, 0,
                NULL, 0);
            send_packet_wrapper(pkt);
            free_packet(pkt);

            sock->window.wnd_send->nextseq += 1;
            sock->state = CLOSING;
            log_debug("FIN_WAIT_1状态下收到FIN+ACK 发送ACK 状态变为CLOSING");
        }
        break;

    case FIN_WAIT_2:
        if(flags == (FIN_FLAG_MASK | ACK_FLAG_MASK)){
            log_debug("FIN_WAIT_2状态下收到FIN|ACK 发送ACK 状态变为TIME_WAIT");
            
            sock->window.wnd_recv->expect_seq = seq + 1;

            // 发送 应答ACK
            tju_packet_t* pkt = create_packet(
                sock->established_local_addr.port, sock->established_remote_addr.port,
                sock->window.wnd_send->nextseq, sock->window.wnd_recv->expect_seq,
                DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN,
                ACK_FLAG_MASK, 1, 0,
                NULL, 0);
            send_packet_wrapper(pkt);
            free_packet(pkt);

            sock->window.wnd_send->nextseq += 1;
            sock->state = TIME_WAIT;
        }
        break;

    case CLOSING:
        if(flags == ACK_FLAG_MASK){
            sock->window.wnd_recv->expect_seq = seq + 1;
            sock->state = TIME_WAIT;
            log_debug("CLOSING状态下收到ACK报文, 状态变为TIME_WAIT");
        }
        break;

    case LAST_ACK:
        if(flags == ACK_FLAG_MASK){
            sock->state = CLOSED;
            log_debug("LAST_ACK状态下收到ACK报文, 状态变为CLOSED");
        }
        break;

    case ESTABLISHED: // 收到数据报文
        if(flags == (FIN_FLAG_MASK | ACK_FLAG_MASK) || flags == FIN_FLAG_MASK){ // 收到FIN报文 表示对方要关闭连接 ///////////我累个烧刚啊！！！！！！！！！！！！！！！！！！！！！！加括号
            
            
            sock->window.wnd_recv->expect_seq = seq + 1;
            // 发送 应答ACK 
            tju_packet_t* pkt = create_packet(
                sock->established_local_addr.port, sock->established_remote_addr.port,
                sock->window.wnd_send->nextseq, sock->window.wnd_recv->expect_seq,
                DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, 
                ACK_FLAG_MASK, 1, 0,
                NULL, 0);
            send_packet_wrapper(pkt);
            free_packet(pkt);

            sock->window.wnd_send->nextseq += 1;
            sock->state = CLOSE_WAIT;
            log_debug("ESTABLISHED状态下收到FIN|ACK, 并发送ACK 状态变为CLOSE_WAIT");


            log_debug("CLOSE_WAIT状态下发送FIN|ACK 进入LAST_ACK");

            pkt = create_packet(
                sock->established_local_addr.port, sock->established_remote_addr.port,
                sock->window.wnd_send->nextseq, sock->window.wnd_recv->expect_seq,
                DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, 
                FIN_FLAG_MASK | ACK_FLAG_MASK, 1, 0,
                NULL, 0);
            send_packet_wrapper(pkt);
            free_packet(pkt);

            sock->window.wnd_send->nextseq += 1;
            sock->state = LAST_ACK;

        }
        else if(flags == NO_FLAG){

            // // 发送 应答ACK
            // tju_packet_t* pkt = create_packet(
            //     sock->established_local_addr.port, sock->established_remote_addr.port,
            //     seq+1, ack, 
            //     DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, 
            //     ACK_FLAG_MASK, 1, 0,
            //     NULL, 0
            // );
            // send_packet_wrapper(pkt);
            // free_packet(pkt);

                    // 把这段注释掉就过不了established的测试 hhhhhhhhh
            // 把收到的数据放到接受缓冲区
            while(pthread_mutex_lock(&(sock->recv_lock)) != 0); // 加锁

            if(sock->received_buf == NULL){ // 第一次收到数据
                sock->received_buf = malloc(data_len);
            }else { 
                sock->received_buf = realloc(sock->received_buf, sock->received_len + data_len);
            }
            memcpy(sock->received_buf + sock->received_len, pkt + DEFAULT_HEADER_LEN, data_len);
            sock->received_len += data_len;

            pthread_mutex_unlock(&(sock->recv_lock)); // 解锁
        }
        else{
            log_error("ESTABLISHED状态下收到非数据报文, 丢弃 此时报文flag%u", flags);
        }
        break;
    
    default:
        break;
    }



    return 0;
}


/*
    先后断开连接:               client                           server
    C -> S  FIN|ACK     ESTABILISHED -> FIN_WAIT_1        
    S -> C  ACK         FIN_WAIT_1 -> FIN_WAIT_2          ESTABLISHED -> CLOSE_WAIT
    S -> C  FIN|ACK     FIN_WAIT_2 -> TIME_WAIT           CLOSE_WAIT -> LAST_ACK
    C -> S  ACK         TIME_WAIT -> CLOSED               LAST_ACK -> CLOSED

    同时断开连接:
    C -> S  FIN|ACK  
    S -> C  FIN|ACK
    S -> C  ACK
    C -> S  ACK

*/
int tju_close (tju_tcp_t* sock){
    tju_packet_t* fin_pkt = create_packet(
        sock->established_local_addr.port, sock->established_remote_addr.port,
        sock->window.wnd_send->nextseq, sock->window.wnd_recv->expect_seq,
        DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, 
        FIN_FLAG_MASK | ACK_FLAG_MASK, 1, 0,
        NULL, 0
    );
    send_packet_wrapper(fin_pkt);
    free_packet(fin_pkt);

    sock->window.wnd_send->nextseq += 1;

    switch (sock->state)
    {
    case ESTABLISHED:
        sock->state = FIN_WAIT_1;
        log_debug("调用close 发送FIN|ACK 进入FIN_WAIT_1");
        while(sock->state != TIME_WAIT);
        // 等待2MSL
        sock->state = CLOSED;
        log_debug("TIME_WAIT状态结束, 进入CLOSED 离开close函数");
        break;
    case CLOSE_WAIT:
        // log_debug("调用close 发送FIN|ACK 进入LAST_ACK");
        // sock->state = LAST_ACK;

        // tju_packet_t* pkt = create_packet(
        //     sock->established_local_addr.port, sock->established_remote_addr.port,
        //     sock->window.wnd_send->nextseq, sock->window.wnd_recv->expect_seq,
        //     DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, 
        //     FIN_FLAG_MASK | ACK_FLAG_MASK, 1, 0,
        //     NULL, 0);
        // send_packet_wrapper(pkt);
        // free_packet(pkt);

        // sock->window.wnd_send->nextseq += 1;
        
        while(sock->state != CLOSED);
        log_debug("离开close函数");
        break;
    default:
        break;
    }

    return 0;
}