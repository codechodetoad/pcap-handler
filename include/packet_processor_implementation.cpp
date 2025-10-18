#include "packet_processor.h"
#include "logging.h"
#include "multicast_logger.h"
#include <endian.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>

// Global statistics instance
PacketStatistics g_stats;

// Global sequence trackers
std::map<std::pair<int, int>, SequenceTracker> g_sequence_trackers;

// CBOE PITCH message type mapping
static const MessageTypeInfo CBOE_MESSAGE_TYPES[] = {
    // PITCH Messages
    {0x97, "UNIT_CLEAR", "Unit Clear", 2},
    {0x3B, "TRADING_STATUS", "Trading Status", 8},
    {0x37, "ADD_ORDER", "Add Order", 34},
    {0x38, "ORDER_EXECUTED", "Order Executed", 30},
    {0x58, "ORDER_EXECUTED_AT_PRICE", "Order Executed at Price", 38},
    {0x39, "REDUCE_SIZE", "Reduce Size", 18},
    {0x3A, "MODIFY_ORDER", "Modify Order", 34},
    {0x3C, "DELETE_ORDER", "Delete Order", 18},
    {0x3D, "TRADE", "Trade", 42},
    {0x3E, "TRADE_BREAK", "Trade Break", 18},
    {0xE3, "CALCULATED_VALUE", "Calculated Value", 26},
    {0x2D, "END_OF_SESSION", "End of Session", 2},
    {0x59, "AUCTION_UPDATE", "Auction Update", 30},
    {0x5A, "AUCTION_SUMMARY", "Auction Summary", 30},

    // Gap Request Proxy Messages
    {0x01, "LOGIN", "Login", 44},
    {0x02, "LOGIN_RESPONSE", "Login Response", 3},
    {0x03, "GAP_REQUEST", "Gap Request", 20},
    {0x04, "GAP_RESPONSE", "Gap Response", 20},

    // Spin Server Messages
    {0x80, "SPIN_IMAGE_AVAILABLE", "Spin Image Available", 20},
    {0x81, "SPIN_REQUEST", "Spin Request", 20},
    {0x82, "SPIN_RESPONSE", "Spin Response", 20},
    {0x83, "SPIN_FINISHED", "Spin Finished", 20}
};

static const int NUM_MESSAGE_TYPES = sizeof(CBOE_MESSAGE_TYPES) / sizeof(MessageTypeInfo);

/**
 * PacketStatistics member functions
 */
void PacketStatistics::reset() {
    total_packets_received = 0;
    total_heartbeats = 0;
    duplicates = 0;
    out_of_order = 0;
    sequence_gaps = 0;
    in_order_packets = 0;
    unsequenced_packets = 0;
    admin_packets = 0;
}

void PacketStatistics::print_summary() {
    g_console_logger->info("=== PACKET STATISTICS SUMMARY ===");
    g_console_logger->info("Total packages received: {}", total_packets_received);
    g_console_logger->info("Total package heartbeat: {}", total_heartbeats);
    g_console_logger->info("In-order packets: {}", in_order_packets);
    g_console_logger->info("Duplicates: {}", duplicates);
    g_console_logger->info("Out-of-order: {}", out_of_order);
    g_console_logger->info("Sequence gaps: {}", sequence_gaps);
    g_console_logger->info("Unsequenced packets: {}", unsequenced_packets);
    g_console_logger->info("Admin packets: {}", admin_packets);
    g_console_logger->info("==================================");

    // Also log to file
    g_logger->info("[STATISTICS] Total: {}, Heartbeats: {}, In-order: {}, Duplicates: {}, Out-of-order: {}, Gaps: {}, Unsequenced: {}, Admin: {}",
                   total_packets_received, total_heartbeats, in_order_packets, duplicates, out_of_order, sequence_gaps, unsequenced_packets, admin_packets);
}

/**
 * Safe little-endian conversion functions
 */
uint16_t le16toh_safe(uint16_t val) {
    return le16toh(val);
}

uint32_t le32toh_safe(uint32_t val) {
    return le32toh(val);
}

