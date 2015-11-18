#include <arpa/inet.h>
#define INFINITY		INT_MAX

struct file_info;
struct sockaddr_in;
struct gossip;
struct node_list;

void gossip_to_peer(struct file_info *fi);
void gossip_received(struct file_info *fi, char *line);
struct gossip* gossip_next(struct gossip* gossip);
struct sockaddr_in gossip_src(struct gossip* gossip);
char* gossip_latest(struct gossip* gossip);

void file_info_send(struct file_info *fi, char *buf, int size);
void file_broadcast(char *buf, int size, struct file_info *fi);
struct file_info* sockaddr_to_file(struct sockaddr_in dst);

// functions from timer.c
double timer_now(void); // Return current time
void timer_start(double when, void(*handler)(void *arg), void *arg); // Set a timer
int timer_check(void); //Check if any timers expired

// functions from addr.c
int addr_get(struct sockaddr_in *sin, const char *addr, int port);
int addr_cmp(struct sockaddr_in a1, struct sockaddr_in a2);

// functions from link_state.c
struct node_list *nl_create(void);
int nl_nsites(struct node_list *nl);
void nl_add(struct node_list *nl, char *node);
int nl_compare(const void *e1, const void *e2);
void nl_sort(struct node_list *nl);
int nl_index(struct node_list *nl, char *node);
char *nl_name(struct node_list *nl, int index);
void nl_destroy(struct node_list *nl);
void set_dist(struct node_list *nl, int graph[], int nnodes, char *src, char *dst, int dist);
char* addr_to_string(struct sockaddr_in addr);
struct sockaddr_in string_to_addr(char* string);
void dijkstra(int graph[], int nnodes, int src, int dist[], int prev[]);