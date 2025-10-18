# PCAP Handler - CBOE PITCH Multicast Packet Logger

A high-performance C++ network packet capture and analysis tool designed to monitor, log, and analyze CBOE PITCH protocol multicast traffic with comprehensive message decoding and sequencing capabilities.

## Project Overview

This application is a specialized network monitoring tool that captures and processes real-time market data from CBOE (Chicago Board Options Exchange) PITCH protocol multicast feeds. It provides detailed packet inspection, message decoding, sequence tracking, and performance monitoring capabilities suitable for financial market data analysis and debugging.

### Key Features

- **Real-time Multicast Packet Capture**: Monitors multiple UDP multicast ports simultaneously
- **CBOE PITCH Protocol Support**: Full decoding of CBOE PITCH message types including:
  - Trading messages (Add Order, Order Executed, Trade, etc.)
  - Administrative messages (Trading Status, Unit Clear, End of Session)
  - Auction messages (Auction Update, Auction Summary)
  - Gap Request and Spin Server messages
- **Advanced Sequence Tracking**: Detects and reports:
  - In-order packets
  - Out-of-order packets
  - Duplicate packets
  - Sequence gaps
  - Heartbeat messages
- **High-Performance Logging**: Asynchronous logging with spdlog featuring:
  - Rotating file logs (200MB per file, 40 file rotation)
  - Console output with color coding
  - Large async queue for high throughput (32K queue size)
- **Performance Monitoring**: Real-time statistics including packets per second and processing metrics
- **Idle Timeout Detection**: Automatic statistics reporting after periods of inactivity

## Architecture

### Directory Structure

```
pcap-handler/
├── include/           # Header files
│   ├── logging.h              # Logging system configuration
│   ├── multicast_logger.h     # Multicast definitions and data structures
│   └── packet_processor.h     # Packet processing functions and statistics
├── src/               # Source files
│   ├── init_logging.cpp       # Logging initialization
│   ├── main.cpp               # Main event loop and socket handling
│   └── packet_processor_implementation.cpp  # Message parsing and decoding
├── bin/               # Build output directory
├── Makefile           # Build configuration
└── README.md          # This file
```

### Core Components

#### 1. Packet Processor ([packet_processor.h](include/packet_processor.h))
- **PacketStatistics**: Global statistics tracking for all packet types
- **Message Type Lookup**: Maps CBOE message type IDs to human-readable names
- **Message Decoding**: Parses binary message data into structured information
- **Endian Conversion**: Safe little-endian to host byte order conversion
- **Sequence Tracking**: Per-port/unit sequence number validation

#### 2. Multicast Logger ([multicast_logger.h](include/multicast_logger.h))
- **CBOE Protocol Structures**:
  - `CboeSequencedUnitHeader`: Packet-level header with sequence numbers
  - `CboeMessageHeader`: Message-level header for multi-message packets
  - `AuctionSummaryMessage`: Example decoded message structure
- **Message Type Definitions**: Complete mapping of CBOE PITCH message types
- **Sequence Tracker**: Thread-safe sequence validation with gap detection

#### 3. Logging System ([logging.h](include/logging.h))
- Dual logger setup: file and console
- Asynchronous logging for minimal performance impact
- Configurable rotation and buffer sizes
- Thread-safe operation

#### 4. Main Loop ([main.cpp](src/main.cpp))
- Poll-based socket monitoring
- Multi-port packet capture
- Idle timeout handling
- Performance metric reporting
- Graceful shutdown with final statistics

## Configuration

### Network Settings
Defined in [multicast_logger.h](include/multicast_logger.h):
```cpp
#define MULTICAST_IP "233.218.133.80"  // CBOE multicast group
#define PORT1 30501                     // Primary port
#define PORT2 30502                     // Secondary port
```

### Logging Settings
Defined in [logging.h](include/logging.h):
```cpp
#define LOG_FILE_SIZE (200 * 1024 * 1024)  // 200MB per file
#define LOG_FILE_COUNT 40                   // 40 rotating files
#define ASYNC_QUEUE_SIZE 32768              // 32K async queue
```

### Behavioral Settings
Defined in [multicast_logger.h](include/multicast_logger.h):
```cpp
#define SKIP_HEARTBEATS true           // Filter heartbeat messages
#define IDLE_TIMEOUT_MS 10000          // 10 second idle timeout
```

## Building the Project

### Prerequisites

- **Compiler**: g++ with C++17 support
- **Required Libraries**:
  - spdlog: Fast C++ logging library
  - fmt: Formatting library (dependency of spdlog)
  - pthread: POSIX threads

### Installation on Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install g++ make libspdlog-dev libfmt-dev
```

### Build Commands

```bash
# Build the project
make

# Clean build artifacts
make clean

# Build and run
make run
```

The compiled binary will be created at `bin/pcap-handler`.

## Usage

### Running the Application

```bash
# Build and run
make run

