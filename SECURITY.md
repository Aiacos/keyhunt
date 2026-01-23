# Security

This document describes security features and best practices for the keyhunt distributed mode.

## Table of Contents

- [Network Security](#network-security)
- [Rate Limiting](#rate-limiting)
- [Authentication](#authentication)
- [TLS Encryption](#tls-encryption)
- [Input Validation](#input-validation)
- [Thread Safety](#thread-safety)
- [Security Recommendations](#security-recommendations)
- [Reporting Vulnerabilities](#reporting-vulnerabilities)

## Network Security

### Bind Address Configuration

By default, the distributed coordinator binds to all network interfaces (0.0.0.0). For improved security, you can restrict the coordinator to listen only on specific interfaces.

```c
// API
void dist_coordinator_set_bind_address(dist_coordinator_t *coordinator,
                                       const char *address);
```

Examples:
- `"127.0.0.1"` - Accept only local connections
- `"192.168.1.100"` - Accept only connections on specific LAN interface
- `NULL` or `""` - Accept connections on all interfaces (default)

### Rate Limiting

The distributed coordinator includes rate limiting to prevent denial-of-service attacks. Rate limiting tracks:

1. **Connection rate**: Maximum new connections per IP address per time window
2. **Message rate**: Maximum messages per connection per time window

Default limits:
- 10 connections per IP per 60 seconds
- 100 messages per connection per 60 seconds

```c
// API
void dist_coordinator_enable_rate_limiting(dist_coordinator_t *coordinator,
                                           int max_connections,
                                           int max_messages,
                                           int window_sec);
```

Example configuration:
```c
// Limit to 5 connections and 50 messages per 30-second window
dist_coordinator_enable_rate_limiting(&coordinator, 5, 50, 30);
```

When rate limiting triggers:
- Excess connections are silently closed
- This prevents resource exhaustion attacks
- Legitimate workers should not be affected under normal operation

## Authentication

Worker authentication uses a shared token system. When authentication is enabled, workers must provide the correct token during registration.

```c
// API
void dist_coordinator_set_auth_token(dist_coordinator_t *coordinator,
                                     const char *token);
```

Security features:
- **Constant-time comparison**: Token validation uses timing-safe comparison to prevent timing attacks
- **Token not logged**: Tokens are never printed to logs
- **Max length**: Tokens are limited to 64 characters

Example usage:
```c
// Coordinator
dist_coordinator_set_auth_token(&coordinator, "your-secret-token-here");

// Worker
strncpy(worker_client.auth_token, "your-secret-token-here",
        sizeof(worker_client.auth_token) - 1);
```

Best practices:
- Use strong, randomly generated tokens (minimum 32 characters)
- Do not hardcode tokens in source code
- Use environment variables or configuration files with restricted permissions

## TLS Encryption

The distributed coordinator supports optional TLS encryption for network communication. This is recommended when operating over untrusted networks.

```c
// API
int dist_coordinator_enable_tls(dist_coordinator_t *coordinator,
                                const char *cert_file,
                                const char *key_file);
```

Requirements:
- OpenSSL libraries available at compile time
- Build with: `make ENABLE_TLS=1`
- Valid PEM certificate and private key files

Generating self-signed certificates (for testing):
```bash
# Generate private key
openssl genrsa -out server.key 2048

# Generate self-signed certificate (valid for 365 days)
openssl req -new -x509 -key server.key -out server.crt -days 365 \
    -subj "/CN=keyhunt-coordinator"
```

Note: TLS support is optional and requires OpenSSL. If OpenSSL is not available, the function returns an error.

## Input Validation

### JSON Message Sanitization

All incoming JSON messages are sanitized before parsing:
- Control characters (except whitespace) are removed
- Message length is validated against maximum size (8KB)
- Invalid UTF-8 sequences are filtered

### Path Validation

Executable paths are validated to prevent command injection:
- Shell metacharacters are rejected: `; | & $ \` ( ) { } < > ! * ? [ ] " ' \ \n \r \t`
- Paths are validated before use with fork/exec
- system() is not used for subprocess execution

## Thread Safety

The following measures ensure thread safety in the codebase:

### Atomic Operations

Critical shared variables use atomic operations:
- `FINISHED_ITEMS` - Uses `std::atomic<uint64_t>` with relaxed memory ordering
- Signal handler flags - Uses `volatile sig_atomic_t`

### Thread-Local Random Number Generation

A thread-safe random number generator replaces `rand()`:
```c
static thread_local unsigned int g_thread_rand_state;
int thread_rand(void);      // Thread-safe replacement for rand()
int thread_rand_n(int n);   // Thread-safe random in range [0, n)
```

This prevents data races in multi-threaded key generation.

### Mutex Protection

Critical sections are protected with mutexes:
- Work unit assignment
- Worker state updates
- Rate limiter table access

## Security Recommendations

### Production Deployment

1. **Network isolation**: Run the coordinator on an internal network or VPN
2. **Firewall rules**: Restrict port access to known worker IPs
3. **TLS encryption**: Enable TLS for any network traversing untrusted segments
4. **Authentication**: Always enable authentication tokens
5. **Rate limiting**: Enable rate limiting with conservative limits
6. **Bind address**: Bind to specific interface, not 0.0.0.0
7. **Monitoring**: Monitor connection patterns for anomalies

### Token Management

```bash
# Generate a strong random token
openssl rand -hex 32

# Store in environment variable
export KEYHUNT_AUTH_TOKEN=$(cat /path/to/token/file)
```

### Example Secure Configuration

```c
dist_coordinator_t coordinator;
dist_coordinator_init(&coordinator, 8333);

// Bind only to VPN interface
dist_coordinator_set_bind_address(&coordinator, "10.0.0.1");

// Enable authentication
dist_coordinator_set_auth_token(&coordinator, getenv("KEYHUNT_AUTH_TOKEN"));

// Enable rate limiting
dist_coordinator_enable_rate_limiting(&coordinator, 10, 100, 60);

// Enable TLS (if compiled with OpenSSL)
dist_coordinator_enable_tls(&coordinator, "server.crt", "server.key");
```

## Reporting Vulnerabilities

If you discover a security vulnerability in keyhunt:

1. **Do not** open a public GitHub issue
2. Contact the maintainers privately
3. Provide detailed information:
   - Steps to reproduce
   - Affected versions
   - Potential impact

Response timeline:
- Initial acknowledgment: 48 hours
- Status update: 7 days
- Security patch: Based on severity

## Security Changelog

### Version X.X.X (Current)

- Added rate limiting for DoS protection
- Added configurable bind address
- Added optional TLS support
- Added JSON input sanitization
- Replaced system() with fork/exec for subprocess execution
- Fixed path validation for command injection prevention
- Made FINISHED_ITEMS atomic for thread safety
- Replaced rand() with thread-safe random number generation
- Fixed exit() calls in library code to return errors instead
