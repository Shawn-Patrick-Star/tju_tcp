#include "XQueue.h"

// 初始化队列
void initQueue(XQueue* queue) {
    queue->front = NULL;
    queue->rear = NULL;
    queue->size = 0;
    pthread_mutex_init(&queue->mutex, NULL);
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
    if (queue->rear == NULL) { // 如果队列为空
        queue->front = newNode;
    } else {
        queue->rear->next = newNode;
    }
    queue->rear = newNode;
    queue->size++;
}

// 出队操作
void* pop(XQueue* queue) {
    if (queue->front == NULL) {
        fprintf(stderr, "Queue is empty\n");
        return NULL;
    }
    QueueNode* temp = queue->front;
    void* data = temp->data;
    queue->front = temp->next;
    if (queue->front == NULL) { // 如果队列变空，更新尾部指针
        queue->rear = NULL;
    }
    free(temp);
    queue->size--;
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
}
