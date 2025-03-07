/****************************************************************************
 * apps/system/trace/trace.h
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

#ifndef __VELA_PYXIS_SYSMON_TRACE_H
#define __VELA_PYXIS_SYSMON_TRACE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTERAM

/****************************************************************************
 * Name: trace_dump
 *
 * Description:
 *   Read notes and dump trace results.
 *
 ****************************************************************************/

int sysmon_trace_dump(FAR FILE *out);

/****************************************************************************
 * Name: trace_dump_clear
 *
 * Description:
 *   Clear all contents of the buffer
 *
 ****************************************************************************/

void sysmon_trace_dump_clear(void);

/****************************************************************************
 * Name: trace_dump_get_overwrite
 *
 * Description:
 *   Get overwrite mode
 *
 ****************************************************************************/

bool sysmon_trace_dump_get_overwrite(void);

/****************************************************************************
 * Name: trace_dump_set_overwrite
 *
 * Description:
 *   Set overwrite mode
 *
 ****************************************************************************/

void sysmon_trace_dump_set_overwrite(bool mode);

#else /* CONFIG_DRIVERS_NOTERAM */

#define sysmon_trace_dump(out)
#define sysmon_trace_dump_clear()
#define sysmon_trace_dump_get_overwrite()      0
#define sysmon_trace_dump_set_overwrite(mode)  (void)(mode)

#endif /* CONFIG_DRIVERS_NOTERAM */

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __VELA_PYXIS_SYSMON_TRACE_H */
