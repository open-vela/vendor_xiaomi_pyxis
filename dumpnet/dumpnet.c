/****************************************************************************
 * examples/hello/hello_main.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>

#include <nuttx/net/netconfig.h>
#include <nuttx/net/netdev.h>
#include <nuttx/net/tcp.h>
#include <nuttx/net/udp.h>
#include <net/local/local.h>
#include <net/tcp/tcp.h>
#include <net/udp/udp.h>

#define inet_ntoa __inet_ntoa

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR static char *inet_ntoa(struct in_addr in, char *buffer)
{
  FAR unsigned char *ptr = (FAR unsigned char *)&in.s_addr;
  snprintf(buffer, INET_ADDRSTRLEN + 2, "%u.%u.%u.%u",
           ptr[0], ptr[1], ptr[2], ptr[3]);
  return buffer;
}

/****************************************************************************
 * hello_main
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
#if defined (CONFIG_NET_TCP) || defined (CONFIG_NET_UDP)
  char remote[INET_ADDRSTRLEN + 2];
  char local[INET_ADDRSTRLEN + 2];
  struct in_addr laddr;
  struct in_addr raddr;
#endif
  int i = 0;

  syslog(LOG_WARNING, "---------START---------\n");

#if defined (CONFIG_NET_TCP)
  FAR struct tcp_conn_s *tcp = NULL;

  syslog(LOG_WARNING, "-----------------------\n");
  syslog(LOG_WARNING, "[TCP]                 LADDR    LPORT             "
                      "RADDR      RPORT  RCVBUFS  SNDBUFS  SNDPEND  RCVPEND   RCVOOO\n");

  while ((tcp = tcp_nextconn(tcp)) != NULL)
    {
      laddr.s_addr = tcp->u.ipv4.laddr;
      raddr.s_addr = tcp->u.ipv4.raddr;
      syslog(LOG_WARNING, "TCP   [%d]: [%16s:%6d] -> [%16s:%6d] %8d %8d %8d %8d %8d\n", i++,
          inet_ntoa(laddr, local), ntohs(tcp->lport),
          inet_ntoa(raddr, remote), ntohs(tcp->rport),
          tcp->rcv_bufs, tcp->snd_bufs,
          tcp_wrbuffer_pendingsize(tcp),
          iob_get_queue_count(&tcp->readahead),
          iob_get_queue_count(&tcp->pendingahead));
    }
#endif

#if defined (CONFIG_NET_UDP)
  FAR struct udp_conn_s *udp = NULL;

  syslog(LOG_WARNING, "-----------------------\n");
  syslog(LOG_WARNING, "[UDP]                 LADDR    LPORT             "
                      "RADDR      RPORT  RCVBUFS  SNDBUFS  SNDPEND  RCVPEND\n");

  i = 0;
  while ((udp = udp_nextconn(udp)) != NULL)
    {
      laddr.s_addr = udp->u.ipv4.laddr;
      raddr.s_addr = udp->u.ipv4.raddr;
      syslog(LOG_WARNING, "UDP   [%d] [%16s:%6d] -> [%16s:%6d]: %8d %8d %8d %8d\n", i++,
          inet_ntoa(laddr, local), ntohs(udp->lport),
          inet_ntoa(raddr, remote), ntohs(udp->rport),
          udp->rcvbufs, udp->sndbufs,
          udp_wrbuffer_pendingsize(tcp),
          iob_get_queue_count(&udp->readahead));
    }
#endif

#if defined (CONFIG_NET_LOCAL)
  FAR struct local_conn_s *lo = NULL;

  syslog(LOG_WARNING, "-----------------------\n");
  syslog(LOG_WARNING, "[LOCAL]    PATH\n");

  i = 0;
  while ((lo = local_nextconn(lo)) != NULL)
    {
      syslog(LOG_WARNING, "LOCAL [%d]: [%s]\n", i++,
          lo->lc_path);
    }
#endif

  syslog(LOG_WARNING, "----------END----------\n");

  return 0;
}
