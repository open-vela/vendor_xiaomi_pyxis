#
# Copyright (C) 2020 Xiaomi Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

include $(APPDIR)/Make.defs

# Stack Monitor Application

PROGNAME = sysmon sysmon_start sysmon_stop
PRIORITY = $(CONFIG_VELA_SYSMON_PRIORITY)
STACKSIZE = $(CONFIG_VELA_SYSMON_STACKSIZE)
MODULE = $(CONFIG_VELA_SYSMON)

ifeq ($(CONFIG_DRIVER_NOTERAM),y)
  CSRCS = trace_dump.c
endif

MAINSRC = sysmon.c

include $(APPDIR)/Application.mk
