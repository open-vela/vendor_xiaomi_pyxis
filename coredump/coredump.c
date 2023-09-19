/****************************************************************************
 * vendor/xiaomi/vela/pyxis/coredump/coredump.c
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
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <syslog.h>
#include <elf.h>

#include <nuttx/fs/fs.h>
#include <nuttx/streams.h>
#include <nuttx/binfmt/binfmt.h>

#ifdef CONFIG_KVDB
#include <kvdb.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define COREDUMP_MAGIC    0x434f5245
#define COREDUMP_DEV      "/dev/coredump"
#define COREDUMP_DIR      "/data/coredump"
#define COREDUMP_MAXFILE  5
#define COREDUMP_CBLOCK   128

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct coredump_info
{
  uint32_t       magic;
  struct utsname name;
  time_t         time;
  uint32_t       size;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct lib_blkoutstream_s  g_blockstream;
static unsigned char             *g_blockinfo;
static bool                       g_coredump;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * coredump_restore
 ****************************************************************************/

void coredump_restore(FAR struct coredump_info *info)
{
  FAR struct lib_blkoutstream_s *stream = &g_blockstream;
  struct dirent *entry;
  unsigned char *swap;
  char dumppath[128];
  size_t offset = 1;
  struct tm *dtime;
  int oldpercent;
  size_t sblock;
  size_t csize;
  int dumpfd;
  DIR *dir;
  int ret;

  if (stream->inode == NULL)
    {
      return;
    }

  dir = opendir(COREDUMP_DIR);
  if (dir == NULL)
    {
      ret = mkdir(COREDUMP_DIR, 0777);
      if (ret < 0)
        {
          return;
        }
    }

  while ((entry = readdir(dir)) != NULL)
    {
      if (entry->d_type == DT_REG && !strncmp(entry->d_name, "Core-", 5))
        {
          if (++offset > COREDUMP_MAXFILE)
            {
              syslog(LOG_ERR, "%s: Too many coredump files ( >= %d)\n",
                               __func__, COREDUMP_MAXFILE);
              closedir(dir);
              return;
            }
        }
    }

  closedir(dir);

  ret = snprintf(dumppath, sizeof(dumppath),
                 COREDUMP_DIR "/Core-%s-%s",
                 info->name.nodename, info->name.version);
  dtime = localtime(&info->time);
  if (dtime)
    {
      ret += snprintf(dumppath + ret, sizeof(dumppath) - ret,
                      "-%d-%d-%d-%d-%d-%d", dtime->tm_year + 1900,
                      dtime->tm_mon + 1, dtime->tm_mday,
                      dtime->tm_hour, dtime->tm_min, dtime->tm_sec);
    }
  ret += snprintf(dumppath + ret, sizeof(dumppath) - ret, ".core");
  while (ret--)
    {
      if (dumppath[ret] == ' ' || dumppath[ret] == ':')
        {
          dumppath[ret] = '-';
        }
    }

  dumpfd = open(dumppath, O_CREAT | O_WRONLY | O_TRUNC, 0777);
  if (dumpfd < 0)
    {
      syslog(LOG_ERR, "%s: open %s fail\n", __func__, dumppath);
      return;
    }

  csize = stream->geo.geo_sectorsize * COREDUMP_CBLOCK;

  swap = malloc(csize);
  if (swap == NULL)
    {
      close(dumpfd);
      return;
    }

  offset     = 0;
  oldpercent = 0;

  syslog(LOG_INFO, "%s: Coredumping ... [%s] ...\n", __func__, dumppath);

  while (info->size > offset)
    {
      int percent;

      sblock = info->size > csize ? COREDUMP_CBLOCK :
               (info->size / stream->geo.geo_sectorsize) + 1;

      ret = stream->inode->u.i_bops->read(stream->inode, swap,
                                          offset / stream->geo.geo_sectorsize, sblock);
      if (ret < 0)
        {
          break;
        }

      ret = write(dumpfd, swap,
                  sblock == COREDUMP_CBLOCK ? csize : info->size);
      if (ret < 0)
        {
          break;
        }

      offset += ret;

      percent = (100 * offset / info->size);
      if (percent % 10 == 0 && percent != oldpercent)
        {
          syslog(LOG_INFO, "%s: Coredumping ... [%d%%]\n", __func__, percent);
          oldpercent = percent;
        }
    }

  syslog(LOG_INFO, "%s: Coredumping ... [%s][%lu]\n",
                    __func__, dumppath, info->size);
  close(dumpfd);
}

/****************************************************************************
 * do_coredump
 ****************************************************************************/

void do_coredump(struct memory_region_s *regions)
{
  FAR struct lib_blkoutstream_s *stream = &g_blockstream;
  struct coredump_info *info;
  int ret;

  if (g_coredump == false)
    {
      syslog(LOG_INFO, "%s: Coredump Disabled\n", __func__);
      return;
    }

  if (stream->inode == NULL)
    {
      return;
    }

  ret = stream->inode->u.i_bops->read(stream->inode, g_blockinfo,
                                      stream->geo.geo_nsectors - 1, 1);
  if (ret < 0)
    {
      return;
    }

  info = (FAR struct coredump_info *)g_blockinfo;

  if (info->magic == COREDUMP_MAGIC)
    {
      return;
    }

  ret = core_dump(regions, (FAR struct lib_outstream_s *)stream, INVALID_PROCESS_ID);
  if (ret < 0)
    {
      return;
    }

  info->magic = COREDUMP_MAGIC;
  info->size  = stream->public.nput;
  info->time = time(NULL);
  uname(&info->name);

  stream->inode->u.i_bops->write(stream->inode, (FAR void *)info,
                                 stream->geo.geo_nsectors - 1, 1);
}

/****************************************************************************
 * coredump_init
 ****************************************************************************/

int coredump_init(void)
{
  FAR struct lib_blkoutstream_s *stream = &g_blockstream;
  struct coredump_info *info;
  int ret;

  if (g_blockinfo != NULL || stream->inode != NULL)
    {
      return 0;
    }

#ifdef CONFIG_KVDB
  ret = property_get_int32("persist.coredump.enable", 0);
  if (ret <= 0)
    {
      g_coredump = false;
      return 0;
    }
#endif

  ret = lib_blkoutstream_open(stream, "/dev/coredump");
  if (ret < 0)
    {
      goto errout;
    }

  g_blockinfo = malloc(stream->geo.geo_sectorsize);
  if (g_blockinfo == NULL)
    {
      ret = -ENOMEM;
      goto errout;
    }

  ret = stream->inode->u.i_bops->read(stream->inode, g_blockinfo,
                                      stream->geo.geo_nsectors - 1, 1);
  if (ret < 0)
    {
      goto errout;
    }

  info = (FAR struct coredump_info *)g_blockinfo;
  if (info->magic == COREDUMP_MAGIC)
    {
      coredump_restore(info);
      info->magic = 0x0;
      ret = stream->inode->u.i_bops->write(stream->inode, (FAR void *)info,
                                           stream->geo.geo_nsectors - 1, 1);
    }

errout:
  if (ret < 0)
    {
      if (g_blockinfo)
        {
          free(g_blockinfo);
          g_blockinfo = NULL;
        }

      if (stream->inode)
        {
          lib_blkoutstream_close(stream);
        }
    }
  else
    {
      g_coredump = true;
    }

  return ret;
}

int main(int argc, FAR char *argv[])
{
  return coredump_init();
}
