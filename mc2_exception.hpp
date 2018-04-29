#include <exception>

class mc2_exception : public std::exception {
public:
    mc2_exception(const char *msg) : msg(msg) { }
    virtual const char *what() const noexcept override { return msg; }

private:
    const char *msg;
};
