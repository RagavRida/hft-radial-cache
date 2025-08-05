#ifndef ERROR_HANDLER_HPP
#define ERROR_HANDLER_HPP

#include "config.hpp"
#include <exception>
#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>

enum class ErrorSeverity {
    LOW,
    MEDIUM,
    HIGH,
    CRITICAL
};

enum class ErrorType {
    MEMORY_ALLOCATION_FAILED,
    MEMORY_CORRUPTION,
    THREAD_CONTENTION,
    LOCK_TIMEOUT,
    DATA_CORRUPTION,
    NUMA_ERROR,
    CONFIGURATION_ERROR,
    RECOVERY_FAILED,
    METRICS_ERROR,
    UNKNOWN_ERROR
};

struct ErrorInfo {
    ErrorType type;
    ErrorSeverity severity;
    std::string message;
    std::string function_name;
    std::string file_name;
    int line_number;
    std::chrono::steady_clock::time_point timestamp;
    std::string stack_trace;
    
    ErrorInfo(ErrorType t, ErrorSeverity s, const std::string& msg,
              const std::string& func, const std::string& file, int line)
        : type(t), severity(s), message(msg), function_name(func),
          file_name(file), line_number(line),
          timestamp(std::chrono::steady_clock::now()) {}
};

class CacheException : public std::exception {
private:
    ErrorInfo error_info_;
    
public:
    explicit CacheException(const ErrorInfo& info) : error_info_(info) {}
    
    const char* what() const noexcept override {
        return error_info_.message.c_str();
    }
    
    const ErrorInfo& get_error_info() const { return error_info_; }
};

class ErrorHandler {
private:
    CacheConfig config_;
    std::vector<ErrorInfo> error_history_;
    std::mutex error_mutex_;
    std::atomic<uint64_t> total_errors_{0};
    std::atomic<uint64_t> recovery_attempts_{0};
    std::atomic<uint64_t> successful_recoveries_{0};
    
    // Recovery strategies
    std::map<ErrorType, std::function<bool(const ErrorInfo&)>> recovery_strategies_;
    
    // Error thresholds
    struct ErrorThresholds {
        uint64_t max_errors_per_minute = 100;
        uint64_t max_consecutive_failures = 10;
        std::chrono::milliseconds recovery_timeout{5000};
    } thresholds_;

public:
    explicit ErrorHandler(const CacheConfig& config);
    ~ErrorHandler() = default;
    
    // Error reporting
    void report_error(ErrorType type, ErrorSeverity severity, 
                     const std::string& message, const std::string& function,
                     const std::string& file, int line);
    
    void report_error(const ErrorInfo& error);
    void report_exception(const std::exception& e, const std::string& context = "");
    
    // Recovery mechanisms
    bool attempt_recovery(const ErrorInfo& error);
    bool perform_emergency_recovery();
    void register_recovery_strategy(ErrorType type, 
                                   std::function<bool(const ErrorInfo&)> strategy);
    
    // Error analysis
    std::vector<ErrorInfo> get_recent_errors(size_t count = 100) const;
    std::vector<ErrorInfo> get_errors_by_type(ErrorType type) const;
    std::vector<ErrorInfo> get_errors_by_severity(ErrorSeverity severity) const;
    
    // Statistics
    uint64_t get_total_errors() const { return total_errors_.load(); }
    uint64_t get_recovery_attempts() const { return recovery_attempts_.load(); }
    uint64_t get_successful_recoveries() const { return successful_recoveries_.load(); }
    double get_recovery_success_rate() const;
    
    // Health checks
    bool is_system_healthy() const;
    bool should_trigger_emergency_mode() const;
    
    // Configuration
    void set_error_thresholds(const ErrorThresholds& thresholds);
    void clear_error_history();

private:
    void log_error(const ErrorInfo& error);
    bool execute_recovery_strategy(const ErrorInfo& error);
    std::string generate_stack_trace() const;
    void check_error_thresholds();
    void trigger_emergency_mode();
};

// Global error handler instance
extern ErrorHandler* g_error_handler;

// Convenience macros for error reporting
#define REPORT_ERROR(type, severity, message) \
    do { \
        if (g_error_handler) { \
            g_error_handler->report_error(type, severity, message, \
                                        __FUNCTION__, __FILE__, __LINE__); \
        } \
    } while(0)

#define THROW_CACHE_ERROR(type, severity, message) \
    do { \
        ErrorInfo error_info(type, severity, message, __FUNCTION__, __FILE__, __LINE__); \
        if (g_error_handler) { \
            g_error_handler->report_error(error_info); \
        } \
        throw CacheException(error_info); \
    } while(0)

#define TRY_RECOVERY(error_info) \
    do { \
        if (g_error_handler && !g_error_handler->attempt_recovery(error_info)) { \
            THROW_CACHE_ERROR(ErrorType::RECOVERY_FAILED, ErrorSeverity::CRITICAL, \
                             "Recovery attempt failed"); \
        } \
    } while(0)

// Error checking macros
#define CHECK_MEMORY(ptr) \
    do { \
        if (!(ptr)) { \
            THROW_CACHE_ERROR(ErrorType::MEMORY_ALLOCATION_FAILED, ErrorSeverity::HIGH, \
                             "Memory allocation failed"); \
        } \
    } while(0)

#define CHECK_BOUNDS(index, size) \
    do { \
        if ((index) >= (size)) { \
            THROW_CACHE_ERROR(ErrorType::DATA_CORRUPTION, ErrorSeverity::HIGH, \
                             "Index out of bounds"); \
        } \
    } while(0)

#endif 