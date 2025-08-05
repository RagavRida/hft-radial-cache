#include "error_handler.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>

ErrorHandler::ErrorHandler(const CacheConfig& config) : config_(config) {
    // Register default recovery strategies
    register_recovery_strategy(ErrorType::MEMORY_ALLOCATION_FAILED, 
        [this](const ErrorInfo& error) {
            return handle_memory_allocation_failure(error);
        });
    
    register_recovery_strategy(ErrorType::MEMORY_CORRUPTION,
        [this](const ErrorInfo& error) {
            return handle_memory_corruption(error);
        });
    
    register_recovery_strategy(ErrorType::THREAD_CONTENTION,
        [this](const ErrorInfo& error) {
            return handle_thread_contention(error);
        });
    
    register_recovery_strategy(ErrorType::DATA_CORRUPTION,
        [this](const ErrorInfo& error) {
            return handle_data_corruption(error);
        });
    
    register_recovery_strategy(ErrorType::NUMA_ERROR,
        [this](const ErrorInfo& error) {
            return handle_numa_error(error);
        });
}

void ErrorHandler::report_error(ErrorType type, ErrorSeverity severity, 
                               const std::string& message, const std::string& function,
                               const std::string& file, int line) {
    ErrorInfo error_info(type, severity, message, function, file, line);
    report_error(error_info);
}

void ErrorHandler::report_error(const ErrorInfo& error) {
    total_errors_.fetch_add(1);
    
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        error_history_.push_back(error);
        
        // Maintain history size
        if (error_history_.size() > 1000) {
            error_history_.erase(error_history_.begin());
        }
    }
    
    log_error(error);
    check_error_thresholds();
    
    // Attempt automatic recovery for non-critical errors
    if (error.severity != ErrorSeverity::CRITICAL && config_.enable_error_recovery) {
        attempt_recovery(error);
    }
}

void ErrorHandler::report_exception(const std::exception& e, const std::string& context) {
    ErrorInfo error_info(ErrorType::UNKNOWN_ERROR, ErrorSeverity::HIGH, 
                        e.what(), "exception_handler", "unknown", 0);
    
    if (!context.empty()) {
        error_info.message = context + ": " + e.what();
    }
    
    report_error(error_info);
}

bool ErrorHandler::attempt_recovery(const ErrorInfo& error) {
    recovery_attempts_.fetch_add(1);
    
    if (execute_recovery_strategy(error)) {
        successful_recoveries_.fetch_add(1);
        return true;
    }
    
    return false;
}

bool ErrorHandler::perform_emergency_recovery() {
    std::cout << "Performing emergency recovery..." << std::endl;
    
    // Clear error history
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        error_history_.clear();
    }
    
    // Reset error counters
    total_errors_.store(0);
    recovery_attempts_.store(0);
    successful_recoveries_.store(0);
    
    // Perform system-wide recovery
    bool recovery_success = true;
    
    // Try to recover from memory issues
    if (!handle_memory_corruption(ErrorInfo(ErrorType::MEMORY_CORRUPTION, 
                                           ErrorSeverity::CRITICAL, 
                                           "Emergency memory recovery", "", "", 0))) {
        recovery_success = false;
    }
    
    // Try to recover from thread contention
    if (!handle_thread_contention(ErrorInfo(ErrorType::THREAD_CONTENTION, 
                                           ErrorSeverity::CRITICAL, 
                                           "Emergency thread recovery", "", "", 0))) {
        recovery_success = false;
    }
    
    std::cout << "Emergency recovery " << (recovery_success ? "completed" : "failed") << std::endl;
    return recovery_success;
}

void ErrorHandler::register_recovery_strategy(ErrorType type, 
                                             std::function<bool(const ErrorInfo&)> strategy) {
    recovery_strategies_[type] = strategy;
}

std::vector<ErrorInfo> ErrorHandler::get_recent_errors(size_t count) const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    
    size_t start = (error_history_.size() > count) ? 
                   error_history_.size() - count : 0;
    
    return std::vector<ErrorInfo>(error_history_.begin() + start, error_history_.end());
}

std::vector<ErrorInfo> ErrorHandler::get_errors_by_type(ErrorType type) const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    
    std::vector<ErrorInfo> filtered_errors;
    for (const auto& error : error_history_) {
        if (error.type == type) {
            filtered_errors.push_back(error);
        }
    }
    
    return filtered_errors;
}

std::vector<ErrorInfo> ErrorHandler::get_errors_by_severity(ErrorSeverity severity) const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    
    std::vector<ErrorInfo> filtered_errors;
    for (const auto& error : error_history_) {
        if (error.severity == severity) {
            filtered_errors.push_back(error);
        }
    }
    
    return filtered_errors;
}

double ErrorHandler::get_recovery_success_rate() const {
    uint64_t attempts = recovery_attempts_.load();
    uint64_t successes = successful_recoveries_.load();
    
    if (attempts == 0) return 0.0;
    return static_cast<double>(successes) / static_cast<double>(attempts);
}

bool ErrorHandler::is_system_healthy() const {
    // Check if error rate is within acceptable limits
    auto recent_errors = get_recent_errors(100);
    if (recent_errors.size() > 50) {  // More than 50 errors in recent history
        return false;
    }
    
    // Check for critical errors
    auto critical_errors = get_errors_by_severity(ErrorSeverity::CRITICAL);
    if (!critical_errors.empty()) {
        return false;
    }
    
    // Check recovery success rate
    if (get_recovery_success_rate() < 0.8) {  // Less than 80% success rate
        return false;
    }
    
    return true;
}