uint64_t le64toh_safe(uint64_t val) {
    return le64toh(val);
}

/**
 * Lookup message type information
 */
const MessageTypeInfo* lookup_message_type(uint8_t type_id) {
    for (int i = 0; i < NUM_MESSAGE_TYPES; i++) {
        if (CBOE_MESSAGE_TYPES[i].type_id == type_id) {
            return &CBOE_MESSAGE_TYPES[i];
        }
    }
    return nullptr;
}

/**
 * Decode specific message types with detailed information
 */
std::string decode_message_details(const char* message_data, uint8_t length, uint8_t message_type) {
    std::string details = "";

    switch (message_type) {
        case 0x5A: { // Auction Summary Message
            if (length >= 30) {
                const AuctionSummaryMessage* msg = reinterpret_cast<const AuctionSummaryMessage*>(message_data);

                // Extract symbol (trim spaces)
                std::string symbol(msg->symbol, 6);
                size_t end = symbol.find_last_not_of(' ');
                if (end != std::string::npos) {
                    symbol = symbol.substr(0, end + 1);
                }

                // Decode auction type
                char auction_type_char = msg->auction_type;
                std::string auction_desc;
                switch (auction_type_char) {
                    case 'O': auction_desc = "Opening"; break;
                    case 'C': auction_desc = "Closing"; break;
                    case 'H': auction_desc = "Halt"; break;
                    default: auction_desc = "Unknown"; break;
                }

                uint64_t price = le64toh_safe(msg->price);
                uint32_t shares = le32toh_safe(msg->shares);

                std::ostringstream oss;
                oss << "Symbol=" << symbol << ", Auction=" << auction_desc
                    << ", Price=" << price << ", Shares=" << shares;
                details = oss.str();
            }
            break;
        }

        case 0x3B: { // Trading Status
            if (length >= 8) {
                // Extract symbol from offset 2 (6 bytes)
                std::string symbol(message_data + 2, 6);
                size_t end = symbol.find_last_not_of(' ');
                if (end != std::string::npos) {
                    symbol = symbol.substr(0, end + 1);
                }

                // Status byte is typically at offset 8
                if (length > 8) {
                    char status = message_data[8];
                    std::string status_desc;
                    switch (status) {
                        case 'H': status_desc = "Halted"; break;
                        case 'T': status_desc = "Trading"; break;
                        case 'Q': status_desc = "Quotation"; break;
                        case 'A': status_desc = "Auction"; break;
                        default:
                            status_desc = "Unknown(" + std::to_string((int)status) + ")";
                            break;
                    }
                    std::ostringstream oss;
                    oss << "Symbol=" << symbol << ", Status=" << status_desc;
                    details = oss.str();
                } else {
                    std::ostringstream oss;
                    oss << "Symbol=" << symbol;
                    details = oss.str();
                }
            }
            break;
        }

        case 0x37: { // Add Order
            if (length >= 34) {
                // Extract order ID (8 bytes at offset 2)
                uint64_t order_id = le64toh_safe(*reinterpret_cast<const uint64_t*>(message_data + 2));

                // Extract symbol (6 bytes at offset 18)
                std::string symbol(message_data + 18, 6);
                size_t end = symbol.find_last_not_of(' ');
                if (end != std::string::npos) {
                    symbol = symbol.substr(0, end + 1);
                }

                // Extract side (1 byte at offset 24)
                char side = message_data[24];
                std::string side_desc = (side == 'B') ? "Buy" : (side == 'S') ? "Sell" : "Unknown";

                // Extract shares (4 bytes at offset 25)
                uint32_t shares = le32toh_safe(*reinterpret_cast<const uint32_t*>(message_data + 25));

                std::ostringstream oss;
                oss << "OrderID=" << order_id << ", Symbol=" << symbol
                    << ", Side=" << side_desc << ", Shares=" << shares;
                details = oss.str();
            }
            break;
        }

        case 0x38: case 0x58: { // Order Executed / Order Executed at Price
            if (length >= 18) {
                // Extract order ID (8 bytes at offset 2)
                uint64_t order_id = le64toh_safe(*reinterpret_cast<const uint64_t*>(message_data + 2));

                // Extract executed shares (4 bytes at offset 10)
                uint32_t executed_shares = le32toh_safe(*reinterpret_cast<const uint32_t*>(message_data + 10));

                std::ostringstream oss;
                oss << "OrderID=" << order_id << ", Executed=" << executed_shares;
                details = oss.str();

                // For Order Executed at Price, add execution price
                if (message_type == 0x58 && length >= 26) {
                    uint64_t price = le64toh_safe(*reinterpret_cast<const uint64_t*>(message_data + 18));
                    details += ", Price=" + std::to_string(price);
                }
            }
            break;
        }

        case 0x3D: { // Trade
            if (length >= 42) {
                // Extract symbol (6 bytes at offset 2)
                std::string symbol(message_data + 2, 6);
                size_t end = symbol.find_last_not_of(' ');
                if (end != std::string::npos) {
                    symbol = symbol.substr(0, end + 1);
                }

                // Extract side (1 byte at offset 8)
                char side = message_data[8];
                std::string side_desc = (side == 'B') ? "Buy" : (side == 'S') ? "Sell" : "Unknown";

                // Extract shares (4 bytes at offset 9)
                uint32_t shares = le32toh_safe(*reinterpret_cast<const uint32_t*>(message_data + 9));

                // Extract price (8 bytes at offset 13)
                uint64_t price = le64toh_safe(*reinterpret_cast<const uint64_t*>(message_data + 13));

                std::ostringstream oss;
                oss << "Symbol=" << symbol << ", Side=" << side_desc
                    << ", Shares=" << shares << ", Price=" << price;
                details = oss.str();
            }
            break;
        }

        case 0x97: { // Unit Clear
            details = "UnitCleared";
            break;
        }

        case 0x2D: { // End of Session
            details = "SessionEnded";
            break;
        }

        default:
            // For unknown or unhandled message types, show raw hex
            if (length > 2) {
                std::ostringstream oss;
                oss << "RawData=";
                for (int i = 2; i < std::min((int)length, 10); i++) {
                    oss << std::hex << std::setfill('0') << std::setw(2)
                        << static_cast<unsigned int>(static_cast<unsigned char>(message_data[i]));
                }
                if (length > 10) oss << "...";
                details = oss.str();
            }
            break;
    }

    return details;
}

