#ifndef SECURITY_MANAGER_HPP
#define SECURITY_MANAGER_HPP

#include "config.hpp"
#include <string>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <random>
#include <crypto++/sha.h>
#include <crypto++/hex.h>

enum class OperationType {
    READ,
    WRITE,
    DELETE,
    ADMIN,
    BATCH_OPERATION,
    METRICS_ACCESS,
    CONFIG_ACCESS
};

enum class PermissionLevel {
    NONE,
    READ_ONLY,
    READ_WRITE,
    ADMIN,
    SUPER_ADMIN
};

struct UserCredentials {
    std::string username;
    std::string password_hash;
    std::string salt;
    PermissionLevel permission_level;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_login;
    bool is_active;
    std::vector<std::string> allowed_symbols;  // For symbol-specific permissions
};

struct AuditLogEntry {
    std::string username;
    std::string operation;
    std::string details;
    std::chrono::system_clock::time_point timestamp;
    std::string ip_address;
    bool success;
    std::string error_message;
};

class SecurityManager {
private:
    CacheConfig config_;
    std::unordered_map<std::string, UserCredentials> users_;
    std::unordered_map<std::string, std::atomic<uint64_t>> rate_limiters_;
    std::vector<AuditLogEntry> audit_log_;
    std::mutex users_mutex_;
    std::mutex audit_mutex_;
    std::mutex rate_limit_mutex_;
    
    // Rate limiting configuration
    struct RateLimitConfig {
        uint64_t max_requests_per_second;
        uint64_t max_requests_per_minute;
        uint64_t max_requests_per_hour;
        std::chrono::seconds window_size;
    };
    
    RateLimitConfig default_rate_limit_{1000, 60000, 3600000, std::chrono::seconds(1)};
    
    // Encryption
    std::string encryption_key_;
    bool encryption_enabled_{false};

public:
    explicit SecurityManager(const CacheConfig& config) : config_(config) {
        initialize_default_users();
    }
    
    // Authentication
    bool authenticate_user(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> lock(users_mutex_);
        
        auto it = users_.find(username);
        if (it == users_.end()) {
            log_audit_entry(username, "AUTHENTICATION", "User not found", false, "User does not exist");
            return false;
        }
        
        UserCredentials& user = it->second;
        if (!user.is_active) {
            log_audit_entry(username, "AUTHENTICATION", "User inactive", false, "Account disabled");
            return false;
        }
        
        std::string hashed_password = hash_password(password, user.salt);
        if (hashed_password != user.password_hash) {
            log_audit_entry(username, "AUTHENTICATION", "Invalid password", false, "Password mismatch");
            return false;
        }
        
        user.last_login = std::chrono::system_clock::now();
        log_audit_entry(username, "AUTHENTICATION", "Login successful", true, "");
        return true;
    }
    
    // Authorization
    bool authorize_operation(const std::string& username, OperationType operation, 
                           const std::string& symbol = "") {
        std::lock_guard<std::mutex> lock(users_mutex_);
        
        auto it = users_.find(username);
        if (it == users_.end()) {
            return false;
        }
        
        UserCredentials& user = it->second;
        if (!user.is_active) {
            return false;
        }
        
        // Check permission level
        if (!has_permission(user.permission_level, operation)) {
            log_audit_entry(username, "AUTHORIZATION", "Insufficient permissions", false, 
                          "Operation not allowed for permission level");
            return false;
        }
        
        // Check symbol-specific permissions
        if (!symbol.empty() && !user.allowed_symbols.empty()) {
            if (std::find(user.allowed_symbols.begin(), user.allowed_symbols.end(), symbol) 
                == user.allowed_symbols.end()) {
                log_audit_entry(username, "AUTHORIZATION", "Symbol access denied", false, 
                              "Symbol not in allowed list");
                return false;
            }
        }
        
        return true;
    }
    
    // Rate limiting
    bool allow_operation(const std::string& client_id, OperationType operation) {
        std::string rate_limit_key = client_id + "_" + std::to_string(static_cast<int>(operation));
        
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        
        auto& rate_limiter = rate_limiters_[rate_limit_key];
        uint64_t current_count = rate_limiter.load();
        
        if (current_count >= default_rate_limit_.max_requests_per_second) {
            log_audit_entry(client_id, "RATE_LIMIT", "Rate limit exceeded", false, 
                          "Too many requests per second");
            return false;
        }
        
        rate_limiter.fetch_add(1);
        return true;
    }
    
    void set_rate_limit(const std::string& client_id, uint64_t ops_per_second) {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        // Implementation would update rate limits for specific clients
    }
    
    // User management
    bool create_user(const std::string& username, const std::string& password, 
                    PermissionLevel permission_level) {
        std::lock_guard<std::mutex> lock(users_mutex_);
        
        if (users_.find(username) != users_.end()) {
            return false;  // User already exists
        }
        
        std::string salt = generate_salt();
        std::string password_hash = hash_password(password, salt);
        
        UserCredentials user;
        user.username = username;
        user.password_hash = password_hash;
        user.salt = salt;
        user.permission_level = permission_level;
        user.created_at = std::chrono::system_clock::now();
        user.is_active = true;
        
        users_[username] = user;
        
        log_audit_entry("SYSTEM", "USER_CREATION", "User created", true, 
                       "Created user: " + username);
        return true;
    }
    
