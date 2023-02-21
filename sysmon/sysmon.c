/*
 * Copyright (C) 2020 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include <nuttx/note/notectl_driver.h>

#include "trace.h"

#ifdef CONFIG_PYXIS_SYSMON

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_PYXIS_SYSMON_DAEMON_STACKSIZE
#define CONFIG_PYXIS_SYSMON_DAEMON_STACKSIZE 2048
#endif

#ifndef CONFIG_PYXIS_SYSMON_DAEMON_PRIORITY
#define CONFIG_PYXIS_SYSMON_DAEMON_PRIORITY 50
#endif

#ifndef CONFIG_PYXIS_SYSMON_INTERVAL
#define CONFIG_PYXIS_SYSMON_INTERVAL 2
#endif

#ifndef CONFIG_PYXIS_SYSMON_MOUNTPOINT
#define CONFIG_PYXIS_SYSMON_MOUNTPOINT "/proc"
#endif

#define MAX_CPULOAD_HISTORY 57

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sysmon_state_s {
  volatile bool started;
  volatile bool stop;
  pid_t pid;
  char line[80];
};

struct sysmon_feature_s {
  FAR const char* d_name;
  FAR char* path;
  bool enabled;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sysmon_state_s g_sysmon;
static enum feature_s {
  CRITMON,
  IRQS,
  CPULOAD,
  MEMINFO,
  IOBINFO,
  FEATURES
};
static struct sysmon_feature_s feature[] = {
  { .d_name = "critmon", .path = NULL, .enabled = false },
  { .d_name = "irqs", .path = NULL, .enabled = false },
  { .d_name = "cpuload", .path = NULL, .enabled = false },
  { .d_name = "meminfo", .path = NULL, .enabled = false },
  { .d_name = "iobinfo", .path = NULL, .enabled = false },
};
static struct sysmon_feature_s notectl = {
  .d_name = "/dev/notectl", .path = NULL, .enabled = false
};
static int notectlfd = 0;
#if CONFIG_TASK_NAME_SIZE > 0
static const char g_name[] = "Name:";
#endif

static int clhistory[MAX_CPULOAD_HISTORY];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: notectl_enable
 ****************************************************************************/

static bool notectl_enable(int flag, int notectlfd)
{
#ifdef CONFIG_DRIVERS_NOTECTL
  struct note_filter_mode_s mode;
  int oldflag;

  ioctl(notectlfd, NOTECTL_GETMODE, (unsigned long)&mode);

  oldflag = (mode.flag & NOTE_FILTER_MODE_FLAG_ENABLE) != 0;
  if (flag == oldflag)
    {
      /* Already set */

      return false;
    }

  if (flag)
    {
      mode.flag |= NOTE_FILTER_MODE_FLAG_ENABLE;
    }
  else
    {
      mode.flag &= ~NOTE_FILTER_MODE_FLAG_ENABLE;
    }

  ioctl(notectlfd, NOTECTL_SETMODE, (unsigned long)&mode);
#endif
  return true;
}

/****************************************************************************
 * Name: sysmon_isolate_value
 ****************************************************************************/

static FAR char* sysmon_isolate_value(FAR char* line)
{
  FAR char* ptr;

  while (isblank(*line) && *line != '\0') {
    line++;
  }

  ptr = line;
  while (*ptr != '\n' && *ptr != '\r' && *ptr != '\0') {
    ptr++;
  }

  *ptr = '\0';
  return line;
}

/****************************************************************************
 * Name: sysmon_process_directory
 ****************************************************************************/

