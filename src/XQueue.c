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
    pthread_cond_signal(&queue->notEmpty_cond); // 通知有新元素入队
    pthread_mutex_unlock(&queue->mutex);
}

// 出队操作
void* pop(XQueue* queue) {
    pthread_mutex_lock(&queue->mutex);
    while (queue->size == 0) { // 队列为空时等待
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


// 销毁队列（不释放队列中的数据）
void destroyQueue(XQueue* queue) {
    while (pop(queue) != NULL);
}
