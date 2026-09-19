#pragma once
#include <stdexcept>
#include <utility>
#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <system_error>
#include <unistd.h>
#endif

namespace webscene::graphics {
// Ops isolates the POSIX syscalls so ownership and failure rollback remain
// contract-testable without consuming process descriptors.
template<class Ops> class unique_posix_fd {
    int value_=Ops::empty();
    explicit unique_posix_fd(int owned):value_(owned) {}
public:
    unique_posix_fd()=default;
    unique_posix_fd(const unique_posix_fd&)=delete;
    unique_posix_fd& operator=(const unique_posix_fd&)=delete;
    unique_posix_fd(unique_posix_fd&& other) noexcept:value_(other.release()) {}
    unique_posix_fd& operator=(unique_posix_fd&& other) noexcept {
        if(this!=&other){reset();value_=other.release();}
        return *this;
    }
    ~unique_posix_fd(){reset();}
    static unique_posix_fd adopt(int owned){
        if(!Ops::valid(owned))throw std::invalid_argument("invalid owned file descriptor");
        return unique_posix_fd(owned);
    }
    static unique_posix_fd duplicate(int borrowed){
        if(!Ops::valid(borrowed))throw std::invalid_argument("invalid borrowed file descriptor");
        return adopt(Ops::duplicate(borrowed));
    }
    int get()const noexcept{return value_;}
    explicit operator bool()const noexcept{return Ops::valid(value_);}
    int release()noexcept{return std::exchange(value_,Ops::empty());}
    void reset()noexcept{const auto owned=release();if(Ops::valid(owned))Ops::close(owned);}
};

#if defined(__linux__)
struct linux_fd_ops {
    static int empty()noexcept{return -1;}
    static bool valid(int value)noexcept{return value>=0;}
    static void close(int value)noexcept{
        // close(2) may report EINTR after the descriptor number was consumed;
        // retrying could close an unrelated descriptor reused by another thread.
        ::close(value);
    }
    static int duplicate(int borrowed){
        int result;
        do result=::fcntl(borrowed,F_DUPFD_CLOEXEC,0); while(result<0&&errno==EINTR);
        if(result<0)throw std::system_error(errno,std::generic_category(),"F_DUPFD_CLOEXEC");
        return result;
    }
};
using owned_posix_fd=unique_posix_fd<linux_fd_ops>;
#endif
} // namespace webscene::graphics
