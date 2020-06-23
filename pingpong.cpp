
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <type_traits>
#include <signal.h>
#include <errno.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

std::pair<std::string, int> parentFtok;
std::pair<std::string, int> currentFtok{"./pingpong", 21};
int ball_received = 0;
int ball_send = 0;
pid_t rootProcess = -1;
int numberOfRounds = 0;

struct MessageInBalls
{
    long type;
    char payload[100];
    int pidOfReceiver;
};

enum class MessageInBallsType : long
{
    forward = 4,
    backward = 5
};

using Message = char *;

char *getMessage(MessageInBallsType type)
{
    if (type == MessageInBallsType::forward)
        return strdup("my forward message");
    else
        return strdup("my backward message");
}

void printErrorExit()
{
    printf("error %s\n", strerror(errno));
    std::exit(EXIT_FAILURE);
}

struct ReceivedData
{
    pid_t pid;
    std::string firstFtok;
    int secondFtok;
};

ReceivedData childData;
ReceivedData withoutChild{-1, "NULL", -1};

void read_statistic(pid_t pid)
{
    std::string shm_name = std::string{"shared_"} + std::to_string(pid);

    int fd_shm = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd_shm == -1)
        printErrorExit();

    const int size = getpagesize();
    void *mem_ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    if (mem_ptr == MAP_FAILED)
        printErrorExit();

    auto ptr = reinterpret_cast<int *>(mem_ptr);
    printf("sent ball: %d\n", *ptr);
    ptr++;
    printf("received ball: %d\n", *ptr);

    munmap(mem_ptr, size);
}

void feedBall()
{
    key_t key{ftok(childData.firstFtok.c_str(), childData.secondFtok)};
    if (key == -1)
        printErrorExit();

    int queue_id{msgget(key, 0666 | IPC_CREAT)};
    if (queue_id == -1)
        printErrorExit();

    MessageInBalls msg;
    msg.type = static_cast<std::underlying_type<MessageInBallsType>::type>(MessageInBallsType::forward);
    strcpy(msg.payload, getMessage(MessageInBallsType::forward));
    msg.pidOfReceiver = childData.pid;

    if (msgsnd(queue_id, &msg, sizeof(msg), IPC_NOWAIT) == -1)
        printErrorExit();
    printf("this is root process %d | Feed ball into game to my child %d\n", getpid(), childData.pid);
    ball_send++;
}

void sigusr1ToFeedBall(int sig_no)
{
    if (sig_no == SIGUSR1)
    {
        printf("SIGUSR1 signal:\n");
        feedBall();
        return;
    }
    printf("invalid signal\n");
}
void sigusr2ToReadStatistic(int sig_no)
{
    if (sig_no == SIGUSR2)
    {
        printf("SIGUSR2 signal:\n");
        read_statistic(getpid());
        return;
    }
    printf("invalid signal\n");
}

void signalForPingpong()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &sigusr1ToFeedBall;
    if (sigaction(SIGUSR1, &sa, NULL) == -1)
        printErrorExit();
}

void signalForStats()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &sigusr2ToReadStatistic;
    if (sigaction(SIGUSR2, &sa, NULL) == -1)
        printErrorExit();
}

ReceivedData spawnPlayerP(int numberOfPlayer)
{
    pid_t child_pid;
    for (int playerNo = 0; playerNo < numberOfPlayer; playerNo++)
    {
        child_pid = fork();

        int child_ftok_second = numberOfPlayer + rand() % 100;
        if (child_pid == -1)
            printErrorExit();
        if (child_pid != 0)
            return ReceivedData{child_pid, "./pingpong", child_ftok_second};
        if (child_pid == 0)
        {
            printf("child player process created: %d | parent: %d\n", getpid(), getppid());
            parentFtok.first = currentFtok.first;
            parentFtok.second = currentFtok.second;

            currentFtok.first = "./pingpong";
            currentFtok.second = child_ftok_second;
        }
    }
    return withoutChild;
}

void updateStatistic(pid_t pid)
{
    std::string shm_name = std::string{"shared_"} + std::to_string(pid);

    int fd_shm = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd_shm == -1)
        printErrorExit();

    ftruncate(fd_shm, 2 * sizeof(int));

    const int size = getpagesize();
    auto mem_ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    if (mem_ptr == MAP_FAILED)
        printErrorExit();

    auto casted_mem_ptr = reinterpret_cast<int *>(mem_ptr);
    memcpy(casted_mem_ptr, &ball_send, sizeof(int));
    casted_mem_ptr++;
    memcpy(casted_mem_ptr, &ball_received, sizeof(int));

    munmap(mem_ptr, size);
    printf("-statistic has been updated-\n");
}

