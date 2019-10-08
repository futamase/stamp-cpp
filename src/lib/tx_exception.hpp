#ifndef TX_EXCEPTION_HPP
#define TX_EXCEPTION_HPP

//#include <exception>
#include <stdexcept>
#include <string>

enum AbortStatus {
    Explicit = 0,
    Load,
    Store,
    Allocation,
    Free, 
    Validation,

    NUM_STATUS
};

class inter_tx_exception : public std::runtime_error
{
    public:
    inter_tx_exception(const char* what_arg, int threadID, void* p = nullptr) 
        : runtime_error(what_arg), tid(threadID), ptr(p)
    {}
    inter_tx_exception(const std::string& what_arg, int threadID, void* p = nullptr)
        : runtime_error(what_arg), tid(threadID), ptr(p)
    {}

    inter_tx_exception(AbortStatus status, int threadID) 
        : runtime_error("Abort"), tid(threadID), status(status) 
    {}


    int tid;
    void* ptr;
    AbortStatus status;
};

#endif