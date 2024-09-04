#ifndef __QUEUE_H__
#define __QUEUE_H__

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>

// 定义队列节点结构体
typedef struct QueueNode {
    void* data;            // 存储数据的指针
    struct QueueNode* next; // 指向下一个节点的指针
} QueueNode;

// 定义队列结构体
typedef struct {
    QueueNode* front; // 队列头部
    QueueNode* rear;  // 队列尾部
    size_t size;       // 队列中的元素数量
    pthread_mutex_t mutex; // 队列互斥锁
    pthread_cond_t notEmpty_cond; // 队列条件变量
} XQueue;

// 初始化队列
void initQueue(XQueue* queue);

// 创建新的队列节点
QueueNode* createNode(void* data);


// 入队操作
void push(XQueue* queue, void* data);

// 出队操作
void* pop(XQueue* queue);

// 销毁队列（不释放队列中的数据）
void destroyQueue(XQueue* queue);


#endif // __QUEUE_H__