static int sysmon_process_directory(FAR struct dirent* entryp)
{
  FAR const char* tmpstr;
  FAR char* filepath;
  FAR char* maxpreemp;
  FAR char* maxcrit;
  FAR char* endptr;
  FILE* stream;
  int errcode;
  int len;
  int ret;

#if CONFIG_TASK_NAME_SIZE > 0
  FAR char* name = NULL;

  /* Read the task status to get the task name */

  filepath = NULL;
  ret = asprintf(&filepath, CONFIG_PYXIS_SYSMON_MOUNTPOINT "/%s/status",
    entryp->d_name);
  if (ret < 0 || filepath == NULL) {
    errcode = errno;
    fprintf(stderr, "System Monitor: Failed to create path to status file: %d\n",
      errcode);
    return -errcode;
  }

  /* Open the status file */

  stream = fopen(filepath, "r");
  if (stream == NULL) {
    ret = -errno;
    fprintf(stderr, "System Monitor: Failed to open %s: %d\n",
      filepath, ret);
    goto errout_with_filepath;
  }

  while (fgets(g_sysmon.line, 80, stream) != NULL) {
    g_sysmon.line[79] = '\n';
    len = strlen(g_name);
    if (strncmp(g_sysmon.line, g_name, len) == 0) {
      tmpstr = sysmon_isolate_value(&g_sysmon.line[len]);
      if (*tmpstr == '\0') {
        ret = -EINVAL;
        goto errout_with_stream;
      }

      name = strdup(tmpstr);
      if (name == NULL) {
        ret = -EINVAL;
        goto errout_with_stream;
      }
    }
  }

  free(filepath);
  fclose(stream);
#endif

  /* Read critical section information */

  filepath = NULL;

  ret = asprintf(&filepath, CONFIG_PYXIS_SYSMON_MOUNTPOINT "/%s/critmon",
    entryp->d_name);
  if (ret < 0 || filepath == NULL) {
    errcode = errno;
    fprintf(stderr, "System Monitor: Failed to create path to Csection file: %d\n",
      errcode);
    ret = -EINVAL;
    goto errout_with_name;
  }

  /* Open the Csection file */

  stream = fopen(filepath, "r");
  if (stream == NULL) {
    ret = -errno;
    fprintf(stderr, "System Monitor: Failed to open %s: %d\n",
      filepath, ret);
    goto errout_with_filepath;
  }

  /* Read the line containing the Csection max durations */

  if (fgets(g_sysmon.line, 80, stream) == NULL) {
    ret = -errno;
    fprintf(stderr, "System Monitor: Failed to read from %s: %d\n",
      filepath, ret);
    goto errout_with_filepath;
  }

  /* Input Format:   X.XXXXXXXXX,X.XXXXXXXXX
  * Output Format:  X.XXXXXXXXX X.XXXXXXXXX NNNNN <name>
  */

  maxpreemp = g_sysmon.line;
  maxcrit = strchr(g_sysmon.line, ',');

  if (maxcrit != NULL) {
    *maxcrit++ = '\0';
    endptr = strchr(maxcrit, '\n');
    if (endptr != NULL) {
      *endptr = '\0';
    }
  } else {
    maxcrit = "None";
  }

  /* Finally, output the stack info that we gleaned from the procfs */

#if CONFIG_TASK_NAME_SIZE > 0
  printf("%11s %11s %5s %s\n",
    maxpreemp, maxcrit, entryp->d_name, name);
#else
  printf("%11s %11s %5s\n",
    maxpreemp, maxcrit, entryp->d_name);
#endif

  ret = OK;

errout_with_stream:
  fclose(stream);

errout_with_filepath:
  free(filepath);

errout_with_name:
#if CONFIG_TASK_NAME_SIZE > 0
  if (name != NULL) {
    free(name);
  }
#endif

  return ret;
}

/****************************************************************************
 * Name: sysmon_check_name
 ****************************************************************************/

static bool sysmon_check_name(FAR char* name)
{
  int i;

  /* Check each character in the name */

  for (i = 0; i < NAME_MAX && name[i] != '\0'; i++) {
    if (!isdigit(name[i])) {
      /* Name contains something other than a decimal numeric character */

        return false;
    }
  }

  return true;
}

/****************************************************************************
 * Name: sysmon_global_crit
 ****************************************************************************/

static void sysmon_global_crit(void)
{
  FAR char* filepath;
  FAR char* cpu;
  FAR char* maxpreemp;
  FAR char* maxcrit;
  FAR char* endptr;
  FILE* stream;
  int errcode;
  int ret;

  /* Read critical section information */

  filepath = NULL;

  ret = asprintf(&filepath, CONFIG_PYXIS_SYSMON_MOUNTPOINT "/critmon");
  if (ret < 0 || filepath == NULL) {
    errcode = errno;
    fprintf(stderr, "System Monitor: Failed to create path to Csection file: %d\n",
      errcode);
    return;
  }

  /* Open the Csection file */

  stream = fopen(filepath, "r");
  if (stream == NULL) {
    errcode = errno;
    fprintf(stderr, "System Monitor: Failed to open %s: %d\n",
      filepath, errcode);
    goto errout_with_filepath;
  }

  /* Read the line containing the Csection max durations for each CPU */

  while (fgets(g_sysmon.line, 80, stream) != NULL) {
    /* Input Format:  X,X.XXXXXXXXX,X.XXXXXXXXX
    * Output Format: X.XXXXXXXXX X.XXXXXXXXX       CPU X
    */

    cpu = g_sysmon.line;
    maxpreemp = strchr(g_sysmon.line, ',');

    if (maxpreemp != NULL) {
      *maxpreemp++ = '\0';
      maxcrit = strchr(maxpreemp, ',');
      if (maxcrit != NULL) {
        *maxcrit++ = '\0';
        endptr = strchr(maxcrit, '\n');
        if (endptr != NULL) {
          *endptr = '\0';
        }
      } else {
        maxcrit = "None";
      }
    } else {
      maxpreemp = "None";
      maxcrit = "None";
    }

    /* Finally, output the stack info that we gleaned from the procfs */

    printf("%11s %11s  ---  CPU %s\n", maxpreemp, maxcrit, cpu);
  }

  fclose(stream);

errout_with_filepath:
  free(filepath);
}