# Or run directly
./bin/pcap-handler
```

### Output

The application produces two types of output:

1. **Console Output**: Real-time packet information with color-coded severity levels
   - Startup information
   - Performance statistics (every 1M packets)
   - Idle timeout notifications
   - Final statistics on shutdown

2. **Log Files**: Rotating log files with detailed packet information
   - Location: `logs/` directory (automatically created)
   - Format: `multicast_packets_YYYYMMDD_HHMMSS.log`
   - Includes: Packet headers, message details, sequence analysis

### Sample Output

```
[info] Enhanced CBOE PITCH Multicast Packet Logger with Message Sequencing
[info] Listening on multicast 233.218.133.80 ports 30501 and 30502
[info] HEARTBEAT FILTERING: ENABLED (heartbeats will be skipped)
[info] Starting packet capture...

=== PACKET STATISTICS SUMMARY ===
Total packages received: 1000000
Total package heartbeat: 500000
In-order packets: 999500
Duplicates: 200
Out-of-order: 150
Sequence gaps: 150
Unsequenced packets: 0
Admin packets: 50
==================================
```

## Packet Statistics Explained

- **Total packages received**: All packets captured from both ports
- **Total package heartbeat**: Heartbeat messages (sequence 0)
- **In-order packets**: Packets received in expected sequence order
- **Duplicates**: Packets with previously seen sequence numbers
- **Out-of-order**: Packets received before earlier sequence numbers
- **Sequence gaps**: Missing sequence numbers detected
- **Unsequenced packets**: Packets with sequence 0 (non-heartbeat)
- **Admin packets**: Administrative/control messages

## Message Types Supported

### Trading Messages
- **Add Order** (0x37): New order added to book
- **Order Executed** (0x38): Order execution
- **Order Executed at Price** (0x58): Execution with price-time priority
- **Reduce Size** (0x39): Order size reduction
- **Modify Order** (0x3A): Order modification
- **Delete Order** (0x3C): Order cancellation
- **Trade** (0x3D): Trade execution report
- **Trade Break** (0x3E): Trade cancellation

### Administrative Messages
- **Unit Clear** (0x97): Clear all orders for unit
- **Trading Status** (0x3B): Symbol trading status update
- **End of Session** (0x2D): Session termination
- **Calculated Value** (0xE3): Calculated pricing

### Auction Messages
- **Auction Update** (0x59): Auction information update
- **Auction Summary** (0x5A): Auction results

### Gap Request Messages
- **Login** (0x01), **Login Response** (0x02)
- **Gap Request** (0x03), **Gap Response** (0x04)

### Spin Server Messages
- **Spin Image Available** (0x80)
- **Spin Request** (0x81)
- **Spin Response** (0x82)
- **Spin Finished** (0x83)

## Technical Details

### Threading Model
- Main thread: Packet capture and processing
- Async thread pool (spdlog): Log file I/O

### Memory Management
- Stack-based buffers for packet capture (2KB max)
- Smart pointers for logger instances
- STL containers for sequence tracking

### Error Handling
- Socket errors logged with errno details
- Malformed packet detection and reporting
- Graceful handling of signal interrupts

### Performance Considerations
- Asynchronous logging minimizes I/O blocking
- Poll-based socket monitoring for efficiency
- Optional heartbeat filtering reduces logging overhead
- Pre-sized buffers and containers

## Security Considerations

This tool is designed for **defensive security and monitoring purposes only**:
- Network traffic analysis
- Market data integrity verification
- Protocol compliance monitoring
- Performance diagnostics

**Not intended for**:
- Malicious interception of data
- Unauthorized network monitoring
- Trading system exploitation

Ensure proper authorization before monitoring network traffic.

## Troubleshooting

### Common Issues

**"Permission denied" when running**
- UDP multicast typically requires elevated privileges
- Solution: Run with `sudo ./bin/pcap-handler` or configure capabilities

**"Cannot allocate memory" errors**
- Async queue size may be too large
- Solution: Reduce `ASYNC_QUEUE_SIZE` in [logging.h](include/logging.h)

**No packets received**
- Verify multicast group membership
- Check firewall rules
- Ensure network interface is up

**Log files not created**
- Verify write permissions in current directory
- Check disk space availability

## Future Enhancements

Potential improvements for consideration:
- [ ] PCAP file output support
- [ ] Real-time statistics dashboard
- [ ] Configurable filtering rules
- [ ] Gap request/replay functionality
- [ ] Prometheus metrics export
- [ ] Multi-threaded packet processing
- [ ] Protocol specification compliance testing

## License

This project is provided as-is for educational and professional use in network monitoring and market data analysis.

## Contributing

When contributing to this project:
1. Ensure code follows existing style conventions
2. Add appropriate error handling
3. Update documentation for new features
4. Test with real multicast data streams
5. Verify performance impact of changes

## Support

For issues, questions, or contributions, please refer to the project repository or contact the development team.

---

**Version**: 1.0
**Last Updated**: 2025
**Language**: C++17
**Platform**: Linux