int main(int argc, char **argv)
{

    if (argc < 3)
    {
        printf("missing arguments\n");
        std::exit(EXIT_FAILURE);
    }

    int currentRound = 0;
    numberOfRounds = atoi(argv[1]);
    int numberOfPlayer = atoi(argv[2]) - 1;

    signalForPingpong();
    signalForStats();

    rootProcess = getpid();
    printf("root pid: %d | console pid: %d\n", getpid(), getppid());

    childData = spawnPlayerP(numberOfPlayer);
    updateStatistic(getpid());

    key_t childKey, currentKey, parentKey;
    int childQueueId, currentQueueId, parentQueueId;

    if (childData.pid != -1)
    {
        childKey = ftok(childData.firstFtok.c_str(), childData.secondFtok);
        if (childKey == -1)
            printErrorExit();

        childQueueId = msgget(childKey, 0666 | IPC_CREAT);
        if (childQueueId == -1)
            printErrorExit();
    }

    currentKey = ftok(currentFtok.first.c_str(), currentFtok.second);
    if (currentKey == -1)
        printErrorExit();

    currentQueueId = msgget(currentKey, 0666 | IPC_CREAT);
    if (currentQueueId == -1)
        printErrorExit();

    if (getpid() != rootProcess)
    {
        parentKey = ftok(parentFtok.first.c_str(), parentFtok.second);
        if (parentKey == -1)
            printErrorExit();

        parentQueueId = msgget(parentKey, 0666 | IPC_CREAT);
        if (parentQueueId == -1)
            printErrorExit();
    }

    int status = 0;
    MessageInBalls msgForLoop;
    while (currentRound < numberOfRounds)
    {
        status = msgrcv(currentQueueId, &msgForLoop, sizeof(msgForLoop), 4, IPC_NOWAIT);
        status = msgrcv(currentQueueId, &msgForLoop, sizeof(msgForLoop), 5, IPC_NOWAIT);
        if (msgForLoop.pidOfReceiver == getpid())
        {
            if (msgForLoop.type == static_cast<std::underlying_type<MessageInBallsType>::type>(MessageInBallsType::forward))
            {
                if (childData.pid == withoutChild.pid)
                {
                    msgForLoop.type = static_cast<std::underlying_type<MessageInBallsType>::type>(MessageInBallsType::backward);
                    strcpy(msgForLoop.payload, getMessage(MessageInBallsType::backward));
                    msgForLoop.pidOfReceiver = getppid();

                    if (msgsnd(parentQueueId, &msgForLoop, sizeof(msgForLoop), IPC_NOWAIT) == -1)
                        printErrorExit();
                    printf("this is %d | no child | forwarding ball to parent %d | round: %d\n", getpid(), getppid(), currentRound);
                    ball_send++;
                    ball_received++;
                    updateStatistic(getpid());
                    currentRound++;
                    continue;
                }
                msgForLoop.type = static_cast<std::underlying_type<MessageInBallsType>::type>(MessageInBallsType::forward);
                strcpy(msgForLoop.payload, getMessage(MessageInBallsType::forward));
                msgForLoop.pidOfReceiver = childData.pid;

                if (msgsnd(childQueueId, &msgForLoop, sizeof(msgForLoop), IPC_NOWAIT) == -1)
                    printErrorExit();
                printf("this is %d | forwarding ball to %d | round: %d\n", getpid(), childData.pid, currentRound);
                ball_send++;
                ball_received++;
                updateStatistic(getpid());
                continue;
            }
            if (msgForLoop.type == static_cast<std::underlying_type<MessageInBallsType>::type>(MessageInBallsType::backward))
            {
                if (msgForLoop.pidOfReceiver == rootProcess)
                {
                    currentRound++;
                    updateStatistic(getpid());
                    if (currentRound < numberOfRounds)
                    {
                        msgForLoop.type = static_cast<std::underlying_type<MessageInBallsType>::type>(MessageInBallsType::forward);
                        strcpy(msgForLoop.payload, getMessage(MessageInBallsType::forward));
                        msgForLoop.pidOfReceiver = childData.pid;

                        if (msgsnd(childQueueId, &msgForLoop, sizeof(msgForLoop), IPC_NOWAIT) == -1)
                            printErrorExit();
                        printf("this is ROOT %d | forwarding ball to %d\n", getpid(), childData.pid);
                        ball_send++;
                        ball_received++;
                        updateStatistic(getpid());
                        continue;
                    }
                    break;
                }
                msgForLoop.type = static_cast<std::underlying_type<MessageInBallsType>::type>(MessageInBallsType::backward);
                strcpy(msgForLoop.payload, getMessage(MessageInBallsType::backward));
                msgForLoop.pidOfReceiver = getppid();
                currentRound++;

                if (msgsnd(parentQueueId, &msgForLoop, sizeof(msgForLoop), IPC_NOWAIT) == -1)
                    printErrorExit();
                printf("this is %d | forwarding ball to %d | round: %d\n", getpid(), getppid(), currentRound);
                ball_send++;
                ball_received++;
                updateStatistic(getpid());
            }
        }
        sleep(2);
    }

    if (childData.pid != withoutChild.pid)
    {
        printf("%d is done | wait for child %d termination\n", getpid(), childData.pid);
        int child_status = -1;
        waitpid(childData.pid, &child_status, 0);

        if (child_status == 1)
        {
            printf("child %d terminated with error\n", childData.pid);
        }
        if (child_status == 0)
        {
            printf("child %d terminated without error\n", childData.pid);
        }
    }
    if (msgctl(currentQueueId, IPC_RMID, NULL) == -1)
        printErrorExit();
}