/****************************************************************************
 * Name: sysmon_init
 ****************************************************************************/

static void sysmon_init(void)
{
  for (int i = 0; i < FEATURES; i++) {
    asprintf(&feature[i].path, CONFIG_PYXIS_SYSMON_MOUNTPOINT "/%s",
      feature[i].d_name);
    feature[i].enabled = !!fopen(feature[i].path, "r");
    printf(feature[i].enabled ? "%s/%s enabled\n" : "%s/%s disabled\n",
      CONFIG_PYXIS_SYSMON_MOUNTPOINT, feature[i].d_name);
  }

  notectlfd = open("/dev/notectl", 0);
  if (notectlfd > 0) {
    notectl.enabled = true;
    notectl_enable(true, notectlfd);
  }
  printf(notectl.enabled ? "%s enabled\n" : "%s disabled\n", notectl.d_name);
}

/****************************************************************************
 * Name: sysmon_deinit
 ****************************************************************************/

static void sysmon_deinit(void)
{
  if (notectlfd > 0) {
    notectl_enable(false, notectlfd);
    close(notectlfd);
  }
}

/****************************************************************************
 * Name: sysmon_list_once
 ****************************************************************************/

static int sysmon_list_once(bool graph)
{
  int fd;
  FAR char* buffer;
  int nbytesread;
  DIR* dirp;
  int exitcode = EXIT_SUCCESS;
  int errcount = 0;
  int cl;
  int ret;

  printf("========================================\n");
  for (int i = 0; i < FEATURES; i++) {
    if (feature[i].enabled) {
      switch (i) {
        case CRITMON:
        /* Output a Header */

#if CONFIG_TASK_NAME_SIZE > 0
          printf("PRE-EMPTION CSECTION    PID   DESCRIPTION\n");
#else
          printf("PRE-EMPTION CSECTION    PID\n");
#endif
          printf("MAX DISABLE MAX TIME\n");

          /* Should global usage first */

          sysmon_global_crit();

          /* Open the top-level procfs directory */

          dirp = opendir(CONFIG_PYXIS_SYSMON_MOUNTPOINT);
          if (dirp == NULL) {
            /* Failed to open the directory */

            fprintf(stderr, "System Monitor: Failed to open directory: %s\n",
              CONFIG_PYXIS_SYSMON_MOUNTPOINT);

            if (++errcount > 100) {
              fprintf(stderr, "System Monitor: Too many errors ... exiting\n");
              exitcode = EXIT_FAILURE;
              break;
            }
          }

          /* Read each directory entry */

          for (;;) {
            FAR struct dirent* entryp = readdir(dirp);
            if (entryp == NULL) {
              /* Finished with this directory */

                break;
            }

          /* Task/thread entries in the /proc directory will all be (1)
            * directories with (2) all numeric names.
            */

            if (DIRENT_ISDIRECTORY(entryp->d_type) && sysmon_check_name(entryp->d_name)) {
              /* Looks good -- process the directory */

              ret = sysmon_process_directory(entryp);
              if (ret < 0) {
                /* Failed to process the thread directory */

                fprintf(stderr, "System Monitor: Failed to process sub-directory: %s\n",
                  entryp->d_name);

                if (++errcount > 100) {
                  fprintf(stderr, "System Monitor: Too many errors ... exiting\n");
                  exitcode = EXIT_FAILURE;
                  break;
                }
              }
            }
          }

          closedir(dirp);
          fputc('\n', stdout);

          printf("Processes switch info:\n");
          printf("[CPU] Time:   Prev_task-PID State ==> Next_task-PID\n");
          sysmon_trace_dump(stdout);
          sysmon_trace_dump_clear();
          fflush(stdout);
          break;

        case IRQS:
          printf("Interrupt info:\n");
          goto cat;

        case CPULOAD:
          fd = fopen(feature[i].path, "r");
          if (!fd) {
            break;
          }
          for (int j = MAX_CPULOAD_HISTORY - 1; j; j--) {
            clhistory[j] = clhistory[j-1];
          }
          ret = fscanf(fd, "%d.%d%%", &clhistory[0], &cl);
          if (ret < 0) {
            fclose(fd);
            break;
          }
          printf("CPU load: %d.%d%%\n", clhistory[0], cl);
          printf("#");
          for (int j = 0; j < MAX_CPULOAD_HISTORY; j++) {
            printf("-");
          }
          printf("#\n");
          for (int k = 10; k >= 0; k--) {
            printf("|");
            printf("\x1B[35m");
            for (int j = MAX_CPULOAD_HISTORY - 1; j>=0; j--) {
              printf(clhistory[j] >= k * 10 ? "*" : ".");
            }
            printf("\x1B[0m");
            printf("|\n");
          }
          printf("#");
          for (int j = 0; j < MAX_CPULOAD_HISTORY; j++) {
            printf("-");
          }
          printf("#\n");

          fclose(fd);
          break;

        case MEMINFO:
          printf("Memory usage:\n");
          goto cat;

        case IOBINFO:
          printf("IO block usage:\n");
          goto cat;

      default:
cat:
        fd = open(feature[i].path, O_RDONLY);
        if (fd < 0) {
          break;
        }
        buffer = (FAR char*)malloc(1024);
        if (!buffer) {
          close(fd);
          break;
        }
        for (;;) {
          nbytesread = read(fd, buffer, 1024);
          if (nbytesread < 0) {
            break;
          } else if (nbytesread > 0) {
            int nbyteswritten = 0;
            while (nbyteswritten < nbytesread) {
              ssize_t n = fwrite(buffer, sizeof(char), nbytesread, stdout);
              if (n < 0) {
                fprintf(stderr, "write to stdout error\n");
                break;
              } else {
                nbyteswritten += n;
              }
            }
          } else {
            fflush(stdout);
            break;
          }
        }
        free(buffer);
        close(fd);
        break;
      }
    }
  }
  return exitcode;
}