bool ErrorHandler::should_trigger_emergency_mode() const {
    auto recent_errors = get_recent_errors(10);
    
    // Count critical and high severity errors
    int critical_count = 0;
    int high_count = 0;
    
    for (const auto& error : recent_errors) {
        if (error.severity == ErrorSeverity::CRITICAL) {
            critical_count++;
        } else if (error.severity == ErrorSeverity::HIGH) {
            high_count++;
        }
    }
    
    // Trigger emergency mode if too many critical errors or high severity errors
    return critical_count > 0 || high_count > 5;
}

void ErrorHandler::set_error_thresholds(const ErrorThresholds& thresholds) {
    thresholds_ = thresholds;
}

void ErrorHandler::clear_error_history() {
    std::lock_guard<std::mutex> lock(error_mutex_);
    error_history_.clear();
}

// Private methods
void ErrorHandler::log_error(const ErrorInfo& error) {
    std::stringstream ss;
    ss << "[" << std::chrono::duration_cast<std::chrono::seconds>(
           error.timestamp.time_since_epoch()).count() << "] ";
    
    switch (error.severity) {
        case ErrorSeverity::LOW:
            ss << "[LOW] ";
            break;
        case ErrorSeverity::MEDIUM:
            ss << "[MEDIUM] ";
            break;
        case ErrorSeverity::HIGH:
            ss << "[HIGH] ";
            break;
        case ErrorSeverity::CRITICAL:
            ss << "[CRITICAL] ";
            break;
    }
    
    ss << error.message << " in " << error.function_name 
       << " (" << error.file_name << ":" << error.line_number << ")";
    
    std::cerr << ss.str() << std::endl;
}

bool ErrorHandler::execute_recovery_strategy(const ErrorInfo& error) {
    auto it = recovery_strategies_.find(error.type);
    if (it != recovery_strategies_.end()) {
        return it->second(error);
    }
    
    // Default recovery strategy
    return perform_default_recovery(error);
}

std::string ErrorHandler::generate_stack_trace() const {
    // Simplified stack trace generation
    // In a real implementation, this would use platform-specific APIs
    return "Stack trace not available in this implementation";
}

void ErrorHandler::check_error_thresholds() {
    auto recent_errors = get_recent_errors(60);  // Last 60 errors
    
    // Check if we have too many errors per minute
    if (recent_errors.size() > thresholds_.max_errors_per_minute) {
        std::cerr << "WARNING: Error rate exceeded threshold (" 
                  << recent_errors.size() << " errors in last minute)" << std::endl;
    }
    
    // Check for consecutive failures
    int consecutive_failures = 0;
    for (auto it = recent_errors.rbegin(); it != recent_errors.rend(); ++it) {
        if (it->severity >= ErrorSeverity::HIGH) {
            consecutive_failures++;
        } else {
            break;
        }
    }
    
    if (consecutive_failures > thresholds_.max_consecutive_failures) {
        std::cerr << "WARNING: Too many consecutive failures (" 
                  << consecutive_failures << ")" << std::endl;
        trigger_emergency_mode();
    }
}

void ErrorHandler::trigger_emergency_mode() {
    std::cerr << "EMERGENCY MODE TRIGGERED" << std::endl;
    
    if (config_.enable_error_recovery) {
        perform_emergency_recovery();
    }
}

// Recovery strategy implementations
bool ErrorHandler::handle_memory_allocation_failure(const ErrorInfo& error) {
    std::cout << "Attempting memory allocation failure recovery..." << std::endl;
    
    // Try to free some memory
    // In a real implementation, this would trigger garbage collection
    // or memory pool cleanup
    
    std::this_thread::sleep_for(config_.retry_delay);
    return true;  // Assume recovery succeeded
}

bool ErrorHandler::handle_memory_corruption(const ErrorInfo& error) {
    std::cout << "Attempting memory corruption recovery..." << std::endl;
    
    // In a real implementation, this would:
    // 1. Validate memory integrity
    // 2. Rebuild corrupted data structures
    // 3. Restore from backup if necessary
    
    std::this_thread::sleep_for(config_.retry_delay);
    return true;  // Assume recovery succeeded
}

bool ErrorHandler::handle_thread_contention(const ErrorInfo& error) {
    std::cout << "Attempting thread contention recovery..." << std::endl;
    
    // In a real implementation, this would:
    // 1. Adjust thread scheduling
    // 2. Reduce contention by changing data access patterns
    // 3. Implement backoff strategies
    
    std::this_thread::sleep_for(config_.retry_delay);
    return true;  // Assume recovery succeeded
}

bool ErrorHandler::handle_data_corruption(const ErrorInfo& error) {
    std::cout << "Attempting data corruption recovery..." << std::endl;
    
    // In a real implementation, this would:
    // 1. Validate data integrity
    // 2. Restore corrupted data from backups
    // 3. Rebuild data structures
    
    std::this_thread::sleep_for(config_.retry_delay);
    return true;  // Assume recovery succeeded
}

bool ErrorHandler::handle_numa_error(const ErrorInfo& error) {
    std::cout << "Attempting NUMA error recovery..." << std::endl;
    
    // In a real implementation, this would:
    // 1. Fall back to standard memory allocation
    // 2. Reconfigure NUMA settings
    // 3. Migrate data to different NUMA nodes
    
    std::this_thread::sleep_for(config_.retry_delay);
    return true;  // Assume recovery succeeded
}

bool ErrorHandler::perform_default_recovery(const ErrorInfo& error) {
    std::cout << "Performing default recovery for error type: " 
              << static_cast<int>(error.type) << std::endl;
    
    // Default recovery: wait and retry
    std::this_thread::sleep_for(config_.retry_delay);
    return true;  // Assume recovery succeeded
}

// Global error handler instance
ErrorHandler* g_error_handler = nullptr; 