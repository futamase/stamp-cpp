#ifndef TX_EXCEPTION_HPP
#define TX_EXCEPTION_HPP

//#include <exception>
#include <stdexcept>
#include <string>

class inter_tx_exception : public std::runtime_error
{
    public:
    inter_tx_exception(const char* what_arg, int threadID, void* p = nullptr) 
        : runtime_error(what_arg), tid(threadID), ptr(p)
    {}
    inter_tx_exception(const std::string& what_arg, int threadID, void* p = nullptr)
        : runtime_error(what_arg), tid(threadID), ptr(p)
    {}

    int tid;
    void* ptr;
};

#endif