#include "logging.h"
#include "packet_processor.h"
#include "multicast_logger.h"
#include <iostream>
#include <chrono>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

// Performance monitoring constants
const int STATS_INTERVAL = 1000000; // Report stats every 1 million packets

/**
 * Main program loop with spdlog integration and idle timeout statistics
 */
int main() {
    // Initialize logging system
    init_logging();

    // Log startup information
    g_console_logger->info("Enhanced CBOE PITCH Multicast Packet Logger with Message Sequencing");
    g_console_logger->info("Listening on multicast {} ports {} and {}", MULTICAST_IP, PORT1, PORT2);
    g_console_logger->info("Log rotation: {}MB per file, {} files maximum", LOG_FILE_SIZE / (1024*1024), LOG_FILE_COUNT);
    g_console_logger->info("HEARTBEAT FILTERING: {}", SKIP_HEARTBEATS ? "ENABLED (heartbeats will be skipped)" : "DISABLED");
    g_console_logger->info("IDLE TIMEOUT: Will print statistics after {} seconds of no activity", IDLE_TIMEOUT_MS / 1000);

    // Log startup to file as well
    g_logger->info("[Startup] Enhanced CBOE PITCH Multicast Packet Logger with Message Sequencing");
    g_logger->info("[Startup] Listening on multicast {} ports {} and {}", MULTICAST_IP, PORT1, PORT2);

    int sock1 = create_multicast_socket(PORT1);
    int sock2 = create_multicast_socket(PORT2);

    struct pollfd fds[2];
    fds[0].fd = sock1;
    fds[0].events = POLLIN;
    fds[1].fd = sock2;
    fds[1].events = POLLIN;

    int packet_id = 0;
    char buffer[MAX_BUF];
    char control_buffer[1024];
    int heartbeats_skipped = 0;

    // Performance monitoring
    auto start_time = std::chrono::high_resolution_clock::now();
    auto last_packet_time = std::chrono::high_resolution_clock::now();
    int packets_logged = 0;
    bool idle_stats_printed = false;

    g_console_logger->info("Starting packet capture...");

    while (true) {
        // Use the idle timeout for poll
        int ready = poll(fds, 2, IDLE_TIMEOUT_MS);

        // Check for timeout (no packets received)
        if (ready == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            auto idle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_packet_time);

            if (idle_duration.count() >= IDLE_TIMEOUT_MS && !idle_stats_printed) {
                g_console_logger->info("No packets received for {} seconds. Printing statistics...", IDLE_TIMEOUT_MS / 1000);
                g_stats.print_summary();
                idle_stats_printed = true;
            }
            continue;
        }

        if (ready < 0) {
            if (errno == EINTR) continue; // Interrupted by signal, continue
            g_console_logger->error("Poll error: {}", strerror(errno));
            break;
        }

        for (int i = 0; i < 2; ++i) {
            if (fds[i].revents & POLLIN) {
                sockaddr_in sender_addr{};
                socklen_t addr_len = sizeof(sender_addr);

                struct msghdr msg{};
                struct iovec iov{};

                iov.iov_base = buffer;
                iov.iov_len = MAX_BUF;

                msg.msg_name = &sender_addr;
                msg.msg_namelen = addr_len;
                msg.msg_iov = &iov;
                msg.msg_iovlen = 1;
                msg.msg_control = control_buffer;
                msg.msg_controllen = sizeof(control_buffer);

                ssize_t len = recvmsg(fds[i].fd, &msg, 0);
                if (len > 0) {
                    // Update last packet time and reset idle flag
                    last_packet_time = std::chrono::high_resolution_clock::now();
                    idle_stats_printed = false;

                    packet_id++;
                    packets_logged++;
                    int port = (fds[i].fd == sock1) ? PORT1 : PORT2;

                    std::string src_ip = inet_ntoa(sender_addr.sin_addr);
                    std::string dst_ip = MULTICAST_IP;

                    struct cmsghdr *cmsg;
                    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                        if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_PKTINFO) {
                            struct in_pktinfo *pkt_info = reinterpret_cast<struct in_pktinfo *>(CMSG_DATA(cmsg));
                            dst_ip = inet_ntoa(pkt_info->ipi_addr);
                            break;
                        }
                    }

                    log_packet(packet_id, port, buffer, static_cast<int>(len), src_ip, dst_ip, heartbeats_skipped);

                    // Performance reporting
                    if (packets_logged % STATS_INTERVAL == 0) {
                        auto now = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
                        double pps = static_cast<double>(packets_logged) / (duration.count() / 1000.0);

                        if (SKIP_HEARTBEATS && heartbeats_skipped > 0) {
                            g_console_logger->info("Performance: {} packets logged, {:.1f} packets/sec, {} heartbeats skipped",
                                                 packets_logged, pps, heartbeats_skipped);
                        } else {
                            g_console_logger->info("Performance: {} packets logged, {:.1f} packets/sec",
                                                 packets_logged, pps);
                        }

                        // Print current statistics
                        g_stats.print_summary();

                        // Force flush periodically to ensure data is written
                        g_logger->flush();
                    }
                }
            }
        }
    }

    // Print final statistics before exit
    g_console_logger->info("Program terminating. Final statistics:");
    g_stats.print_summary();

    g_logger->info("[Complete] Program terminated");
    g_console_logger->info("Program terminated");

    // Cleanup
    spdlog::shutdown();
    close(sock1);
    close(sock2);
    return 0;
}