    bool update_user_permissions(const std::string& username, PermissionLevel new_level) {
        std::lock_guard<std::mutex> lock(users_mutex_);
        
        auto it = users_.find(username);
        if (it == users_.end()) {
            return false;
        }
        
        it->second.permission_level = new_level;
        
        log_audit_entry("SYSTEM", "PERMISSION_UPDATE", "Permissions updated", true, 
                       "Updated permissions for: " + username);
        return true;
    }
    
    bool deactivate_user(const std::string& username) {
        std::lock_guard<std::mutex> lock(users_mutex_);
        
        auto it = users_.find(username);
        if (it == users_.end()) {
            return false;
        }
        
        it->second.is_active = false;
        
        log_audit_entry("SYSTEM", "USER_DEACTIVATION", "User deactivated", true, 
                       "Deactivated user: " + username);
        return true;
    }
    
    // Audit logging
    void log_audit_entry(const std::string& username, const std::string& operation, 
                        const std::string& details, bool success, const std::string& error_message) {
        std::lock_guard<std::mutex> lock(audit_mutex_);
        
        AuditLogEntry entry;
        entry.username = username;
        entry.operation = operation;
        entry.details = details;
        entry.timestamp = std::chrono::system_clock::now();
        entry.success = success;
        entry.error_message = error_message;
        
        audit_log_.push_back(entry);
        
        // Maintain audit log size
        if (audit_log_.size() > 10000) {
            audit_log_.erase(audit_log_.begin());
        }
    }
    
    std::vector<AuditLogEntry> get_audit_log(const std::string& username = "", 
                                            size_t limit = 100) const {
        std::lock_guard<std::mutex> lock(audit_mutex_);
        
        std::vector<AuditLogEntry> filtered_log;
        for (const auto& entry : audit_log_) {
            if (username.empty() || entry.username == username) {
                filtered_log.push_back(entry);
                if (filtered_log.size() >= limit) break;
            }
        }
        
        return filtered_log;
    }
    
    // Encryption
    void enable_encryption(const std::string& key) {
        encryption_key_ = key;
        encryption_enabled_ = true;
    }
    
    void disable_encryption() {
        encryption_enabled_ = false;
        encryption_key_.clear();
    }
    
    std::string encrypt_data(const std::string& data) {
        if (!encryption_enabled_) {
            return data;
        }
        
        // Implementation would use encryption_key_ to encrypt data
        // This is a placeholder
        return data;
    }
    
    std::string decrypt_data(const std::string& encrypted_data) {
        if (!encryption_enabled_) {
            return encrypted_data;
        }
        
        // Implementation would use encryption_key_ to decrypt data
        // This is a placeholder
        return encrypted_data;
    }
    
    // Security checks
    bool validate_input(const std::string& input) {
        // Check for SQL injection, XSS, etc.
        if (input.find("'") != std::string::npos || 
            input.find(";") != std::string::npos ||
            input.find("<script>") != std::string::npos) {
            return false;
        }
        return true;
    }
    
    bool is_suspicious_activity(const std::string& username) {
        // Check for unusual patterns
        auto recent_logs = get_audit_log(username, 100);
        int failed_attempts = 0;
        
        for (const auto& entry : recent_logs) {
            if (!entry.success && entry.operation == "AUTHENTICATION") {
                failed_attempts++;
            }
        }
        
        return failed_attempts > 5;  // More than 5 failed attempts
    }

private:
    void initialize_default_users() {
        // Create default admin user
        create_user("admin", "admin123", PermissionLevel::SUPER_ADMIN);
        
        // Create default read-only user
        create_user("reader", "reader123", PermissionLevel::READ_ONLY);
    }
    
    bool has_permission(PermissionLevel user_level, OperationType operation) {
        switch (operation) {
            case OperationType::READ:
                return user_level >= PermissionLevel::READ_ONLY;
            case OperationType::WRITE:
                return user_level >= PermissionLevel::READ_WRITE;
            case OperationType::DELETE:
                return user_level >= PermissionLevel::ADMIN;
            case OperationType::ADMIN:
            case OperationType::BATCH_OPERATION:
            case OperationType::METRICS_ACCESS:
            case OperationType::CONFIG_ACCESS:
                return user_level >= PermissionLevel::ADMIN;
            default:
                return false;
        }
    }
    
    std::string generate_salt() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        std::string salt;
        salt.reserve(16);
        for (int i = 0; i < 16; ++i) {
            salt += static_cast<char>(dis(gen));
        }
        
        return salt;
    }
    
    std::string hash_password(const std::string& password, const std::string& salt) {
        // Implementation would use proper cryptographic hashing
        // This is a placeholder - in production, use bcrypt, Argon2, or similar
        return password + salt;  // Simplified for demonstration
    }
};

#endif 