/**
 * Parse and log all messages within a packet - now returns vector of message details
 */
std::vector<std::string> parse_packet_messages(const char* buffer, int total_length, int header_length) {
    std::vector<std::string> messages;

    // Skip the unit header
    int offset = header_length;
    int message_count = 0;

    while (offset < total_length && message_count < 100) { // Safety limit
        if (offset + 2 > total_length) break; // Need at least 2 bytes for message header

        const CboeMessageHeader* msg_header = reinterpret_cast<const CboeMessageHeader*>(buffer + offset);
        uint8_t msg_length = msg_header->length;
        uint8_t msg_type = msg_header->message_type;

        if (msg_length == 0 || offset + msg_length > total_length) {
            break; // Invalid message length
        }

        // Look up message type information
        const MessageTypeInfo* type_info = lookup_message_type(msg_type);
        std::string type_name = type_info ? type_info->name : "UNKNOWN";

        // Decode message-specific details
        std::string details = decode_message_details(buffer + offset, msg_length, msg_type);

        // Format message information
        std::ostringstream msg_oss;
        msg_oss << "Type=0x" << std::hex
                << std::setfill('0') << std::setw(2) << static_cast<int>(msg_type)
                << " (" << type_name << "), Len=" << std::dec << static_cast<int>(msg_length);

        std::string msg_info = msg_oss.str();

        if (!details.empty()) {
            msg_info += ", " + details;
        }

        messages.push_back(msg_info);

        offset += msg_length;
        message_count++;
    }

    return messages;
}

/**
 * Creates and configures a multicast UDP socket
 */
