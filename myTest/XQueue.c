#include <stdio.h>
#include <stdlib.h>

#include "XQueue.h"

// 初始化队列
void initQueue(XQueue* queue) {
    queue->front = NULL;
    queue->rear = NULL;
    queue->size = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->notEmpty_cond, NULL);
}

// 创建新的队列节点
QueueNode* createNode(void* data) {
    QueueNode* newNode = (QueueNode*)malloc(sizeof(QueueNode));
    if (newNode == NULL) {
        perror("Failed to allocate memory for queue node");
        exit(EXIT_FAILURE);
    }
    newNode->data = data;
    newNode->next = NULL;
    return newNode;
}

// 入队操作
void push(XQueue* queue, void* data) {
    QueueNode* newNode = createNode(data);
    pthread_mutex_lock(&queue->mutex);
    if (queue->rear == NULL) { // 如果队列为空
        queue->front = newNode;
    } else {
        queue->rear->next = newNode;
    }
    queue->rear = newNode;
    queue->size++;
    pthread_cond_signal(&queue->notEmpty_cond);
    pthread_mutex_unlock(&queue->mutex);
}

// 出队操作
void* pop(XQueue* queue) {
    pthread_mutex_lock(&queue->mutex);
    while(queue->size == 0) {
        pthread_cond_wait(&queue->notEmpty_cond, &queue->mutex);
    }
    QueueNode* temp = queue->front;
    void* data = temp->data;
    queue->front = temp->next;
    if (queue->front == NULL) { // 如果队列变空，更新尾部指针
        queue->rear = NULL;
    }
    free(temp);
    queue->size--;
    pthread_mutex_unlock(&queue->mutex);
    return data;
}

// 获取队列头部数据（不删除）
void* front(XQueue* queue) {
    if (queue->front == NULL) {
        return NULL;
    }
    return queue->front->data;
}

// 销毁队列（不释放队列中的数据）
void destroyQueue(XQueue* queue) {
    while (pop(queue) != NULL);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->notEmpty_cond);
}


// // 测试代码
// int main() {
//     XQueue queue;
//     int data1 = 10, data2 = 20;
    
//     // 初始化队列
//     initQueue(&queue);
    
//     // 入队
//     push(&queue, &data1);
//     push(&queue, &data2);
    
//     // 出队
//     int* popdData1 = pop(&queue);
//     printf("popd: %d\n", *popdData1);
    
//     // 再次出队
//     int* popdData2 = pop(&queue);
//     printf("popd: %d\n", *popdData2);
    
//     // 尝试再次出队（队列为空）
//     if (front(&queue) == NULL) {
//         printf("Queue is now empty.\n");
//     }
    
//     // 销毁队列
//     destroyQueue(&queue);

//     return 0;
// }