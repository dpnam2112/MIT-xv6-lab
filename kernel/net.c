#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

typedef struct udp_recv_queue_entry{
  char* buf; // allocated via kalloc()
  uint16 len; // len doesn't exceed PGSIZE
  int saddr;
  int sport;
} udp_recv_queue_entry_t;

// a simple implementation of FIFO buffer for received udp packets
// this buffer only contains udp payloads
#define SOCK_RECV_QUEUE_SIZE 64
typedef struct udp_recv_queue {
  int head; // out
  int tail; // in
  udp_recv_queue_entry_t entries[SOCK_RECV_QUEUE_SIZE];
  struct spinlock lock;
} udp_recv_queue_t;

void
udp_recv_queue_enq(udp_recv_queue_t* q, char* payload, int len, int saddr, int sport){
  acquire(&q->lock);
  udp_recv_queue_entry_t *entry = &q->entries[q->head++];
  entry->buf = kalloc();
  if (entry->buf == 0){
    release(&q->lock);
    panic("net: failed to kalloc()");
  }
  memmove(entry->buf, payload, len);
  entry->buf[len] = 0;
  entry->sport = sport;
  entry->saddr = saddr;
  entry->len = len;
  release(&q->lock);
  wakeup(&q->tail);
}

// get the packet in FIFO order and put its content in dst_buf
// return the length of the udp payload
// if the queue is empty, put the current process in sleep state
int
udp_recv_queue_deq(udp_recv_queue_t* q, char* dst_buf, int *saddr, int *sport){
  acquire(&q->lock);
  while (q->head == q->tail){
    sleep(&q->tail, &q->lock);
  }
  udp_recv_queue_entry_t *entry = &q->entries[q->tail++];
  memmove(dst_buf, entry->buf, entry->len);
  dst_buf[entry->len] = 0;
  *saddr = entry->saddr;
  *sport = entry->sport;
  kfree(entry->buf);
  release(&q->lock);
  return entry->len;
}

// simple implementation: a table of 65535 udp_recv_queue recordssys_recv
// we assume that there is only one remote machine connecting to this host,
// hence, port numbers could be used as keys for look-up.

#define RECV_QUEUE_TBL_SIZE 128
struct net_udp_sock {
  uint8 alloc;
  uint16 port;
  uint32 pid; // id of the process owning this socket
  // these queues contain incoming packets from remote sockets
  udp_recv_queue_t recv_queue;
};
typedef struct net_udp_sock net_udp_sock_t;

udp_recv_queue_t*
get_udp_recv_queue(net_udp_sock_t *udp_sock){
  return &udp_sock->recv_queue;
}

// a simple implementation for udp socket lookup table :)
// I know this is weird but well... hope that conflict won't happen here...
// Further improvement: handle port namespace per IP, since we're assuming there is a single IP
// assigned to the host, then there is only a single table here.
#define SOCKTBL_SIZE 1024
net_udp_sock_t socktbl[SOCKTBL_SIZE];

// get udp socket given the port
// assume that the host only has one ip (the defined 'local_ip')
net_udp_sock_t*
get_udp_sock(int port){
  net_udp_sock_t *sock = &socktbl[port % SOCKTBL_SIZE];
  if (sock->alloc == 0){
    return 0;
  }
  if (sock->port != port){
    panic("bad: port mismatch");
  }
  return sock;
}

void
socktbl_init()
{
  for (int i = 0; i < SOCKTBL_SIZE; i++){
    socktbl[i].alloc = 0;
    socktbl[i].port = -1;
    socktbl[i].pid = -1;

    udp_recv_queue_t *recv_queue = &socktbl[i].recv_queue;
    initlock(&recv_queue->lock, "udp_recv_queue_lock");
    recv_queue->head = 0;
    recv_queue->tail = 0;
  }
}

void
socktbl_alloc_sock(int pid, int port)
{
  acquire(&netlock);
  int i = port % SOCKTBL_SIZE;
  if (socktbl[i].alloc != 0){
    release(&netlock);
    panic("socktbl entry in use");
  }
  socktbl[i].alloc = 1;
  socktbl[i].port = port;
  socktbl[i].pid = pid;
  release(&netlock);
}