int create_multicast_socket(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        g_console_logger->error("Failed to create socket: {}", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        g_console_logger->warn("Failed to set SO_REUSEADDR: {}", strerror(errno));
    }

    int rcvbuf = 16 * 1024 * 1024;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
        g_console_logger->warn("Failed to set SO_RCVBUF: {}", strerror(errno));
    }

    int pktinfo = 1;
    if (setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &pktinfo, sizeof(pktinfo)) < 0) {
        g_console_logger->warn("Failed to enable IP_PKTINFO: {}", strerror(errno));
    }

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr)) < 0) {
        g_console_logger->error("Failed to bind to port {}: {}", port, strerror(errno));
        exit(EXIT_FAILURE);
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_IP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        g_console_logger->error("Failed to join multicast group: {}", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return sock;
}

/**
 * Enhanced packet logging with proper out-of-order detection and statistics tracking
 */
void log_packet(int packet_id, int port, const char* buffer, int len,
                const std::string& src_ip, const std::string& dst_ip,
                int& heartbeats_skipped) {

    if (len < static_cast<int>(sizeof(CboeSequencedUnitHeader))) return;

    const CboeSequencedUnitHeader* header = reinterpret_cast<const CboeSequencedUnitHeader*>(buffer);

    uint32_t seq = le32toh_safe(header->hdr_sequence);
    uint16_t length = le16toh_safe(header->hdr_length);
    uint8_t count = header->hdr_count;
    uint8_t unit = header->hdr_unit;

    // Classify packet type
    std::string packet_type;
    if (seq == 0) {
        if (count == 0 && len <= 20) {
            packet_type = "HEARTBEAT";
            g_stats.total_heartbeats++;
        } else if (count == 0) {
            packet_type = "ADMIN";
            g_stats.admin_packets++;
        } else {
            packet_type = "UNSEQUENCED";
            g_stats.unsequenced_packets++;
        }
    } else {
        packet_type = "DATA";
    }

    // Count total packets received
    g_stats.total_packets_received++;

    // Skip heartbeat packets if configured to do so
    if (SKIP_HEARTBEATS && packet_type == "HEARTBEAT") {
        heartbeats_skipped++;
        return;
    }

    // Analyze sequence ordering with new logic
    std::string order_status = "UNSEQUENCED";
    if (seq > 0) {
        auto key = std::make_pair(port, static_cast<int>(unit));
        auto& tracker = g_sequence_trackers[key];

        // Thread-safe access to tracker
        std::lock_guard<std::mutex> lock(tracker.tracker_mutex);

        // Determine actual message count for sequence tracking
        // Use header count, but default to 1 if count is 0
        uint32_t message_count = (count > 0) ? static_cast<uint32_t>(count) : 1;

        // First packet for this unit
        if (tracker.last_confirmed_seq == 0 && tracker.highest_seen_seq == 0) {
            tracker.last_confirmed_seq = seq + message_count - 1;
            tracker.highest_seen_seq = seq + message_count - 1;
            order_status = "SEQUENCED-FIRST";
            g_stats.in_order_packets++;
        } else {
            uint32_t expected = tracker.last_confirmed_seq + 1;

            // Exactly what we expected
            if (seq == expected) {
                // Update confirmed sequence (accounting for multi-message packets)
                tracker.last_confirmed_seq = seq + message_count - 1;

                // Check if we can now confirm any pending sequences
                while (true) {
                    auto next_expected = tracker.last_confirmed_seq + 1;
                    auto it = tracker.pending_sequences.find(next_expected);
                    if (it != tracker.pending_sequences.end()) {
                        // Find how many consecutive sequences we can confirm
                        uint32_t confirmed_end = next_expected;
                        while (tracker.pending_sequences.find(confirmed_end + 1) != tracker.pending_sequences.end()) {
                            confirmed_end++;
                        }

                        // Update last confirmed and remove from pending
                        tracker.last_confirmed_seq = confirmed_end;
                        for (uint32_t s = next_expected; s <= confirmed_end; s++) {
                            tracker.pending_sequences.erase(s);
                        }
                    } else {
                        break;
                    }
                }

                order_status = "SEQUENCED-IN-ORDER";
                g_stats.in_order_packets++;
            }
            // Earlier than expected - definitely out of order (late arrival)
            else if (seq < expected) {
                // Check if it's a duplicate
                if (seq <= tracker.last_confirmed_seq) {
                    order_status = "SEQUENCED-DUPLICATE";
                    g_stats.duplicates++;
                } else {
                    order_status = "SEQUENCED-OUT-OF-ORDER-LATE";
                    g_stats.out_of_order++;
                }
            }
            // Later than expected - early arrival
            else if (seq > expected) {
                // Mark all sequences in this packet as pending
                for (uint32_t i = 0; i < message_count; i++) {
                    tracker.pending_sequences[seq + i] = true;
                }
                tracker.highest_seen_seq = std::max(tracker.highest_seen_seq, seq + message_count - 1);

                // Calculate gap size for logging
                uint32_t gap_size = seq - expected;
                order_status = "SEQUENCED-OUT-OF-ORDER-EARLY[gap=" + std::to_string(gap_size) + "]";
                g_stats.out_of_order++;
                g_stats.sequence_gaps += gap_size;
            }
        }
    }

    // Parse messages within the packet
    std::vector<std::string> messages = parse_packet_messages(buffer, len, sizeof(CboeSequencedUnitHeader));

    // Validate count matches parsed messages
    if (count > 0 && messages.size() != static_cast<size_t>(count)) {
        g_logger->warn("Count mismatch in packet {}: header says {} messages, parsed {}",
                      packet_id, static_cast<int>(count), messages.size());
    }

    // Log packets with multiple messages
    if (count > 1 && seq > 0) {
        // Log the first message with the original sequence
        if (!messages.empty()) {
            g_logger->info("Packet {} | Port: {} | Src: {} | Dst: {} | Seq: {} | Len: {} | Count: {} | Unit: {} | Type: {} | {} | Msg1/{}: {}",
                          packet_id, port, src_ip, dst_ip, seq, length, static_cast<int>(count), static_cast<int>(unit),
                          packet_type, order_status, messages.size(), messages[0]);
        }

        // Log additional messages with incremented sequences
        for (size_t i = 1; i < messages.size(); i++) {
            uint32_t msg_seq = seq + static_cast<uint32_t>(i);

            // Determine order status for individual messages within the packet
            std::string msg_order_status = order_status;
            if (order_status == "SEQUENCED-IN-ORDER") {
                // All messages in an in-order packet are in-order
                msg_order_status = "SEQUENCED-IN-ORDER";
            }

            g_logger->info("Packet {} | Port: {} | Src: {} | Dst: {} | Seq: {} | Len: {} | Count: {} | Unit: {} | Type: {} | {} | Msg{}/{}: {}",
                          packet_id, port, src_ip, dst_ip, msg_seq, length, static_cast<int>(count), static_cast<int>(unit),
                          packet_type, msg_order_status, i + 1, messages.size(), messages[i]);
        }
    } else {
        // Original behavior for single message packets or count <= 1
        if (messages.empty()) {
            g_logger->info("Packet {} | Port: {} | Src: {} | Dst: {} | Seq: {} | Len: {} | Count: {} | Unit: {} | Type: {} | {}",
                          packet_id, port, src_ip, dst_ip, seq, length, static_cast<int>(count), static_cast<int>(unit), packet_type, order_status);
        } else {
            std::string message_info = "";
            for (size_t i = 0; i < messages.size(); i++) {
                if (i > 0) message_info += "; ";
                message_info += "Msg" + std::to_string(i + 1) + ": " + messages[i];
            }

            g_logger->info("Packet {} | Port: {} | Src: {} | Dst: {} | Seq: {} | Len: {} | Count: {} | Unit: {} | Type: {} | {} | Messages: [{}]",
                          packet_id, port, src_ip, dst_ip, seq, length, static_cast<int>(count), static_cast<int>(unit), packet_type, order_status, message_info);
        }
    }
}
