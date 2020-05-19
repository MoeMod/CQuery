#include "GlobalContext.h"
#include "boost/asio.hpp"

struct Context : std::enable_shared_from_this<Context> {
    boost::asio::io_context io_context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard;
    std::vector<std::thread> thread_pool;

    Context() : work_guard(make_work_guard(io_context))
    {
        // dont start here when shared_from_this() is not ready
    }

    ~Context()
    {
        stop();
    }

    std::shared_ptr<Context> start(int thread_num = std::max<int>(std::thread::hardware_concurrency() * 2 + 1, 2))
    {
        assert(thread_num >= 1);
        std::generate_n(std::back_inserter(thread_pool), thread_num, std::bind(&Context::make_thread, this));
        return shared_from_this();
    }

    void stop()
    {
        io_context.stop();
        std::for_each(thread_pool.begin(), thread_pool.end(), std::mem_fn(&std::thread::join));
        thread_pool.clear();
    }

    std::thread make_thread()
    {
        return std::thread([&ioc = io_context]{ ioc.run(); });
    }
};

std::shared_ptr<boost::asio::io_context> GlobalContextSingleton() {
    static auto sp = std::make_shared<Context>()->start();
    return std::shared_ptr<boost::asio::io_context>(sp, &sp->io_context);
}