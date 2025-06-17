
#include <csignal>
#include <thread>
#include <unistd.h>


void SignalHdlr() {
    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGINT);

    int32_t signum {0};

    (void)sigwait(&sigset, &signum);
}

int main() {
    sigset_t sigset;
    (void)sigfillset(&sigset);
    (void)pthread_sigmask(SIG_SETMASK, &sigset, nullptr);

    std::thread exitThrd = std::thread(&SignalHdlr);

    exitThrd.join();

    return 0;
}