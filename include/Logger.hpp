#pragma once
#include <iostream>
#include <sstream>
#include <mutex>
#include <string>
#include <chrono>
#include <iomanip>

namespace Log {

    enum class Level { INFO, MSG, WARN, ERROR, DEBUG };

    // ------------------------------------------------------------
    // Short timestamp helper: HH:MM:SS.mmm
    // ------------------------------------------------------------
    inline std::string short_timestamp() {
        using namespace std::chrono;

        auto now = system_clock::now();
        auto t = system_clock::to_time_t(now);
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%H:%M:%S")
            << "." << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

    // ------------------------------------------------------------
    // Logger core
    // ------------------------------------------------------------
    class Logger {
    public:
        static Logger& instance() {
            static Logger inst;
            return inst;
        }

        void setVerbose(bool v) {
            std::lock_guard<std::mutex> lock(mutex_);
            verbose_ = v;
        }

        bool isVerbose() const {
            return verbose_;
        }

        void write(Level lvl, const std::string& src, const std::string& msg) {
            std::lock_guard<std::mutex> lock(mutex_);

            // Filter based on verbosity
            if (!verbose_) {
                if (lvl == Level::INFO || lvl == Level::DEBUG)
                    return;
            }

            std::cerr << short_timestamp()
                      << " [" << src << "] "
                      << msg << "\n";
        }

    private:
        Logger() = default;
        mutable std::mutex mutex_;
        bool verbose_ = false;   // default: quiet mode
    };

    // ------------------------------------------------------------
    // Stream proxy
    // ------------------------------------------------------------
    class Stream {
    public:
        Stream(Level lvl, const std::string& src)
            : level_(lvl), source_(src) {}

        ~Stream() {
            Logger::instance().write(level_, source_, buffer_.str());
        }

        template<typename T>
        Stream& operator<<(const T& value) {
            buffer_ << value;
            return *this;
        }

    private:
        Level level_;
        std::string source_;
        std::ostringstream buffer_;
    };

    // ------------------------------------------------------------
    // Stream-style API
    // ------------------------------------------------------------
    inline Stream info(const std::string& src)  { return Stream(Level::INFO,  src); }
    inline Stream msg(const std::string& src)   { return Stream(Level::MSG,   src); }
    inline Stream warn(const std::string& src)  { return Stream(Level::WARN,  src); }
    inline Stream error(const std::string& src) { return Stream(Level::ERROR, src); }
    inline Stream debug(const std::string& src) { return Stream(Level::DEBUG, src); }

    // ------------------------------------------------------------
    // Direct-message overloads
    // ------------------------------------------------------------
    inline void info(const std::string& src, const std::string& msg) {
        Logger::instance().write(Level::INFO, src, msg);
    }

    inline void msg(const std::string& src, const std::string& msg) {
        Logger::instance().write(Level::MSG, src, msg);
    }

    inline void warn(const std::string& src, const std::string& msg) {
        Logger::instance().write(Level::WARN, src, msg);
    }

    inline void error(const std::string& src, const std::string& msg) {
        Logger::instance().write(Level::ERROR, src, msg);
    }

    inline void debug(const std::string& src, const std::string& msg) {
        Logger::instance().write(Level::DEBUG, src, msg);
    }

    // ------------------------------------------------------------
    // Verbosity control
    // ------------------------------------------------------------
    inline void setVerbose(bool v) {
        Logger::instance().setVerbose(v);
    }

    inline bool isVerbose() {
        return Logger::instance().isVerbose();
    }

}
