#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <netinet/ether.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>

#define BUF_SIZ		1024

typedef struct __attribute__((packed))
{
    __be32  seq;
    __u8    channel;
    __u8    enable;
    __be16  value;
    __be16  scale;
    __be16  offset;
    __be16  freq;
} pwm_packet_t;

void print_usage()
{
    printf("Send PWM command over ethernet.\n\n"
           "Supported arguments (all are required):\n"
           "--iface=IFACE      - interface to send packet to\n"
           "--dst=MAC          - Destination MAC address\n"
           "--enable=yes|no    - 'yes' to enable PWM, 'no' to disable PWM\n"
           "--freq=NUMBER      - PWM frequency\n"
           "--value=NUMBER     - PWM value\n"
           "--scale=NUMBER     - Scale for PWM\n"
           "--offset=NUMBER    - Offset for PWM\n");
}

int main(int argc, char* argv[])
{
    int sockfd;
    struct ifreq if_idx;
    struct ifreq if_mac;
    int tx_len = 0;
    char sendbuf[BUF_SIZ];
    struct ether_header *eh = (struct ether_header *) sendbuf;
    pwm_packet_t *pwm = (pwm_packet_t *) (sendbuf + sizeof(struct ether_header));
    pwm_packet_t pwm_data = {0};
    struct sockaddr_ll socket_address;
    char ifName[IFNAMSIZ];
    unsigned char dst_mac[6] = {0};
    int c, option_index;
    struct option long_options[] = {
            {"iface",   required_argument,  0, 'i'},
            {"dst",     required_argument,  0, 'd'},
            {"enable",  required_argument,  0, 'e'},
            {"freq",    required_argument,  0, 'f'},
            {"value",   required_argument,  0, 'v'},
            {"scale",   required_argument,  0, 's'},
            {"offset",  required_argument,  0, 'o'},
            {0,         0,                  0, 0}
    };
    int applied_options[sizeof(long_options) / sizeof(struct option)] = {0};

    while((c = getopt_long(argc, argv, "i:d:e:f:v:s:o:", long_options, &option_index)) != -1)
    {
        switch (c)
        {
            case 'i':
                if(strlen(optarg) > IFNAMSIZ - 1)
                {
                    printf("Interface name is too long\n");
                    exit(EXIT_FAILURE);
                }
                strcpy(ifName, optarg);
                applied_options[option_index] = 1;
                break;
            case 'd':
                if(sscanf(optarg, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx%*c", &dst_mac[0],
                          &dst_mac[1], &dst_mac[2], &dst_mac[3], &dst_mac[4], &dst_mac[5]) != 6)
                {
                    printf("Incorrect destination address\n");
                    exit(EXIT_FAILURE);
                }
                applied_options[option_index] = 1;
                break;
            case 'e':
                if(!strcmp(optarg, "yes"))
                {
                    pwm_data.enable = 1;
                } else if(!strcmp(optarg, "no"))
                {
                    pwm_data.enable = 0;
                } else
                {
                    printf("enable argument can be yes of no\n");
                    exit(EXIT_FAILURE);
                }
                applied_options[option_index] = 1;
                break;
            case 'f':
                errno = 0;
                pwm_data.freq = htons((uint16_t) strtol(optarg, NULL, 10));
                if(errno != 0 && pwm_data.freq == 0)
                {
                    printf("frequency value is incorrect\n");
                    exit(EXIT_FAILURE);
                }
                applied_options[option_index] = 1;
                break;
            case 'v':
                errno = 0;
                pwm_data.value = htons((uint16_t) strtol(optarg, NULL, 10));
                if(errno != 0 && pwm_data.value == 0)
                {
                    printf("value format is incorrect\n");
                    exit(EXIT_FAILURE);
                }
                applied_options[option_index] = 1;
                break;
            case 's':
                errno = 0;
                pwm_data.scale = htons((uint16_t) strtol(optarg, NULL, 10));
                if(errno != 0 && pwm_data.scale == 0)
                {
                    printf("scale value is incorrect\n");
                    exit(EXIT_FAILURE);
                }
                applied_options[option_index] = 1;
                break;
            case 'o':
                errno = 0;
                pwm_data.offset = htons((uint16_t) strtol(optarg, NULL, 10));
                if(errno != 0 && pwm_data.offset == 0)
                {
                    printf("offset value is incorrect\n");
                    exit(EXIT_FAILURE);
                }
                applied_options[option_index] = 1;
                break;
            case '?':
                print_usage();
                exit(EXIT_SUCCESS);
            default:
                printf("?? getopt returned character code 0%o ??\n", c);
                exit(EXIT_FAILURE);
        }
    }

    option_index = 0;
    for(c = 0; long_options[c].name != NULL; c++)
    {
        if(!applied_options[c])
        {
            option_index = 1;
            printf("ERROR: Argument '%s' is required\n", long_options[c].name);
        }
    }
    if(option_index)
    {
        printf("\n\n");
        print_usage();
        exit(EXIT_FAILURE);
    }

    /* Open RAW socket to send on */
    if ((sockfd = socket(AF_PACKET, SOCK_RAW, IPPROTO_RAW)) == -1)
    {
        perror("socket");
        return -1;
    }

    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_idx.ifr_name, ifName, IFNAMSIZ-1);
    if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) < 0)
    {
        perror("SIOCGIFINDEX");
        return -1;
    }

    /* Get the MAC address of the interface to send on */
    memset(&if_mac, 0, sizeof(struct ifreq));
    strncpy(if_mac.ifr_name, ifName, IFNAMSIZ-1);
    if (ioctl(sockfd, SIOCGIFHWADDR, &if_mac) < 0)
    {
        perror("SIOCGIFHWADDR");
        return -1;
    }

    memset(sendbuf, 0, BUF_SIZ);

    /* Ethernet header */
    eh->ether_shost[0] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[0];
    eh->ether_shost[1] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[1];
    eh->ether_shost[2] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[2];
    eh->ether_shost[3] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[3];
    eh->ether_shost[4] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[4];
    eh->ether_shost[5] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[5];
    eh->ether_dhost[0] = dst_mac[0];
    eh->ether_dhost[1] = dst_mac[1];
    eh->ether_dhost[2] = dst_mac[2];
    eh->ether_dhost[3] = dst_mac[3];
    eh->ether_dhost[4] = dst_mac[4];
    eh->ether_dhost[5] = dst_mac[5];
    /* Ethertype field */
    eh->ether_type = htons(0xEAEB);
    tx_len += sizeof(struct ether_header);

    memcpy(pwm, &pwm_data, sizeof(pwm_packet_t));
    tx_len += sizeof(pwm_packet_t);

    /* Index of the network device */
    socket_address.sll_ifindex = if_idx.ifr_ifindex;
    /* Address length*/
    socket_address.sll_halen = ETH_ALEN;
    /* Destination MAC */
    socket_address.sll_addr[0] = dst_mac[0];
    socket_address.sll_addr[1] = dst_mac[1];
    socket_address.sll_addr[2] = dst_mac[2];
    socket_address.sll_addr[3] = dst_mac[3];
    socket_address.sll_addr[4] = dst_mac[4];
    socket_address.sll_addr[5] = dst_mac[5];

    /* Send packet */
    if (sendto(sockfd, sendbuf, tx_len, 0, (struct sockaddr*)&socket_address, sizeof(struct sockaddr_ll)) < 0)
    {
        printf("Send failed\n");
    }

    close(sockfd);
    return 0;
}

