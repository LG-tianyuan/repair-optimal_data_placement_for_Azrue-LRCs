/*  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 * 
 *  Libmemcached library
 *
 *  Copyright (C) 2011 Data Differential, http://datadifferential.com/ 
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *      * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *  copyright notice, this list of conditions and the following disclaimer
 *  in the documentation and/or other materials provided with the
 *  distribution.
 *
 *      * The names of its contributors may not be used to endorse or
 *  promote products derived from this software without specific prior
 *  written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#pragma once

struct memcached_stat_st {//对应`stats`命令
  unsigned long connection_structures;  //Memcached分配的连接结构数量
  unsigned long curr_connections; //当前连接数量
  unsigned long curr_items; //当前存储的数据总数
  pid_t pid;  //memcached服务器进程ID
  unsigned long pointer_size; //操作系统指针大小
  unsigned long rusage_system_microseconds; //进程累计系统时间（微秒）
  unsigned long rusage_system_seconds;  //进程累计系统时间（秒）
  unsigned long rusage_user_microseconds; //进程累计用户时间（微秒）
  unsigned long rusage_user_seconds;  //进程累计用户时间（秒）
  unsigned long threads;  //当前线程数
  unsigned long time; //服务器当前Unix时间戳
  unsigned long total_connections;  //Memcached运行以来连接总数
  unsigned long total_items;  //启动以来存储的数据总数
  unsigned long uptime; //服务器已运行秒数
  unsigned long long bytes; //当前存储占用的字节数
  unsigned long long bytes_read;  //读取总字节数
  unsigned long long bytes_written; //发送总字节数
  unsigned long long cmd_get; //get命令请求次数
  unsigned long long cmd_set; //set命令请求次数
  unsigned long long evictions; //LRC驱逐的对象数目
  unsigned long long get_hits;  //get命令命中次数
  unsigned long long get_misses;  //get命令未命中次数
  unsigned long long limit_maxbytes;  //分配的内存总大小
  char version[MEMCACHED_VERSION_STRING_LENGTH];  //Memcached版本
  void *__future; // @todo create a new structure to place here for future usage
  memcached_st *root;
};

