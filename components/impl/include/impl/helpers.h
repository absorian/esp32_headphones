#ifndef IMPL_HELPERS_H
#define IMPL_HELPERS_H

#include <type_traits>

template<typename CTX_FUNC>
struct ctx_func_t {
private:
    CTX_FUNC m_func = nullptr;
    void* m_context = nullptr;
public:
    operator bool() const {
        return m_func;
    }

    template<typename... ARGS>
    using ret_type = std::invoke_result_t<CTX_FUNC, ARGS..., void* >;

    template<typename... ARGS>
    ret_type<ARGS...> operator()(ARGS... args) {
        if (m_func) return m_func(args..., m_context);

        return ret_type<ARGS...>{};
    }

    ctx_func_t() = default;
    ctx_func_t(CTX_FUNC func) : m_func(func) {}
    ctx_func_t(CTX_FUNC func, void* context) : m_func(func), m_context(context) {}

    void* context() {
        return m_context;
    }

    CTX_FUNC function() {
        return m_func;
    }
};

#define BAD_VAL_CHECK(x, b) if (x == b) { loge(TAG, #x " has bad value = " #b); }
#define BAD_VAL_MSG(x, b, m) if (x == b) { loge(TAG, #x " has bad value = " #b ": " m); }
#define BAD_VAL_MSG_IF(x, b, m) BAD_VAL_MSG(x, b, m) if(x == b)
#define BAD_VAL_IF(x, b) BAD_VAL_CHECK(x, b) if(x == b)

#endif //IMPL_HELPERS_H
