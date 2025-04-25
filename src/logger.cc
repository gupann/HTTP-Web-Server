#include "logger.h"
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/support/date_time.hpp>
#include <ios>

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace expr = boost::log::expressions;

void logger::init(const std::string& pattern)
{
    // console sink
    logging::add_console_log(
        std::clog,
        logging::keywords::format =
          expr::stream
            << "[" << expr::format_date_time<boost::posix_time::ptime>(
                        "TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
            << "][" << expr::attr<logging::attributes::current_thread_id::value_type>("ThreadID")
            << "][" << logging::trivial::severity
            << "] " << expr::smessage);
    
    // rotating file sink
    logging::add_file_log(
        logging::keywords::file_name = pattern,
        logging::keywords::rotation_size = 10 * 1024 * 1024,
        logging::keywords::time_based_rotation = sinks::file::rotation_at_time_point{0,0,0},
        logging::keywords::open_mode = std::ios_base::app,
        logging::keywords::auto_flush = true,
        logging::keywords::format =
          expr::stream
            << "[" << expr::format_date_time<boost::posix_time::ptime>(
                        "TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
            << "][" << expr::attr<logging::attributes::current_thread_id::value_type>("ThreadID")
            << "][" << logging::trivial::severity
            << "] " << expr::smessage);

    logging::add_common_attributes();
}