void
socktbl_dealloc_sock(int pid, int port)
{
  acquire(&netlock);
  int i = port % SOCKTBL_SIZE;
  if (socktbl[i].alloc != 0){
    release(&netlock);
    panic("bad: port conflict");
  }
  socktbl[i].alloc = 0;
  socktbl[i].port = -1;
  socktbl[i].pid = -1;
  release(&netlock);
}


void
netinit(void)
{
  initlock(&netlock, "netlock");
  socktbl_init();
}

// handle the received udp packet
void
udp_rx(char*, int);

//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.
  //
  int port;
  argint(0, &port);
  struct proc *p = myproc();
  socktbl_alloc_sock(p->pid, port);
  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //
  int port;
  argint(0, &port);
  struct proc *p = myproc();
  socktbl_dealloc_sock(p->pid, port);
  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  //
  // Your code here.
  //
  struct proc *p = myproc();
  int dport;
  uint64 sport_va; // source port
  uint64 saddr_va; // source address
  int maxlen;
  uint64 ubuf; // user-space buffer
  argint(0, &dport);
  argaddr(1, &saddr_va);
  argaddr(2, &sport_va);
  argaddr(3, &ubuf);
  argint(4, &maxlen);
  net_udp_sock_t *sock = get_udp_sock(dport);
  if (sock == 0){
//    printf("debug: no socket is allocated at port %d\n", dport);
    panic("sys_recv");
  }
  udp_recv_queue_t *q = get_udp_recv_queue(sock);
  if (q == 0){
    panic("sys_recv");
  }
  char *pkt_buf = kalloc();
  int pktlen;
  int saddr, sport;
  pktlen = udp_recv_queue_deq(q, pkt_buf, &saddr, &sport);
  int copiedlen = (pktlen < maxlen) ? pktlen : maxlen;
//  printf("debug: pkt_buf=%s\n", pkt_buf);
  if (copyout(p->pagetable, ubuf, pkt_buf, maxlen) != 0){
    panic("sys_recv");
  }
//  printf("debug: saddr=%x\n", saddr);
//  printf("debug: sport=%x\n", sport);
  if (copyout(p->pagetable, saddr_va, (void*) &saddr, sizeof(saddr)) != 0){
    panic("sys_recv");
  }
  
  short temp_sport = sport;
  if (copyout(p->pagetable, sport_va, (void*) &temp_sport, sizeof(short)) != 0){
    panic("sys_recv");
  }

  kfree(pkt_buf);
  return copiedlen;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  // Your code here.
  //

  struct eth *eth = (struct eth *) buf;
  struct ip *ip = (struct ip *)(eth + 1);
  if (ip->ip_p == IPPROTO_UDP){
    udp_rx(buf, len);
  } else {
    kfree(buf);
  }
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}

void
udp_rx(char* buf, int len)
{
  // assumptions for simplicity:
  // packet's size doesn't exceed page size
  // there are physically only two endpoints in a udp connection
  struct eth *eth = (struct eth *) buf;
  struct ip *ip = (struct ip *)(eth + 1);
  struct udp *udp = (struct udp *)(ip + 1);

  uint16 dport = ntohs(udp->dport);
  uint16 sport = ntohs(udp->sport);
  uint16 ulen = ntohs(udp->ulen);
  int saddr = ntohl(ip->ip_src);

  net_udp_sock_t *udp_sock = get_udp_sock(dport);
  if (udp_sock == 0){
    kfree(buf);
    return;
  }

  // lookup udp buffer for the connection saddrort) (ip is intentionally ignored here)
  udp_recv_queue_t *recv_queue = get_udp_recv_queue(udp_sock);
  int payload_len = ulen - sizeof(struct udp);
//  printf("debug: raw_payload=%s, len=%d\n", (char* ) (udp + 1), payload_len);
  udp_recv_queue_enq(recv_queue, (char*) (udp + 1), payload_len, saddr, sport);
  kfree(buf);
}
