#include <iostream>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <csignal>
#include <ctime>
#include <sys/wait.h>

#define MSG_KEY 1234
#define TIMEOUT 10
#define MAX_WAIT_TIME 30

struct Message {
    long type;
    int value;
};

int f(int x) {
    return x * 2;
}

int g(int x) {
    return x + 3;
}

void worker(int queue_id, int x, bool is_f) {
    Message msg;
    msg.type = is_f ? 1 : 2;

    time_t start_time = time(nullptr);

    if (is_f) {
        msg.value = f(x);
    } else {
        msg.value = g(x);
    }

    msgsnd(queue_id, &msg, sizeof(Message) - sizeof(long), 0);
    exit(0);
}

int main() {
    int x;
    std::cout << "Введіть x: ";
    std::cin >> x;

    int queue_id = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (queue_id == -1) {
        perror("msgget");
        return 1;
    }

    std::cout << "[Main] Запуск процесів обчислення...\n";
    pid_t pid1 = fork();
    if (pid1 == 0) worker(queue_id, x, true);

    pid_t pid2 = fork();
    if (pid2 == 0) worker(queue_id, x, false);

    long long result_f = -1, result_g = -1;
    bool received_f = false, received_g = false;
    bool continue_without_asking = false;
    time_t start_time = time(nullptr);
    time_t no_ask_start_time = 0;

    while (!received_f  !received_g) {
        Message msg;
        if (msgrcv(queue_id, &msg, sizeof(Message) - sizeof(long), 0, IPC_NOWAIT) != -1) {
            if (msg.type == 1) {
                result_f = msg.value;
                received_f = true;
                std::cout << "[Main] Отримано f(x) = " << result_f << "\n";
            } else if (msg.type == 2) {
                result_g = msg.value;
                received_g = true;
                std::cout << "[Main] Отримано g(x) = " << result_g << "\n";
            }
        }

        if ((received_f && result_f == 0)  (received_g && result_g == 0)) {
            std::cout << "[Main] Результат: 0 (через множення на 0)\n";
            kill(pid1, SIGKILL);
            kill(pid2, SIGKILL);
            break;
        }

        if (!continue_without_asking && time(nullptr) - start_time >= TIMEOUT) {
            std::cout << "Обчислення тривають. 1) Продовжити, 2) Вийти, 3) Продовжити без запитань? ";
            int choice;
            std::cin >> choice;
            if (choice == 2) {
                std::cout << "[Main] Завершення роботи за вибором користувача.\n";
                kill(pid1, SIGKILL);
                kill(pid2, SIGKILL);
                break;
            } else if (choice == 3) {
                continue_without_asking = true;
                no_ask_start_time = time(nullptr);
            }
            start_time = time(nullptr);
        }

        if (continue_without_asking && time(nullptr) - no_ask_start_time >= MAX_WAIT_TIME) {
            std::cout << "[Main] Час очікування після вибору 3 (" << MAX_WAIT_TIME << " секунд) перевищено. Завершення програми.\n";
            kill(pid1, SIGKILL);
            kill(pid2, SIGKILL);
            break;
        }

        int status;
        if (!received_f && waitpid(pid1, &status, WNOHANG) == 0) {
            if (time(nullptr) - start_time >= MAX_WAIT_TIME) {
                std::cout << "[Main] f(x) завис, примусове завершення.\n";
                kill(pid1, SIGKILL);
                received_f = true;
            }
        }
        if (!received_g && waitpid(pid2, &status, WNOHANG) == 0) {
            if (time(nullptr) - start_time >= MAX_WAIT_TIME) {
                std::cout << "[Main] g(x) завис, примусове завершення.\n";
                kill(pid2, SIGKILL);
                received_g = true;
            }
        }
    }
    if (received_f && received_g) {
        std::cout << "[Main] Остаточний результат: " << (result_f * result_g) << "\n";
    }

    msgctl(queue_id, IPC_RMID, nullptr);
    return 0;
}
