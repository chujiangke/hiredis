#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

#include "hiredis.h"

/* The following line is our testing "framework" :) */
#define test_cond(_c) if(_c) printf("PASSED\n"); else {printf("FAILED\n"); fails++;}

long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

void connect(int *fd) {
    redisReply *reply = redisConnect(fd, "127.0.0.1", 6379);
    if (reply != NULL) {
        printf("Connection error: %s", reply->reply);
        exit(1);
    }
}

int main(void) {
    int fd;
    int i, fails = 0;
    long long t1, t2;
    redisReply *reply;
    connect(&fd);

    /* test 0 */
    printf("#0 Returns I/O error when the connection is lost: ");
    reply = redisCommand(fd,"QUIT");
    test_cond(reply->type == REDIS_REPLY_ERROR &&
        strcasecmp(reply->reply,"i/o error") == 0);
    freeReplyObject(reply);
    connect(&fd); /* reconnect */

    /* test 1 */
    printf("#1 Is able to deliver commands: ");
    reply = redisCommand(fd,"PING");
    test_cond(reply->type == REDIS_REPLY_STRING &&
        strcasecmp(reply->reply,"pong") == 0)

    /* Switch to DB 9 for testing, now that we know we can chat. */
    reply = redisCommand(fd,"SELECT 9");
    freeReplyObject(reply);

    /* Make sure the DB is emtpy */
    reply = redisCommand(fd,"DBSIZE");
    if (reply->type != REDIS_REPLY_INTEGER ||
        reply->integer != 0) {
        printf("Sorry DB 9 is not empty, test can not continue\n");
        exit(1);
    } else {
        printf("DB 9 is empty... test can continue\n");
    }
    freeReplyObject(reply);

    /* test 2 */
    printf("#2 Is a able to send commands verbatim: ");
    reply = redisCommand(fd,"SET foo bar");
    test_cond (reply->type == REDIS_REPLY_STRING &&
        strcasecmp(reply->reply,"ok") == 0)
    freeReplyObject(reply);

    /* test 3 */
    printf("#3 %%s String interpolation works: ");
    reply = redisCommand(fd,"SET %s %s","foo","hello world");
    freeReplyObject(reply);
    reply = redisCommand(fd,"GET foo");
    test_cond(reply->type == REDIS_REPLY_STRING &&
        strcmp(reply->reply,"hello world") == 0);
    freeReplyObject(reply);

    /* test 4 & 5 */
    printf("#4 %%b String interpolation works: ");
    reply = redisCommand(fd,"SET %b %b","foo",3,"hello\x00world",11);
    freeReplyObject(reply);
    reply = redisCommand(fd,"GET foo");
    test_cond(reply->type == REDIS_REPLY_STRING &&
        memcmp(reply->reply,"hello\x00world",11) == 0)

    printf("#5 binary reply length is correct: ");
    test_cond(sdslen(reply->reply) == 11)
    freeReplyObject(reply);

    /* test 6 */
    printf("#6 can parse nil replies: ");
    reply = redisCommand(fd,"GET nokey");
    test_cond(reply->type == REDIS_REPLY_NIL)
    freeReplyObject(reply);

    /* test 7 */
    printf("#7 can parse integer replies: ");
    reply = redisCommand(fd,"INCR mycounter");
    test_cond(reply->type == REDIS_REPLY_INTEGER && reply->integer == 1)
    freeReplyObject(reply);

    /* test 8 */
    printf("#8 can parse multi bulk replies: ");
    freeReplyObject(redisCommand(fd,"LPUSH mylist foo"));
    freeReplyObject(redisCommand(fd,"LPUSH mylist bar"));
    reply = redisCommand(fd,"LRANGE mylist 0 -1");
    test_cond(reply->type == REDIS_REPLY_ARRAY &&
              reply->elements == 2 &&
              !memcmp(reply->element[0]->reply,"bar",3) &&
              !memcmp(reply->element[1]->reply,"foo",3))
    freeReplyObject(reply);

    /* test 9 (m/e with multi bulk reply *before* other reply).
     * specifically test ordering of reply items to parse. */
    printf("#10 can handle nested multi bulk replies: ");
    freeReplyObject(redisCommand(fd,"MULTI"));
    freeReplyObject(redisCommand(fd,"LRANGE mylist 0 -1"));
    freeReplyObject(redisCommand(fd,"PING"));
    reply = (redisCommand(fd,"EXEC"));
    test_cond(reply->type == REDIS_REPLY_ARRAY &&
              reply->elements == 2 &&
              reply->element[0]->type == REDIS_REPLY_ARRAY &&
              reply->element[0]->elements == 2 &&
              !memcmp(reply->element[0]->element[0]->reply,"bar",3) &&
              !memcmp(reply->element[0]->element[1]->reply,"foo",3) &&
              reply->element[1]->type == REDIS_REPLY_STRING &&
              strcasecmp(reply->element[1]->reply,"pong") == 0);
    freeReplyObject(reply);

    if (fails == 0) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("*** %d TESTS FAILED ***\n", fails);
    }

    printf("\nSpeed tests:\n");
    for (i = 0; i < 500; i++)
        freeReplyObject(redisCommand(fd,"LPUSH mylist foo"));

    t1 = usec();
    for (i = 0; i < 1000; i++)
        freeReplyObject(redisCommand(fd,"PING"));
    t2 = usec();
    printf("(1000x PING: %.2fs)\n", (t2-t1)/1000000.0);

    t1 = usec();
    for (i = 0; i < 1000; i++)
        freeReplyObject(redisCommand(fd,"LRANGE mylist 0 499"));
    t2 = usec();
    printf("(1000x LRANGE with 500 elements: %.2fs)\n", (t2-t1)/1000000.0);

    /* Clean DB 9 */
    reply = redisCommand(fd,"FLUSHDB");
    freeReplyObject(reply);

    return 0;
}
