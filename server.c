#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

#define PKT_DATA 0
#define PKT_ACK 3
#define PKT_SYN 1      // New packet type for SYN
#define PKT_SYN_ACK 2  // New packet type for SYN-ACK
#define PKT_RST 4      // Packet type for ending connection

#define MAX_SEQ 255    // Maximum sequence number
#define TIMEOUT 2      // Timeout duration in seconds

typedef struct {
  int seq;      /* Sequence number */
  int ack;      /* Acknowledgement number */
  int flag;     /* Control flag */
  char payload; /* Data payload (1 character for this project) */
} Packet;

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <listen_port>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  int sockfd;
  struct sockaddr_in server_addr, client_addr;
  socklen_t addrlen = sizeof(client_addr);
  int listening_port = atoi(argv[1]);

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(listening_port);

  if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("Bind failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  printf("Server initialized and listening on port %d\n", listening_port);

  Packet pkt;
  int window_size = 0, byte_request = 0, num_packets = 0;
  int initial_window_size = window_size;

  // Step 1: Wait for SYN and respond with SYN-ACK
  recvfrom(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&client_addr, &addrlen);
  if (pkt.flag == PKT_SYN) {
    printf("Server: Received SYN packet\n");

    pkt.flag = PKT_SYN_ACK;
    sendto(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&client_addr, addrlen);
    printf("Server: Sent SYN-ACK packet\n");

    // Step 2: Wait for ACK to complete handshake
    recvfrom(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&client_addr, &addrlen);
    if (pkt.flag == PKT_ACK) {
      printf("Server: Received ACK packet, handshake complete\n\n");

      // Step 3: Receive initial parameters for window size and byte request
      recvfrom(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&client_addr, &addrlen);
      window_size = pkt.payload;  // Initial window size
      initial_window_size = window_size; // Save the initial window size
      printf("Server: Received window size (N) = %d\n", window_size);

      recvfrom(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&client_addr, &addrlen);
      byte_request = pkt.payload;
      num_packets = byte_request;  // Number of packets to send
      printf("Server: Received byte request (S) = %d\n\n", byte_request);

      // Start Go-back-N transmission
      int base = 1;  // First unacknowledged packet
      int next_seq = 1;  // Next packet to send
      int successful_windows = 0;  // Count of successful transmissions without loss
      fd_set read_fds;
      struct timeval tv;

      while (base <= num_packets) {
        // Send packets in the current window
        while (next_seq < base + window_size && next_seq <= num_packets) {
          Packet data_pkt;
          data_pkt.seq = next_seq;
          data_pkt.ack = PKT_ACK;  // Not used in data packets
          data_pkt.flag = PKT_ACK;
          data_pkt.payload = 'A' + (next_seq % 26);  // Dummy payload (A-Z)

          sendto(sockfd, &data_pkt, sizeof(data_pkt), 0, (struct sockaddr *)&client_addr, addrlen);
          printf("Current Window = %d\n", window_size);
          printf("Server: Sent packet with seq=%d\n\n", data_pkt.seq);
          next_seq++;
        }

        // Set up select() for timeout handling
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        tv.tv_sec = TIMEOUT;
        tv.tv_usec = 0;

        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(sockfd, &read_fds)) {
          Packet ack_pkt;
          recvfrom(sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client_addr, &addrlen);

          if (ack_pkt.flag == PKT_ACK) {
            printf("Server: Received ACK for seq=%d\n\n", ack_pkt.ack);

            if (ack_pkt.ack == base) {
              base++;  // Slide window forward
              successful_windows++;

              // Check if we can increase the window size back to the initial value
              if (successful_windows == 2 && window_size < initial_window_size) {  // Restore to initial size (example)
                window_size = initial_window_size;
                successful_windows = 0;  // Reset counter
              }
            }
          }
        } else {
          // Timeout occurred, reset window
          printf("Server: Timeout occurred, resending from base seq=%d\n", base);
          next_seq = base;  // Retransmit from the base sequence number
          if (window_size > 1) {
            window_size /= 2;  // Halve the window size on timeout
          }
          successful_windows = 0;  // Reset successful windows counter
        }
      }

      // End transmission with RST packet
      Packet rst_pkt;
      rst_pkt.flag = PKT_RST;
      rst_pkt.ack = PKT_ACK;
      rst_pkt.seq = next_seq;
      sendto(sockfd, &rst_pkt, sizeof(rst_pkt), 0, (struct sockaddr *)&client_addr, addrlen);
      printf("Server: Sent RST packet, ending transmission\n");
    }
  }

  close(sockfd);
  return 0;
}
