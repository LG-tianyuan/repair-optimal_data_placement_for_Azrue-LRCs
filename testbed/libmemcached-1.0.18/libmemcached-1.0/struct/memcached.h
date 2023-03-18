/*  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 * 
 *  Libmemcached library
 *
 *  Copyright (C) 2011 Data Differential, http://datadifferential.com/
 *  Copyright (C) 2006-2009 Brian Aker All rights reserved.
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

struct memcached_st {
  /**
    @note these are static and should not change without a call to behavior.
  */
  struct {
    bool is_purging:1;
    bool is_processing_input:1;
    bool is_time_for_rebuild:1;
    bool is_parsing:1;
  } state; //状态

  struct {
    // Everything below here is pretty static.
    bool auto_eject_hosts:1;  //自动驱逐failed server
    bool binary_protocol:1; //二进制协议
    bool buffer_requests:1; //缓冲请求
    bool hash_with_namespace:1; //
    bool no_block:1; // Don't block，非阻塞
    bool reply:1;
    bool randomize_replica_read:1;
    bool support_cas:1; //是否支持cas命令
    bool tcp_nodelay:1; //使用tcp传输协议，无延迟
    bool use_sort_hosts:1;
    bool use_udp:1; //使用udp传输协议
    bool verify_key:1;  //是否校验键
    bool tcp_keepalive:1; //保持tcp连接不断开
    bool is_aes:1;
    bool is_fetching_version:1;
    bool not_used:1;
  } flags;

  memcached_server_distribution_t distribution; //
  hashkit_st hashkit;
  struct {
    unsigned int version;
  } server_info;  //服务器版本信息
  uint32_t number_of_hosts; //主机（服务器）数量
  memcached_instance_st *servers; //服务器实例数组
  memcached_instance_st *last_disconnected_server;  //最后断开连接的服务器？
  int32_t snd_timeout;  //发送超时
  int32_t rcv_timeout;  //接收超时
  uint32_t server_failure_limit;  //服务器故障限制
  uint32_t server_timeout_limit;  //服务器超时限制
  uint32_t io_msg_watermark;
  uint32_t io_bytes_watermark;
  uint32_t io_key_prefetch;
  uint32_t tcp_keepidle;
  int32_t poll_timeout;
  int32_t connect_timeout; // How long we will wait on connect() before we will timeout
  int32_t retry_timeout; //重试超时
  int32_t dead_timeout;
  int send_size;  //发送数据的大小
  int recv_size;  //接收数据的大小
  void *user_data;  //用户数据
  uint64_t query_id;  //查询id
  uint32_t number_of_replicas;  //副本的数量
  memcached_result_st result; //操作结果

  // lrc add
  uint32_t number_of_n;
  uint32_t number_of_k;
  uint32_t number_of_r;
  uint32_t number_of_b;
  uint32_t number_of_rack;
  uint32_t network_core_server_key; //server key of network core
  bool is_placed; //place data or not
  bool is_core; //request object is network core or not
  bool is_lrc; //apply lrc mechanism or not
  uint32_t node2server[25];  //node map to server
  uint32_t node2rack[25];  //node map to rack
  uint32_t node2group[25];  //node map to group of a single stripe
  uint32_t fail_node_index;  //index of failed node
  uint32_t lrc_scheme;  //scheme of lrc, i.e. Azure-LRC, Azure-LRC+1
  uint32_t placement_scheme;  //scheme of data placement, i.e. flat, random, optimal
  double divide_latency;
  double encoding_latency;
  double transmission_latency;
  double repair_transmission_latency;  //repair time of a block

  struct {
    bool weighted_;
    uint32_t continuum_count; // Ketama
    uint32_t continuum_points_counter; // Ketama
    time_t next_distribution_rebuild; // Ketama
    struct memcached_continuum_item_st *continuum; // Ketama
  } ketama;

  struct memcached_virtual_bucket_t *virtual_bucket;

  struct memcached_allocator_t allocators;  //内存分配器

  memcached_clone_fn on_clone;
  memcached_cleanup_fn on_cleanup;
  memcached_trigger_key_fn get_key_failure;
  memcached_trigger_delete_key_fn delete_trigger;
  memcached_callback_st *callbacks;
  struct memcached_sasl_st sasl;
  struct memcached_error_t *error_messages;
  struct memcached_array_st *_namespace;
  struct {
    uint32_t initial_pool_size;
    uint32_t max_pool_size;
    int32_t version; // This is used by pool and others to determine if the memcached_st is out of date.
    struct memcached_array_st *filename;  //配置文件名称
  } configure;
  struct {
    bool is_allocated:1;
  } options;

};
