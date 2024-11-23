#include <cerrno>
#include <concepts>
#include <cstddef>
#include <iostream>
#include <print>
#include <regex>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>


const std::regex WIFI_CRASHED_RE(
        R"(;ath10k_pci.*(?:could not init core|failed to pop paddr list))",
        std::regex::icase | std::regex::optimize);


bool reportError(std::string_view action, int code)
{
    if (code == 0) {
        return false;
    }

    std::println(std::cerr, "Failed to {}: {}", action,
                 std::error_code(code, std::system_category()).message());

    return true;
}


struct SpawnAttrGuard {
    posix_spawnattr_t* spawnAttr;
    ~SpawnAttrGuard()
    {
        reportError("destroy spawn attr",
                    posix_spawnattr_destroy(spawnAttr));
    }
};


void doRunCommand(const char* const argv[])
{
    posix_spawnattr_t attr{};
    if (reportError("init spawn attr", posix_spawnattr_init(&attr))) {
        return;
    }
    SpawnAttrGuard attrGuard{&attr};
    sigset_t noSigs{};
    sigemptyset(&noSigs);
    reportError("set spawn attr sigmask",
                posix_spawnattr_setsigmask(&attr, &noSigs));
    reportError("set spawn attr flags",
                posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK));
    pid_t pid{};
    int rc = posix_spawnp(&pid, argv[0], nullptr, &attr,
                          const_cast<char* const*>(argv), environ);
    if (reportError("spawn sub-process", rc)) {
        return;
    }

    int wstatus{};
    if (waitpid(pid, &wstatus, 0) == -1) {
        reportError("wait for sub-process", errno);
    }
    else if (WIFSIGNALED(wstatus)) {
        const int sig = WTERMSIG(wstatus);
        std::println(std::cerr, "sub-process died of SIG{} ({}): {}",
                     sigabbrev_np(sig), sig, sigdescr_np(sig));
    }
    else if (WIFEXITED(wstatus)) {
        if (const int rc = WEXITSTATUS(wstatus); rc != 0) {
            std::println(std::cerr, "sub-process error: rc={}", rc);
        }
    }
    else {
        std::println(std::cerr, "sub-process died abnormally: {}", wstatus);
    }
}

void runCommand(const std::convertible_to<const char*> auto&... args)
{
    const char* const argv[] {args..., nullptr};
    doRunCommand(argv);
}

void processEntry(std::string_view msg)
{
    if (std::regex_search(msg.begin(), msg.end(), WIFI_CRASHED_RE)) {
        std::println("Reloading ath10k_pci kernel module");
        runCommand("modprobe", "-r", "ath10k_pci");
        runCommand("modprobe", "ath10k_pci");
    }
}


struct FdGuard {
    int fd;
    ~FdGuard() {
        if (close(fd) != 0) {
            reportError("close fd", errno);
        }
    }
};


int main()
{
    const int kmsgFd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (kmsgFd == -1) {
        reportError("open /dev/kmsg", errno);
        return 1;
    }
    FdGuard kmsgFdGuard{kmsgFd};

    if (lseek(kmsgFd, 0, SEEK_END) == -1) {
        reportError("seek to end of /dev/kmsg", errno);
        return 1;
    }

    std::println("Monitoring kernel log for ath10k_pci trouble...");

    sigset_t sigs{};
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGTERM);
    if (sigprocmask(SIG_SETMASK, &sigs, nullptr) == -1) {
        reportError("set signal mask", errno);
        return 1;
    }

    const int sigFd = signalfd(-1, &sigs, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sigFd == -1) {
        reportError("create signalfd", errno);
        return 1;
    }
    FdGuard sigFdGuard{sigFd};

    while (true) {
        pollfd pollFds[] = { {.fd = kmsgFd, .events = POLLIN, .revents = 0 },
                             {.fd = sigFd,  .events = POLLIN, .revents = 0 } };
        if (poll(pollFds, std::size(pollFds), -1) == -1) {
            reportError("poll", errno);
            return 1;
        }

        if (pollFds[1].revents & POLLIN) {
            signalfd_siginfo sigInfo{};
            const ssize_t sz = read(sigFd, &sigInfo, sizeof sigInfo);
            if (sz == sizeof sigInfo) {
                std::println("Caught SIG{}; exiting",
                             sigabbrev_np(sigInfo.ssi_signo));
                return 0;
            }
            else if (sz >= 0) {
                std::println(std::cerr,
                             "Caught signal but read unexpected number of "
                             "bytes from signalfd ({} != {}); exiting",
                             sz, sizeof sigInfo);
                return 1;
            }
            else {
                reportError("read from signalfd", errno);
                std::println(std::cerr, "Exiting");
                return 1;
            }
        }

        for (const auto& pfd : pollFds) {
            if ((pfd.revents & (POLLERR | POLLHUP)) != 0) {
                std::println(std::cerr, "poll error for fd {}: {}; exiting",
                             pfd.fd, pfd.revents);
                return 1;
            }
        }

        if (!(pollFds[0].revents & POLLIN)) {
            continue;
        }

        while (true) {
            char buf[8192];
            const ssize_t sz = read(kmsgFd, buf, sizeof buf);
            if (sz > 0) {
                processEntry({buf, std::size_t(sz)});
            }
            else if (sz == 0) {
                std::println(std::cerr, "/dev/kmsg unexpectedly reached EOF");
                return 1;
            }
            else if (errno == EAGAIN || errno == EPIPE) {
                break;
            }
            else if (errno != EINTR) {
                reportError("read from /dev/kmsg", errno);
                return 1;
            }
        }
    }
}
