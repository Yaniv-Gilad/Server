#include "threadpool.h"
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRUE 1
#define FALSE 0

threadpool *create_threadpool(int num_threads_in_pool)
{
    int check = 0;
    if (num_threads_in_pool > MAXT_IN_POOL || num_threads_in_pool <= 0)
    {
        printf("Usage: threadpool <pool-size> <max-number-of-jobs>\n");
        return NULL;
    }

    threadpool *t = (threadpool *)malloc(sizeof(threadpool));
    if (t == NULL)
    {
        perror("error: malloc\n");
        return NULL;
    }

    t->num_threads = num_threads_in_pool;
    t->qsize = 0;
    t->dont_accept = FALSE;
    t->shutdown = FALSE;
    t->qhead = NULL;
    t->qtail = NULL;

    // need to continue
    check = pthread_mutex_init(&(t->qlock), NULL);
    if (check != 0)
    {
        free(t);
        perror("error: pthread_mutex_init\n");
        return NULL;
    }

    check = pthread_cond_init(&(t->q_empty), NULL);
    if (check != 0)
    {
        pthread_mutex_destroy(&t->qlock);
        free(t);
        perror("error: pthread_cond_init\n");
        return NULL;
    }

    check = pthread_cond_init(&(t->q_not_empty), NULL);
    if (check != 0)
    {
        pthread_cond_destroy(&t->q_empty);
        pthread_mutex_destroy(&t->qlock);
        free(t);
        perror("error: pthread_cond_init\n");
        return NULL;
    }

    // init the threads
    t->threads = (pthread_t *)malloc(sizeof(pthread_t) * t->num_threads);
    if (t->threads == NULL)
    {
        pthread_cond_destroy(&t->q_not_empty);
        pthread_cond_destroy(&t->q_empty);
        pthread_mutex_destroy(&t->qlock);
        free(t);
        perror("error: malloc\n");
        return NULL;
    }
    for (int i = 0; i < t->num_threads; i++)
        t->threads[i] = 0;
    for (int i = 0; i < t->num_threads; i++)
    {
        check = pthread_create(t->threads + i, NULL, do_work, t);
        if (check != 0)
        {
            pthread_cond_destroy(&t->q_not_empty);
            pthread_cond_destroy(&t->q_empty);
            pthread_mutex_destroy(&t->qlock);
            free(t);
            perror("error: pthread_create\n");
            return NULL;
        }
    }

    return t;
}

void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg)
{
    if (from_me == NULL)
        return;

    pthread_mutex_lock(&from_me->qlock);
    if (from_me->dont_accept == TRUE)
    {
        pthread_mutex_unlock(&from_me->qlock);
        return;
    }

    work_t *temp = (work_t *)malloc(sizeof(work_t));
    if (temp == NULL)
    {
        perror("error: malloc\n");
        return;
    }
    temp->routine = dispatch_to_here;
    temp->arg = arg;
    temp->next = NULL;

    if (from_me->qhead == NULL && from_me->qtail == NULL) // if q is empty
    {
        from_me->qhead = temp;
        from_me->qtail = temp;
    }
    else if (from_me->qhead != NULL && from_me->qhead == from_me->qtail) // head and tail are the same node
    {
        from_me->qtail = temp;
        from_me->qhead->next = from_me->qtail;
    }
    else
    {
        from_me->qtail->next = temp;
        from_me->qtail = temp;
    }

    from_me->qsize++;
    pthread_cond_signal(&from_me->q_not_empty);
    pthread_mutex_unlock(&from_me->qlock);
}

void *do_work(void *p)
{
    if (p == NULL)
        return NULL;

    int check = 0;
    threadpool *t = (threadpool *)p;
    work_t *curr;

    while (TRUE)
    {
        check = pthread_mutex_lock(&t->qlock);
        if (check != 0)
        {
            perror("error: pthread_mutex_lock\n");
            return NULL;
        }

        if (t->shutdown == TRUE)
        {
            pthread_mutex_unlock(&t->qlock);
            return NULL;
        }

        if (t->qsize == 0) // if q is empty - wait until there is a job
            pthread_cond_wait(&t->q_not_empty, &t->qlock);

        // if "destroy_threadpool" signal to the threads to exit
        if (t->shutdown == TRUE)
        {
            pthread_mutex_unlock(&t->qlock);
            return NULL;
        }

        // takes the first element from the queue and run it
        curr = t->qhead;
        if (curr != NULL)
        {
            t->qsize--;
            if (t->qhead->next == NULL) // last element in q
            {
                t->qhead = NULL;
                t->qtail = NULL;

                if (t->dont_accept == TRUE) // if destroy began
                    pthread_cond_signal(&t->q_empty);
            }
            else
                t->qhead = t->qhead->next;

            check = pthread_mutex_unlock(&t->qlock);
            if (check != 0)
            {
                perror("error: pthread_mutex_unlock\n");
                return NULL;
            }

            curr->routine(curr->arg);
            free(curr);
        }
        else
        {
            check = pthread_mutex_unlock(&t->qlock);
            if (check != 0)
            {
                perror("error: pthread_mutex_unlock\n");
                return NULL;
            }
        }
    }
}

void destroy_threadpool(threadpool *destroyme)
{
    if (destroyme == NULL)
        return;

    pthread_mutex_lock(&destroyme->qlock);
    destroyme->dont_accept = TRUE;
    if(destroyme->qsize > 0)
        pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock); // wait for q to be empty

    destroyme->shutdown = TRUE;
    pthread_cond_broadcast(&destroyme->q_not_empty); // signal to all threads to finish
    pthread_mutex_unlock(&destroyme->qlock);

    for (int i = 0; i < destroyme->num_threads; i++) // wait for all threads to finish
        pthread_join(destroyme->threads[i], NULL);
    
    free(destroyme->threads);
    pthread_cond_destroy(&destroyme->q_not_empty);
    pthread_cond_destroy(&destroyme->q_empty);
    pthread_mutex_destroy(&destroyme->qlock);
    free(destroyme);
}
