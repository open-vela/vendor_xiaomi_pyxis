/****************************************************************************
 * vendor/xiaomi/vela/pyxis/fbdebug/fbdebug.c
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

#include <ctype.h>
#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <nuttx/video/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "netutils/base64.h"
#include <png.h>

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

#define BGRA_TO_RGBA(c) (((c)&0xFF00FF00) | ((c) >> 16 & 0xFF) | ((c) << 16 & 0xFF0000))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct fb_state_s
{
  int fd;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
#ifdef CONFIG_FB_OVERLAY
  struct fb_overlayinfo_s oinfo;
#endif
  FAR void *fbmem;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * fb_main
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  FAR const char *fbdev = "/dev/fb0";
  FAR const char *out_path = NULL;
  struct fb_state_s state;
  uint32_t color = 0xDEADBEEF;
  int x = 0;
  int y = 0;
  int w = 1;
  int h = 1;
  int ret;
  int c;
  int gflag = 0;
  int sflag = 0;
  int mflag = 0;
  int base64 = 0;

  opterr = 0;

  while ((c = getopt (argc, argv, "D:gsmx:y:w:h:c:bo:")) != -1)
    switch (c)
      {
      case 'D':
        fbdev = optarg;
        break;
      case 'g':
        gflag = 1;
        break;
      case 's':
        sflag = 1;
        break;
      case 'm':
        mflag = 1;
        break;
      case 'x':
        x = atoi(optarg);
        break;
      case 'y':
        y = atoi(optarg);
        break;
      case 'w':
        w = atoi(optarg);
        break;
      case 'h':
        h = atoi(optarg);
        break;
      case 'c':
        color = strtoul(optarg, NULL, 16);
        break;
      case 'b':
        base64 = 1;
        break;
      case 'o':
        out_path = optarg;
        break;
      case '?':
        if (optopt == 'D' || optopt == 'x' || optopt == 'y' || optopt == 'w'
            || optopt == 'h' || optopt == 'c' || optopt == 'o')
          fprintf (stderr, "Option -%c requires an argument.\n", optopt);
        else if (isprint (optopt))
          fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf (stderr,
                   "Unknown option character `\\x%x'.\n",
                   optopt);
        return 1;
      default:
        abort ();
      }
  printf("\nFBdebug: a tool to get screenshot and set color to a certain area on framebuffer.\n");
  printf("\nUsage: fbdebug [-D <fb_path>] -g -x <Xoffset> -y <Yoffset> -w <area_width> -h <area_height> [-b] [-o <out_path>] [-s -c <color_hex>]\n");
  printf("Arguments:\n");
  printf("  -D: override default framebuffer path defined in Kconfig");
  printf("  -g: get screenshot\n");
  printf("  -x/y: area start position x & y, default to zero\n");
  printf("  -w/h: area width & height(must be positive), default to 1\n");
  printf("  -b: print base64-encoded image string\n");
  printf("  -o: write png file to <out_path>\n");
  printf("  -s: set area to target color, must be used together with -c <color_hex>\n");
  printf("  -m: mark the area with rectangle frame, color is <color_hex> or, if not specified, purple\n");
  printf("\nExample: fbdebug -g  // get the first pixel on framebuffer and print in hex\n");
  printf("         fbdebug -g -x 100 -y 100 -w 50 -h 50 -m // print hex color values in area (50, 50) at (100, 100) and mark the area with purple\n");
  printf("         fbdebug -x 200 -y 200 -w 50 -h 50 -g -b -s -c 0xFF00FF00  // encode area (50, 50) at (200, 200) to base64 and print, then set the area to green\n");
  printf("         fbdebug -w 480 -h 480 -g -o /data/screenshot.png    //save area (480, 480) at (0, 0) to file\n");
  printf("\n");
  if (sflag && color == 0xDEADBEEF)
    {
      printf("-s set but no color provided! Add -c [color_hex]\n");
    }
  /* Open the framebuffer driver */

  state.fd = open(fbdev, O_RDWR);
  if (state.fd < 0)
    {
      int errcode = errno;
      fprintf(stderr, "ERROR: Failed to open %s: %d\n", fbdev, errcode);
      return EXIT_FAILURE;
    }

  /* Get the characteristics of the framebuffer */

  ret = ioctl(state.fd, FBIOGET_VIDEOINFO,
              (unsigned long)((uintptr_t)&state.vinfo));
  if (ret < 0)
    {
      int errcode = errno;
      fprintf(stderr, "ERROR: ioctl(FBIOGET_VIDEOINFO) failed: %d\n",
              errcode);
      close(state.fd);
      return EXIT_FAILURE;
    }

  ret = ioctl(state.fd, FBIOGET_PLANEINFO,
              (unsigned long)((uintptr_t)&state.pinfo));
  if (ret < 0)
    {
      int errcode = errno;
      fprintf(stderr, "ERROR: ioctl(FBIOGET_PLANEINFO) failed: %d\n",
              errcode);
      close(state.fd);
      return EXIT_FAILURE;
    }

  /* Only these pixel depths are supported.  viinfo.fmt is ignored, only
   * certain color formats are supported.
   */

  if (state.pinfo.bpp != 32 && state.pinfo.bpp != 16 &&
      state.pinfo.bpp != 8  && state.pinfo.bpp != 1)
    {
      fprintf(stderr, "ERROR: bpp=%u not supported\n", state.pinfo.bpp);
      close(state.fd);
      return EXIT_FAILURE;
    }

  /* mmap() the framebuffer.
   *
   * NOTE: In the FLAT build the frame buffer address returned by the
   * FBIOGET_PLANEINFO IOCTL command will be the same as the framebuffer
   * address.  mmap(), however, is the preferred way to get the framebuffer
   * address because in the KERNEL build, it will perform the necessary
   * address mapping to make the memory accessible to the application.
   */

  state.fbmem = mmap(NULL, state.pinfo.fblen, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_FILE, state.fd, 0);
  if (state.fbmem == MAP_FAILED)
    {
      int errcode = errno;
      fprintf(stderr, "ERROR: ioctl(FBIOGET_PLANEINFO) failed: %d\n",
              errcode);
      close(state.fd);
      return EXIT_FAILURE;
    }

  uint8_t* fb = state.fbmem;
  char* base64str = NULL;
  fb += y * state.pinfo.stride + (x * state.pinfo.bpp >> 3);
  if (gflag)
    {
      if (!out_path && !base64)
        for (int i = 0; i < h; i++)
          {
            if (state.pinfo.bpp == 32)
              for (int j = 0; j < w; j++)
              {
                printf("%" PRIx32 " ", ((uint32_t*)fb)[j]);
              }
            else if (state.pinfo.bpp == 16)
              for (int j = 0; j < w; j++)
              {
                printf("%x ", ((uint16_t*)fb)[j]);
              }
            else if (state.pinfo.bpp == 8)
              for (int j = 0; j < w; j++)
              {
                printf("%x ", fb[j]);
              }
            else //if (state.pinfo.bpp == 1)
              for (int j = 0; j < (w + 7) >> 3; j++)
              {
                printf("%x", fb[j]);
              }
            fb += state.pinfo.stride;
            printf("\n");
          }
      else if (state.pinfo.bpp == 32 || w * h > 0)
        {
          int ROI_stride = w * state.pinfo.bpp >> 3;
          int ROI_len = ROI_stride * h;
          void* roi = malloc(ROI_len);
          if (!roi)
            goto Error_handler;
          for (int i = 0; i < h; i++)
            {
#ifdef CONFIG_PYXIS_FBDEBUG_SWAPRB
              uint32_t* troi = roi + i * ROI_stride;
              uint32_t* tfb = (uint32_t*)(fb + i * state.pinfo.stride);
              for (int j = 0; j < w; j++)
                {
                  troi[j] = BGRA_TO_RGBA(tfb[j]);
                }
#else
              memcpy(roi + i * ROI_stride, fb + i * state.pinfo.stride, ROI_stride);
#endif
            }

          if (out_path)
            {
              png_image image;
              int error;

              /* Construct the PNG image structure. */

              memset(&image, 0, sizeof(image));

              image.version = PNG_IMAGE_VERSION;
              image.width   = w;
              image.height  = h;
              image.format  = PNG_FORMAT_BGRA;

              /* Write the PNG image. */

              error = png_image_write_to_file(&image, out_path, 0, state.fbmem,
                                              state.pinfo.stride, NULL);
              if (error < 0)
                {
                  printf("Write file to %s failed: %d\n", out_path, error);
                }
              else
                {
                  printf("Screenshot saved to %s\n", out_path);
                }
            }
          if (base64)
            {
              /* TODO:base64str = base64_encode(png, pngsize, NULL, NULL); */
            }
          free(roi);
          if (base64str)
            printf("FB [%d,%d](%d,%d) in base64:%s\n", x, y, w, h, base64str);
        }
      else if (state.pinfo.bpp != 32)
        {
          printf("Encoded output only supports 32bpp FB\n");
        }
      else
        {
          printf("ROI size is 0, please check input w and h\n");
        }
    }
  if (base64str)
    {
      free(base64str);
    }
  if (sflag && color != 0xDEADBEEF)
    for (int i = 0; i < h; i++)
      {
        if (state.pinfo.bpp == 32)
          for (int j = 0; j < w; j++)
          {
            ((uint32_t*)fb)[j] = color;
          }
        else if (state.pinfo.bpp == 16)
          for (int j = 0; j < w; j++)
          {
            ((uint16_t*)fb)[j] = color & 0xFFFF;
          }
        else if (state.pinfo.bpp == 8)
          for (int j = 0; j < w; j++)
          {
            fb[j] = color & 0xFF;
          }
        else //if (state.pinfo.bpp == 1)
          for (int j = 0; j < (w + 7) >> 3; j++)
          {
            if ((w & 7) && (j == w >> 3))
              {
                fb[j] = (color & 1) * ((1 << (w & 7)) - 1);
              }
            else
              {
                fb[j] = (color & 1) * 0xFF;
              }
          }
        fb += state.pinfo.stride;
      }
  if (mflag)
    {
      fb = state.fbmem;
      fb += y * state.pinfo.stride + (x * state.pinfo.bpp >> 3);
      if (color == 0xDEADBEEF)
        {
          color = 0xFFFF00FF;
        }
      for (int i = 0; i < h; i++)
      {
        if (state.pinfo.bpp == 32)
          {
            if (i == 0 || i == h - 1)
              for (int j = 0; j < w; j++)
                {
                  ((uint32_t*)fb)[j] = color;
                }
            else
              ((uint32_t*)fb)[0] = ((uint32_t*)fb)[w - 1] = color;
          }
        else if (state.pinfo.bpp == 16)
          {
            if (i == 0 || i == h - 1)
              for (int j = 0; j < w; j++)
                {
                  ((uint16_t*)fb)[j] = color & 0xFFFF;
                }
            else
              ((uint16_t*)fb)[0] = ((uint16_t*)fb)[w - 1] = color & 0xFFFF;
          }
        else if (state.pinfo.bpp == 8)
          {
            if (i == 0 || i == h - 1)
              for (int j = 0; j < w; j++)
                {
                  fb[j] = color & 0xFF;
                }
            else
              fb[0] = fb[w - 1] = color & 0xFF;
          }
        else //if (state.pinfo.bpp == 1)
          {
            if (i == 0 || i == h - 1)
              for (int j = 0; j < (w + 7) >> 3; j++)
              {
                if ((w & 7) && (j == w >> 3))
                  {
                    fb[j] = (color & 1) * ((1 << (w & 7)) - 1);
                  }
                else
                  {
                    fb[j] = (color & 1) * 0xFF;
                  }
              }
            else
              {
                fb[0] = (color & 1) * 0xFF;
                fb[(w - 1) >> 3] = (color & 1) * (w & 7 ? ((1 << (w & 7)) - 1) : 0xFF);
              }
          }
        fb += state.pinfo.stride;
      }
    }
Error_handler:
  munmap(state.fbmem, state.pinfo.fblen);
  close(state.fd);
  return EXIT_SUCCESS;
}