/****************************************************************************
 * Name: sysmon_daemon
 ****************************************************************************/

static int sysmon_daemon(int argc, char** argv)
{
  int exitcode = EXIT_SUCCESS;

  printf("System Monitor: Running: %d\n", g_sysmon.pid);
  memset(clhistory, -1, sizeof(clhistory));

  /* Loop until we detect that there is a request to stop. */

  while (!g_sysmon.stop) {
    /* Wait for the next sample interval */
    if (notectl.enabled)
      notectl_enable(true, notectlfd);
    sleep(CONFIG_PYXIS_SYSMON_INTERVAL);
    if (notectl.enabled)
      notectl_enable(false, notectlfd);

    exitcode = sysmon_list_once(1);

    if (exitcode != EXIT_SUCCESS) {
      break;
    }
  }

  /* Stopped */

  g_sysmon.stop = false;
  g_sysmon.started = false;
  if (notectl.enabled)
    close(notectlfd);
  printf("System Monitor: Stopped: %d\n", g_sysmon.pid);

  return exitcode;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sysmon_start_main(int argc, char** argv)
{
  sysmon_init();

  /* Has the monitor already started? */

  sched_lock();

  if (!g_sysmon.started) {
    int ret;

    /* No.. start it now */

    /* Then start the stack monitoring daemon */

    g_sysmon.started = true;
    g_sysmon.stop = false;

    ret = task_create("System Monitor", CONFIG_PYXIS_SYSMON_DAEMON_PRIORITY,
      CONFIG_PYXIS_SYSMON_DAEMON_STACKSIZE,
      (main_t)sysmon_daemon, argv);
    if (ret < 0) {
      int errcode = errno;
      printf("System Monitor ERROR: Failed to start the monitor: %d\n",
        errcode);
    } else {
      g_sysmon.pid = ret;
      printf("System Monitor: Started: %d\n", g_sysmon.pid);
    }

    sched_unlock();
    return 0;
  }

  sched_unlock();
  printf("System Monitor: %s: %d\n",
    g_sysmon.stop ? "Stopping" : "Running", g_sysmon.pid);
  return 0;
}

int sysmon_stop_main(int argc, char** argv)
{
  /* Has the monitor already started? */

  if (g_sysmon.started) {
    /* Stop the stack monitor.  The next time the monitor wakes up,
    * it will see the stop indication and will exist.
    */

    printf("System Monitor: Stopping: %d\n", g_sysmon.pid);
    g_sysmon.stop = true;
    sysmon_deinit();
  }

  printf("System Monitor: Stopped: %d\n", g_sysmon.pid);
  return 0;
}

int main(int argc, char** argv)
{
  sysmon_init();
  return sysmon_list_once(0);
  sysmon_deinit();
}

#endif /* CONFIG_PYXIS_SYSMON */
