/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * memory_monitor_sr.cpp - Implementation of memory monitor module
 */

#if !defined(WINDOWS)
#include <stdio.h>
#include "memory_monitor_sr.hpp"

#ifdef __cplusplus
extern "C" {
#endif
//TODO : 에러처리 추가
int memmon_disabled_force (void)
{
  printf ("LD_PRELOAD가 비활성화 되어있습니다.\n");
  return 0;
}

int memmon_dump_memory_usage (void)
{
  printf ("LD_PRELOAD가 비활성화 되어있습니다.\n");
  return 0;
}

#ifdef __cplusplus
}
#endif
#endif // !WINDOWS
