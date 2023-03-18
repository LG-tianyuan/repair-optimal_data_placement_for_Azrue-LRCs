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


#include <libmemcached/common.h>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <vector>
#include <math.h>
extern "C"{
#include "../Jerasure-1.2A/cauchy.h"
#include "../Jerasure-1.2A/reed_sol.h"
#include "../Jerasure-1.2A/jerasure.h"
#include "../Jerasure-1.2A/galois.h"
}
// #define STORAGE_DEBUG

enum memcached_storage_action_t {
  SET_OP,
  REPLACE_OP,
  ADD_OP,
  PREPEND_OP,
  APPEND_OP,
  CAS_OP
};

//lrc add
struct group_st {
  uint32_t* blocks;
  uint32_t group_id;
  uint32_t num_of_blocks;
};

struct rack_st {
  uint32_t* blocks;
  uint32_t rack_id;
  uint32_t num_of_nodes;
};

double get_times(memcached_st *ptr, double *times){
  if(times != NULL){
    times[0] = ptr->divide_latency;
    times[1] = ptr->encoding_latency;
    times[2] = ptr->transmission_latency;
    return 0;
  }else{
    return ptr->repair_transmission_latency;
  }
}

//lrc add
//calculate the size of a block
static size_t determine_block_size_rs(const size_t value_length, uint32_t k) {
  size_t block_size = 0;
  size_t temp_value_length = 0;
  // int size_of_long = sizeof(long);
  int size_of_long = sizeof(char);
  int mod = value_length % (k*size_of_long);
  if(mod == 0) {
    block_size = value_length / k;
  } else {
    temp_value_length = value_length + k*size_of_long - mod;
    block_size = temp_value_length / k;
  }
  return block_size;
}

static void fill_data_ptrs(char** data_ptrs, uint32_t size, size_t block_size, const char* value, const size_t value_length)
{
  size_t bytes_remained = value_length;
  for(uint32_t i = 0; i < size; ++i){
    if(block_size <= bytes_remained){
      memcpy(data_ptrs[i], value + i * block_size, block_size);
      bytes_remained -= block_size;
    }else{
      memcpy(data_ptrs[i], value + i * block_size, bytes_remained);
      bytes_remained -= bytes_remained;
    }
  }
}
//encoding
static void calculate_parity_ptrs_rs(char** data_ptrs, uint32_t k, char** coding_ptrs, uint32_t m, size_t block_size)
{
  int* matrix = reed_sol_vandermonde_coding_matrix(k, m, 8);
  jerasure_matrix_encode(k, m, 8, matrix, data_ptrs, coding_ptrs, block_size);
  free(matrix);
}
//calculate keys for blocks in a stripe
static void generate_new_keys_rs(char **key_ptrs, uint32_t size, const char *key, const size_t key_length) {
  for(uint32_t i = 0; i < size; ++i) {
    key_ptrs[i] = new char[key_length + 2];
    memcpy(key_ptrs[i], key, key_length);
    int ten = i / 10;
    int one = i - ten * 10;
    char ten_bit_char = '0' + ten;
    char one_bit_char = '0' + one;
    key_ptrs[i][key_length] = ten_bit_char;
    key_ptrs[i][key_length+1] = one_bit_char;
    key_ptrs[i][key_length+2] = '\0';
  }
}
//simulate cross-cluster transfering, set and get by once respectively
static void cross_networkcore_libmemcached(Memcached* ptr, const char* key, size_t key_length, const char* value, size_t value_length)
{
  size_t val_len = value_length;
  uint32_t flags = 0;
  memcached_return_t rc;
  ptr->is_core = true;
  rc = memcached_set(ptr, key, key_length, value, value_length, 86400, 0);
  if(rc != MEMCACHED_SUCCESS){
    printf("[NETWORK CORE] Set key failure! %s!\n", memcached_strerror(ptr, rc));
  }
  memcached_get_by_key(ptr, NULL, 0, key, key_length, &val_len, &flags, &rc);
  if(rc != MEMCACHED_SUCCESS){
    printf("[NETWORK CORE] Get value failure! %s!\n", memcached_strerror(ptr, rc));
  }
  ptr->is_core = false;
}
// calculate information of each groups
static struct group_st* groups_information(Memcached* ptr, uint32_t n, uint32_t l){
  int group_len[l] = {0};
  struct group_st *groups = new group_st[l];
  for(uint32_t i = 0; i < n; i++){
    group_len[ptr->node2group[i]]++;
  }
  for(uint32_t i = 0; i < l; i++){
    groups[i].group_id = i;
    groups[i].num_of_blocks = group_len[i];
    groups[i].blocks = new uint32_t[group_len[i]];
  }
  int group_index[l] = {0};
  for(uint32_t i = 0; i < n; i++){
    uint32_t group_id = ptr->node2group[i];
    groups[group_id].blocks[group_index[group_id]++] = i;
  }
  #ifdef STORAGE_DEBUG
  for(uint32_t i = 0; i < l; i++)
  {
    printf("Group %d: ",groups[i].group_id);
    for(uint32_t j = 0; j < groups[i].num_of_blocks; j++)
    {
      printf("%d, ",groups[i].blocks[j]);
    }
    printf("\n");
  }
  #endif
  return groups;
}

static void optimal_data_placement(Memcached* ptr)
{
  uint32_t n = ptr->number_of_n;
  uint32_t k = ptr->number_of_k;
  uint32_t r = ptr->number_of_r;
  uint32_t l = ceil(double(k) / double(r));
  if(ptr->lrc_scheme == MEMCACHED_LRC_AZURE_1){
    l += 1;
  }
  uint32_t g = n - k - l;
  uint32_t b = g + 1;
  l = ceil(double(k) / double(r)) + 1;

  struct group_st *groups = groups_information(ptr, n, l);
  struct group_st *remaining_groups = new group_st[l];
  uint32_t remain = 0;
  struct rack_st *racks = new rack_st[n];
  uint32_t rack_id = 0;
  uint32_t theta = floor(g / r);
  uint32_t ii = 0;
  for(uint32_t i = 0; i < l - 1; i++){
    if(r + 1 <= b){ //place every θ groups in a cluster
      racks[rack_id].blocks = new uint32_t[g + theta];
      racks[rack_id].rack_id = rack_id;
      uint32_t index = 0;
      for(uint32_t j = ii; j < ii + theta; j++){
        if(j >= l - 1)
          break;
        for(uint32_t jj = 0; jj < groups[j].num_of_blocks; jj++){
          racks[rack_id].blocks[index++] = groups[j].blocks[jj];
        }
      }
      racks[rack_id].num_of_nodes = index;
      rack_id += 1;
      ii += theta;
      if(ii >= l - 1)
        break;
    }else{  //place every g+1 blocks in a cluster
      uint32_t cur_group_len = groups[i].num_of_blocks;
      for(uint32_t j = 0; j < cur_group_len; j += b){
        if(j + b > cur_group_len){
          remaining_groups[remain].blocks = new uint32_t[cur_group_len-j-1];
          remaining_groups[remain].group_id = i;
          uint32_t index = 0;
          for(uint32_t jj = j; jj < cur_group_len; jj++){
            remaining_groups[remain].blocks[index++] = groups[i].blocks[jj];
          }
          remaining_groups[remain].num_of_blocks = index;
          remain++;
        }else{
          racks[rack_id].blocks = new uint32_t[b];
          racks[rack_id].rack_id = rack_id;
          uint32_t index = 0;
          for(uint32_t jj = j; jj < j + b; jj++){
            racks[rack_id].blocks[index++] = groups[i].blocks[jj];
          }
          racks[rack_id].num_of_nodes = index;
          rack_id += 1;
        }
      }
    }
  }
  racks[rack_id].blocks = new uint32_t[g+l];
  racks[rack_id].rack_id = rack_id;
  uint32_t index = 0;
  for(uint32_t i = 0; i < groups[l-1].num_of_blocks; i++)
  {
    racks[rack_id].blocks[index++] = groups[l-1].blocks[i];
  }
  racks[rack_id].num_of_nodes = index;
  uint32_t g_rack_id = rack_id;
  rack_id += 1;
  if(remain > 0){ //try to place remaining local group into fewest clusters
    theta = -1;
    if(k % r == 0){
      uint32_t m = remaining_groups[0].num_of_blocks;
      if(m == 1){
        index = racks[g_rack_id].num_of_nodes;
        for(uint32_t i = 0; i < remain; i++){
          racks[g_rack_id].blocks[index++] = remaining_groups[i].blocks[0];
        }
        racks[g_rack_id].num_of_nodes = index;
      }else{
        theta = floor(g / (m - 1));
        for(uint32_t i = 0; i < remain; i += theta){
          racks[rack_id].blocks = new uint32_t[g+theta];
          index = 0;
          for(uint32_t j = i; j < i + theta; j++){
            if(j >= remain)
              break;
            for(uint32_t jj = 0; jj < remaining_groups[j].num_of_blocks; jj++){
              racks[rack_id].blocks[index++] = remaining_groups[j].blocks[jj];
            }
          }
          racks[rack_id].num_of_nodes = index;
          rack_id += 1;
        }
      }
    }else{
      if((r + 1) % b != 0){
        uint32_t m = remaining_groups[0].num_of_blocks;
        if(m == 1){
          index = racks[g_rack_id].num_of_nodes;
          for(uint32_t i = 0; i < l - 2; i++){
            racks[g_rack_id].blocks[index++] = remaining_groups[i].blocks[0];
          }
          racks[g_rack_id].num_of_nodes = index;
        }else{
          theta = floor(g / (m - 1));
          for(uint32_t i = 0; i < l - 2; i += theta){
            racks[rack_id].blocks = new uint32_t[g+theta];
            index = 0;
            for(uint32_t j = i; j < i + theta; j++){
              if(j >= l - 2)
                break;
              for(uint32_t jj = 0; jj < remaining_groups[j].num_of_blocks; jj++){
                racks[rack_id].blocks[index++] = remaining_groups[j].blocks[jj];
              }
            }
            racks[rack_id].num_of_nodes = index;
            rack_id += 1;
          }
        }
        if(l - 1 == remain){
          uint32_t m_ = remaining_groups[remain-1].num_of_blocks;
          uint32_t last_group = floor((l - 2) / theta) * theta;
          if(m_ == 1){
            index = racks[g_rack_id].num_of_nodes;
            racks[g_rack_id].blocks[index++] = remaining_groups[remain-1].blocks[0];
            racks[g_rack_id].num_of_nodes = index;
          }else if(m != 1 && l - 2 != last_group && (remain - 1 - last_group) * m + m_ <= g + remain - last_group){
            index = racks[rack_id-1].num_of_nodes;
            uint32_t *tmp_blocks = new uint32_t[g+remain-last_group];
            for(uint32_t i = 0; i < index; i++)
            {
              tmp_blocks[i] = racks[rack_id-1].blocks[i];
            }
            for(uint32_t i = 0; i < m_; i++){
              tmp_blocks[index++] = remaining_groups[remain-1].blocks[i];
            }
            racks[rack_id-1].blocks = tmp_blocks;
            racks[rack_id-1].num_of_nodes = index;
          }else{
            racks[rack_id].blocks = new uint32_t[b];
            index = 0;
            for(uint32_t i = 0; i < m_; i++){
              racks[rack_id].blocks[index++] = remaining_groups[remain-1].blocks[i];
            }
            racks[rack_id].num_of_nodes = index;
            rack_id += 1;
          }
        }
      }else{
        uint32_t m_ = remaining_groups[remain-1].num_of_blocks;
        if(m_ == 1){
          index = racks[g_rack_id].num_of_nodes;
          racks[g_rack_id].blocks[index++] = remaining_groups[remain-1].blocks[0];
          racks[g_rack_id].num_of_nodes = index;
        }else{
          racks[rack_id].blocks = new uint32_t[b];
          index = 0;
          for(uint32_t i = 0; i < m_; i++){
            racks[rack_id].blocks[index++] = remaining_groups[remain-1].blocks[i];
          }
          racks[rack_id].num_of_nodes = index;
          rack_id += 1;
        }
      }
    }
  }
  for(uint32_t i = 0; i < rack_id; i++){
    uint32_t cur_rack_len = racks[i].num_of_nodes;
    for(uint32_t j = 0; j < cur_rack_len; j++){
      ptr->node2rack[racks[i].blocks[j]] = racks[i].rack_id;
    }
  }
  ptr->is_placed = true;
  ptr->number_of_rack = rack_id;
  #ifdef STORAGE_DEBUG
  printf("Optimal data placement:\n");
  for(uint32_t i = 0; i < rack_id; i++){
    printf("Rack %d:", i);
    uint32_t cur_rack_len = racks[i].num_of_nodes;
    for(uint32_t j = 0; j < cur_rack_len; j++){
      printf("%d ", racks[i].blocks[j]);
    }
    printf("\n");
  }
  #endif
}

static void random_data_placement(Memcached* ptr)
{
  uint32_t n = ptr->number_of_n;
  uint32_t k = ptr->number_of_k;
  uint32_t r = ptr->number_of_r;
  uint32_t l = ceil(double(k) / double(r));
  if(ptr->lrc_scheme == MEMCACHED_LRC_AZURE_1){
    l += 1;
  }
  uint32_t g = n - k - l;
  uint32_t b = g + 1;
  std::vector<uint32_t> left_blocks;
  for(uint32_t i = 0; i < n; i++)
  {
    left_blocks.push_back(i);
  }
  uint32_t rack_id = 0;
  uint32_t cnt = 0;
  uint32_t blocks_num_in_rack = 0;
  uint32_t racks_num = 0;
  for(uint32_t i = 0; i < n; i++){
    racks_num++;
    blocks_num_in_rack = rand() % b + 1;
    uint32_t ii = 0;
    for(uint32_t j = 0; j < blocks_num_in_rack; j++){
      uint32_t index = rand() % (n - cnt);
      uint32_t id = left_blocks[index];
      ptr->node2rack[id] = rack_id;
      left_blocks.erase(left_blocks.begin() + index);
      ii++;
      cnt++;
      if(cnt >= n)
        break;
    }
    rack_id += 1;
    if(cnt >= n)
      break;
  }
  ptr->is_placed = true;
  ptr->number_of_rack = racks_num;
  #ifdef STORAGE_DEBUG
  printf("Random data placement:\n");
  for(uint32_t i = 0; i < ptr->number_of_n; i++){
    printf("%d ", ptr->node2rack[i]);
  }
  printf("\n");
  #endif
}

static void flat_data_placement(Memcached* ptr)
{
  uint32_t n = ptr->number_of_n;
  for(uint32_t i = 0; i < n; i++){
    ptr->node2rack[i] = i;
  }
  ptr->is_placed = true;
  ptr->number_of_rack = n;
  #ifdef STORAGE_DEBUG
  printf("Flat data placement:\n");
  for(uint32_t i = 0; i < ptr->number_of_n; i++){
    printf("%d ", ptr->node2rack[i]);
  }
  printf("\n");
  #endif
}

/* Inline this */
static inline const char *storage_op_string(memcached_storage_action_t verb)
{
  switch (verb)
  {
  case REPLACE_OP:
    return "replace ";

  case ADD_OP:
    return "add ";

  case PREPEND_OP:
    return "prepend ";

  case APPEND_OP:
    return "append ";

  case CAS_OP:
    return "cas ";

  case SET_OP:
    break;
  }

  return "set ";
}

static inline uint8_t can_by_encrypted(const memcached_storage_action_t verb)
{
  switch (verb)
  {
  case SET_OP:
  case ADD_OP:
  case CAS_OP:
  case REPLACE_OP:
    return true;
    
  case APPEND_OP:
  case PREPEND_OP:
    break;
  }

  return false;
}

static inline uint8_t get_com_code(const memcached_storage_action_t verb, const bool reply)
{
  if (reply == false)
  {
    switch (verb)
    {
    case SET_OP:
      return PROTOCOL_BINARY_CMD_SETQ;

    case ADD_OP:
      return PROTOCOL_BINARY_CMD_ADDQ;

    case CAS_OP: /* FALLTHROUGH */
    case REPLACE_OP:
      return PROTOCOL_BINARY_CMD_REPLACEQ;

    case APPEND_OP:
      return PROTOCOL_BINARY_CMD_APPENDQ;

    case PREPEND_OP:
      return PROTOCOL_BINARY_CMD_PREPENDQ;
    }
  }

  switch (verb)
  {
  case SET_OP:
    break;

  case ADD_OP:
    return PROTOCOL_BINARY_CMD_ADD;

  case CAS_OP: /* FALLTHROUGH */
  case REPLACE_OP:
    return PROTOCOL_BINARY_CMD_REPLACE;

  case APPEND_OP:
    return PROTOCOL_BINARY_CMD_APPEND;

  case PREPEND_OP:
    return PROTOCOL_BINARY_CMD_PREPEND;
  }

  return PROTOCOL_BINARY_CMD_SET;
}

static memcached_return_t memcached_send_binary(Memcached *ptr,
                                                memcached_instance_st* server,
                                                uint32_t server_key,
                                                const char *key,
                                                const size_t key_length,
                                                const char *value,
                                                const size_t value_length,
                                                const time_t expiration,
                                                const uint32_t flags,
                                                const uint64_t cas,
                                                const bool flush,
                                                const bool reply,
                                                memcached_storage_action_t verb)
{
  protocol_binary_request_set request= {};
  size_t send_length= sizeof(request.bytes);

  initialize_binary_request(server, request.message.header);

  request.message.header.request.opcode= get_com_code(verb, reply);
  request.message.header.request.keylen= htons((uint16_t)(key_length + memcached_array_size(ptr->_namespace)));
  request.message.header.request.datatype= PROTOCOL_BINARY_RAW_BYTES;
  if (verb == APPEND_OP or verb == PREPEND_OP)
  {
    send_length -= 8; /* append & prepend does not contain extras! */
  }
  else
  {
    request.message.header.request.extlen= 8;
    request.message.body.flags= htonl(flags);
    request.message.body.expiration= htonl((uint32_t)expiration);
  }

  request.message.header.request.bodylen= htonl((uint32_t) (key_length + memcached_array_size(ptr->_namespace) + value_length +
                                                            request.message.header.request.extlen));
  
  if (cas)
  {
    request.message.header.request.cas= memcached_htonll(cas);
  }

  libmemcached_io_vector_st vector[]=
  {
    { NULL, 0 },
    { request.bytes, send_length },
    { memcached_array_string(ptr->_namespace),  memcached_array_size(ptr->_namespace) },
    { key, key_length },
    { value, value_length }
  };

  /* write the header */
  memcached_return_t rc;
  if ((rc= memcached_vdo(server, vector, 5, flush)) != MEMCACHED_SUCCESS)
  {
    memcached_io_reset(server);

#if 0
    if (memcached_has_error(ptr))
    {
      memcached_set_error(*server, rc, MEMCACHED_AT);
    }
#endif

    assert(memcached_last_error(server->root) != MEMCACHED_SUCCESS);
    return memcached_last_error(server->root);
  }

  if (verb == SET_OP and ptr->number_of_replicas > 0)
  {
    request.message.header.request.opcode= PROTOCOL_BINARY_CMD_SETQ;
    WATCHPOINT_STRING("replicating");

    for (uint32_t x= 0; x < ptr->number_of_replicas; x++)
    {
      ++server_key;
      if (server_key == memcached_server_count(ptr))
      {
        server_key= 0;
      }

      memcached_instance_st* instance= memcached_instance_fetch(ptr, server_key);

      if (memcached_vdo(instance, vector, 5, false) != MEMCACHED_SUCCESS)
      {
        memcached_io_reset(instance);
      }
      else
      {
        memcached_server_response_decrement(instance);
      }
    }
  }

  if (flush == false)
  {
    return MEMCACHED_BUFFERED;
  }

  // No reply always assumes success
  if (reply == false)
  {
    return MEMCACHED_SUCCESS;
  }

  return memcached_response(server, NULL, 0, NULL);
}

static memcached_return_t memcached_send_ascii(Memcached *ptr,
                                               memcached_instance_st* instance,
                                               const char *key,
                                               const size_t key_length,
                                               const char *value,
                                               const size_t value_length,
                                               const time_t expiration,
                                               const uint32_t flags,
                                               const uint64_t cas,
                                               const bool flush,
                                               const bool reply,
                                               const memcached_storage_action_t verb)
{
  char flags_buffer[MEMCACHED_MAXIMUM_INTEGER_DISPLAY_LENGTH +1];
  int flags_buffer_length= snprintf(flags_buffer, sizeof(flags_buffer), " %u", flags);
  if (size_t(flags_buffer_length) >= sizeof(flags_buffer) or flags_buffer_length < 0)
  {
    return memcached_set_error(*instance, MEMCACHED_MEMORY_ALLOCATION_FAILURE, MEMCACHED_AT, 
                               memcached_literal_param("snprintf(MEMCACHED_MAXIMUM_INTEGER_DISPLAY_LENGTH)"));
  }

  char expiration_buffer[MEMCACHED_MAXIMUM_INTEGER_DISPLAY_LENGTH +1];
  int expiration_buffer_length= snprintf(expiration_buffer, sizeof(expiration_buffer), " %llu", (unsigned long long)expiration);
  if (size_t(expiration_buffer_length) >= sizeof(expiration_buffer) or expiration_buffer_length < 0)
  {
    return memcached_set_error(*instance, MEMCACHED_MEMORY_ALLOCATION_FAILURE, MEMCACHED_AT, 
                               memcached_literal_param("snprintf(MEMCACHED_MAXIMUM_INTEGER_DISPLAY_LENGTH)"));
  }

  char value_buffer[MEMCACHED_MAXIMUM_INTEGER_DISPLAY_LENGTH +1];
  int value_buffer_length= snprintf(value_buffer, sizeof(value_buffer), " %llu", (unsigned long long)value_length);
  if (size_t(value_buffer_length) >= sizeof(value_buffer) or value_buffer_length < 0)
  {
    return memcached_set_error(*instance, MEMCACHED_MEMORY_ALLOCATION_FAILURE, MEMCACHED_AT, 
                               memcached_literal_param("snprintf(MEMCACHED_MAXIMUM_INTEGER_DISPLAY_LENGTH)"));
  }

  char cas_buffer[MEMCACHED_MAXIMUM_INTEGER_DISPLAY_LENGTH +1];
  int cas_buffer_length= 0;
  if (cas)
  {
    cas_buffer_length= snprintf(cas_buffer, sizeof(cas_buffer), " %llu", (unsigned long long)cas);
    if (size_t(cas_buffer_length) >= sizeof(cas_buffer) or cas_buffer_length < 0)
    {
      return memcached_set_error(*instance, MEMCACHED_MEMORY_ALLOCATION_FAILURE, MEMCACHED_AT, 
                                 memcached_literal_param("snprintf(MEMCACHED_MAXIMUM_INTEGER_DISPLAY_LENGTH)"));
    }
  }

  libmemcached_io_vector_st vector[]=
  {
    { NULL, 0 },
    { storage_op_string(verb), strlen(storage_op_string(verb))},
    { memcached_array_string(ptr->_namespace), memcached_array_size(ptr->_namespace) },
    { key, key_length },
    { flags_buffer, size_t(flags_buffer_length) },
    { expiration_buffer, size_t(expiration_buffer_length) },
    { value_buffer, size_t(value_buffer_length) },
    { cas_buffer, size_t(cas_buffer_length) },
    { " noreply", reply ? 0 : memcached_literal_param_size(" noreply") },
    { memcached_literal_param("\r\n") },
    { value, value_length },
    { memcached_literal_param("\r\n") }
  };

  /* Send command header */
  memcached_return_t rc=  memcached_vdo(instance, vector, 12, flush);

  // If we should not reply, return with MEMCACHED_SUCCESS, unless error
  if (reply == false)
  {
    return memcached_success(rc) ? MEMCACHED_SUCCESS : rc; 
  }

  if (flush == false)
  {
    return memcached_success(rc) ? MEMCACHED_BUFFERED : rc; 
  }

  if (rc == MEMCACHED_SUCCESS)
  {
    char buffer[MEMCACHED_DEFAULT_COMMAND_SIZE];
    rc= memcached_response(instance, buffer, sizeof(buffer), NULL);

    if (rc == MEMCACHED_STORED)
    {
      return MEMCACHED_SUCCESS;
    }
  }

  if (rc == MEMCACHED_WRITE_FAILURE)
  {
    memcached_io_reset(instance);
  }

  assert(memcached_failed(rc));
#if 0
  if (memcached_has_error(ptr) == false)
  {
    return memcached_set_error(*ptr, rc, MEMCACHED_AT);
  }
#endif

  return rc;
}

static memcached_return_t memcached_lrc_send_binary(Memcached *ptr,
                                                memcached_instance_st* server,
                                                uint32_t server_key,
                                                const char *key,
                                                const size_t key_length,
                                                const char *value,
                                                const size_t value_length,
                                                const time_t expiration,
                                                const uint32_t flags,
                                                const uint64_t cas,
                                                const bool flush,
                                                const bool reply,
                                                memcached_storage_action_t verb)
{
  uint32_t n = ptr->number_of_n;
  uint32_t k = ptr->number_of_k;
  uint32_t r = ptr->number_of_r;
  uint32_t l = ceil(double(k) / double(r));
  if(ptr->lrc_scheme == MEMCACHED_LRC_AZURE_1){
    l += 1;
  }
  uint32_t g = n - k - l;
  #ifdef STORAGE_DEBUG
  printf("(n, k, r, l, g) %d, %d, %d, %d, %d\n", n, k, r, l, g);
  #endif


  struct timeval start_time, end_time;
  gettimeofday(&start_time, NULL);

  size_t block_size = determine_block_size_rs(value_length, k);
  char** key_ptrs = new char*[n];
  for(uint32_t i = 0; i < n; ++i){
    key_ptrs[i] = new char[key_length + 3];
    memset(key_ptrs[i], 0, key_length + 3);
  }
  generate_new_keys_rs(key_ptrs, n, key, key_length);
  // fill data blocks
  char **data_ptrs = new char*[k];
  for(uint32_t i = 0; i < k; ++i) {
    data_ptrs[i] = new char [block_size];
    memset(data_ptrs[i], 0, block_size);
    data_ptrs[i][block_size] = '\0';    
  }
  fill_data_ptrs(data_ptrs, k, block_size, value, value_length);
  gettimeofday(&end_time, NULL);
  ptr->divide_latency = end_time.tv_sec - start_time.tv_sec + (end_time.tv_usec - start_time.tv_usec) * 1.0 / 1000000;

  char **global_coding_ptrs = new char*[g];
  for(uint32_t i = 0; i < g; i++){
    global_coding_ptrs[i] = new char[block_size];
    memset(global_coding_ptrs[i], 0, block_size);
    global_coding_ptrs[i][block_size] = '\0';
  }

  char **all_blocks = new char*[n];
  for(uint32_t i = 0; i < n; ++i) {
    all_blocks[i] = new char[block_size];
    memset(all_blocks[i], 0, block_size);
    all_blocks[i][block_size] = '\0';
  }
  //calculate global parity blocks
  gettimeofday(&start_time, NULL);
  calculate_parity_ptrs_rs(data_ptrs, k, global_coding_ptrs, g, block_size);
  for(uint32_t i = 0; i < g; i++){
    memcpy(all_blocks[k + i], global_coding_ptrs[i], block_size);
    ptr->node2group[k + i] = ceil(double(k) / double(r));
  }

  uint32_t remain = k;
  uint32_t index = 0;
  uint32_t l_ = l;
  if(ptr->lrc_scheme == MEMCACHED_LRC_AZURE_1)
    l_ = l - 1;
  for(uint32_t i = 0; i < l_; i++){ //calculate local parity blocks
    char **local_coding_ptrs = new char*[1];
    local_coding_ptrs[0] = new char[block_size];
    memset(local_coding_ptrs[0], 0, block_size);
    local_coding_ptrs[0][block_size] = '\0';
    if(remain >= r){
      char **local_group = new char*[r];
      for(uint32_t ii = 0; ii < r; ii++){
        index = i*r + ii;
        local_group[ii] = data_ptrs[index];
        memcpy(all_blocks[index], data_ptrs[index], block_size);
        ptr->node2group[index] = i;
       }
      calculate_parity_ptrs_rs(local_group, r, local_coding_ptrs, 1, block_size);
      memcpy(all_blocks[k+g+i], local_coding_ptrs[0], block_size);
      ptr->node2group[k+g+i] = i;
      remain = remain - r;
    }else{
      char **local_group = new char*[remain];
      for(uint32_t ii = 0; ii < remain; ii++){
        index = i*r+ii;
        local_group[ii] = data_ptrs[index];
        memcpy(all_blocks[index], data_ptrs[index], block_size);
        ptr->node2group[index] = i;
      }
      calculate_parity_ptrs_rs(local_group, remain, local_coding_ptrs, 1, block_size);
      uint32_t j = n - 1;
      if(ptr->lrc_scheme == MEMCACHED_LRC_AZURE_1)
        j -= 1;
      memcpy(all_blocks[j], local_coding_ptrs[0], block_size);
      ptr->node2group[j] = i;
    } 
    delete local_coding_ptrs[0];
    delete local_coding_ptrs;
  }
  if(ptr->lrc_scheme == MEMCACHED_LRC_AZURE_1){
    char **local_coding_ptrs = new char*[1];
    local_coding_ptrs[0] = new char[block_size];
    memset(local_coding_ptrs[0], 0, block_size);
    local_coding_ptrs[0][block_size] = '\0';
    calculate_parity_ptrs_rs(global_coding_ptrs, g, local_coding_ptrs, 1, block_size);
    memcpy(all_blocks[n-1], local_coding_ptrs[0], block_size);
    ptr->node2group[n-1] = ceil(double(k) / double(r));
    delete local_coding_ptrs[0];
    delete local_coding_ptrs;
  }
  gettimeofday(&end_time, NULL);
  ptr->encoding_latency = end_time.tv_sec - start_time.tv_sec + (end_time.tv_usec - start_time.tv_usec) * 1.0 / 1000000;

  #ifdef STORAGE_DEBUG
  // printf("Blocks value:\n");
  // for(uint32_t i = 0; i < ptr->number_of_n; i++){
  //   printf("Block %d (length %ld):\nkey:%s, value:%s\n", i, strlen(all_blocks[i]), key_ptrs[i], all_blocks[i]);
  // }
  printf("Groups information: ");
  for(uint32_t i = 0; i < ptr->number_of_n; i++){
    printf("%d ", ptr->node2group[i]);
  }
  printf("\n");
  #endif

  // data placement
  if(!ptr->is_placed){
    if(ptr->placement_scheme == MEMCACHED_PLACEMENT_FLAT){
      flat_data_placement(ptr);
    }else if(ptr->placement_scheme == MEMCACHED_PLACEMENT_RANDOM){
      random_data_placement(ptr);
    }else{
      optimal_data_placement(ptr);
    }
  } 
  
  gettimeofday(&start_time, NULL);
  //simulate cross-cluster transmission
  for(uint32_t i = 0; i < n; ++i){
    cross_networkcore_libmemcached(ptr, key_ptrs[i], key_length+2, all_blocks[i], block_size);
  }

  // generate k+m requests for k+m data and parity blocks
  uint32_t last_data_index = k - 1;
  uint32_t storage_key = server_key;
  size_t last_data_request_size = value_length - block_size * (k - 1);
  char *last_data_request = new char[last_data_request_size];
  memcpy(last_data_request, data_ptrs[k-1], last_data_request_size);

  // send
  for(uint32_t i = 0; i < n; i++){
    storage_key = ptr->node2server[i];
    memcached_instance_st *instance = memcached_instance_fetch(ptr, storage_key);

    protocol_binary_request_set request = {};
    size_t send_length = sizeof(request.bytes);

    initialize_binary_request(instance, request.message.header);
    request.message.header.request.opcode= get_com_code(verb, reply);
    request.message.header.request.keylen= htons((uint16_t)(key_length + 2 + memcached_array_size(ptr->_namespace)));
    request.message.header.request.datatype= PROTOCOL_BINARY_RAW_BYTES;
    request.message.header.request.extlen= 8;
    request.message.body.flags= htonl(flags);
    request.message.body.expiration= htonl((uint32_t)expiration);

    if(i != last_data_index) {
      request.message.header.request.bodylen= htonl((uint32_t) (key_length + 2 + memcached_array_size(ptr->_namespace) + block_size +
                                                            request.message.header.request.extlen));
    } else {
      request.message.header.request.bodylen= htonl((uint32_t) (key_length + 2 + memcached_array_size(ptr->_namespace) + last_data_request_size +
                                                            request.message.header.request.extlen));
    }
    if(cas)
    {
      request.message.header.request.cas= memcached_htonll(cas);
    }

    if(i != last_data_index) {
      libmemcached_io_vector_st vector[] = 
      {
        { NULL, 0 },
        { request.bytes, send_length },
        { memcached_array_string(ptr->_namespace),  memcached_array_size(ptr->_namespace) },
        { key_ptrs[i], key_length+2 },
        { all_blocks[i], block_size }
      };
      // write the block
      memcached_return_t rc;
      if((rc= memcached_vdo(instance, vector, 5, flush)) != MEMCACHED_SUCCESS)
      {
        memcached_io_reset(instance);
        #if 0
          if(memcached_has_error(ptr))
          {
            memcached_set_error(*instance, rc, MEMCACHED_AT);
          }
        #endif
        assert(memcached_last_error(instance->root) != MEMCACHED_SUCCESS);
        return memcached_last_error(instance->root);
      }
    } else {
      libmemcached_io_vector_st vector[] = 
      {
        { NULL, 0 },
        { request.bytes, send_length },
        { memcached_array_string(ptr->_namespace),  memcached_array_size(ptr->_namespace) },
        { key_ptrs[i], key_length+2 },
        { last_data_request, last_data_request_size }
      };
      // write the block
      memcached_return_t rc;
      if((rc= memcached_vdo(instance, vector, 5, flush)) != MEMCACHED_SUCCESS)
      {
        memcached_io_reset(instance);
        #if 0
          if (memcached_has_error(ptr))
          {
            memcached_set_error(*instance, rc, MEMCACHED_AT);
          }
        #endif
        assert(memcached_last_error(instance->root) != MEMCACHED_SUCCESS);
        return memcached_last_error(instance->root);
      }
    }
  }

  gettimeofday(&end_time, NULL);
  ptr->transmission_latency = end_time.tv_sec - start_time.tv_sec + (end_time.tv_usec - start_time.tv_usec) * 1.0 / 1000000;

  for(uint32_t i = 0; i < k; i++)
    delete data_ptrs[i];
  delete data_ptrs;
  for(uint32_t i = 0; i < g; i++)
    delete global_coding_ptrs[i];
  delete global_coding_ptrs;

  if (flush == false)
  {
    return MEMCACHED_BUFFERED;
  }

  // No reply always assumes success
  if (reply == false)
  {
    return MEMCACHED_SUCCESS;
  }

  return memcached_response(server, NULL, 0, NULL);
}

static inline memcached_return_t memcached_send(memcached_st *shell,
                                                const char *group_key, size_t group_key_length,
                                                const char *key, size_t key_length,
                                                const char *value, size_t value_length,
                                                const time_t expiration,
                                                const uint32_t flags,
                                                const uint64_t cas,
                                                memcached_storage_action_t verb)
{
  Memcached* ptr= memcached2Memcached(shell);
  memcached_return_t rc;
  if (memcached_failed(rc= initialize_query(ptr, true)))
  {
    return rc;
  }

  if (memcached_failed(memcached_key_test(*ptr, (const char **)&key, &key_length, 1)))
  {
    return memcached_last_error(ptr);
  }

  uint32_t server_key = 0;
  if(ptr->is_lrc){
    server_key= ptr->node2server[ptr->fail_node_index];
  }else{
    server_key= memcached_generate_hash_with_redistribution(ptr, group_key, group_key_length);
  }
  memcached_instance_st* instance= memcached_instance_fetch(ptr, server_key);

  WATCHPOINT_SET(instance->io_wait_count.read= 0);
  WATCHPOINT_SET(instance->io_wait_count.write= 0);

  bool flush= true;
  if (memcached_is_buffering(instance->root) and verb == SET_OP)
  {
    flush= false;
  }

  bool reply= memcached_is_replying(ptr);

  hashkit_string_st* destination= NULL;

  if (memcached_is_encrypted(ptr))
  {
    if (can_by_encrypted(verb) == false)
    {
      return memcached_set_error(*ptr, MEMCACHED_NOT_SUPPORTED, MEMCACHED_AT, 
                                 memcached_literal_param("Operation not allowed while encyrption is enabled"));
    }

    if ((destination= hashkit_encrypt(&ptr->hashkit, value, value_length)) == NULL)
    {
      return rc;
    }
    value= hashkit_string_c_str(destination);
    value_length= hashkit_string_length(destination);
  }

  if (memcached_is_binary(ptr))
  {
    if(verb == SET_OP && ptr->is_lrc && !ptr->is_core && ptr->number_of_b == 0)
    {
      #ifdef STORAGE_DEBUG
      printf("[LRC SEND]\n");
      #endif
      rc = memcached_lrc_send_binary(ptr, instance, server_key,
                                    key, key_length,
                                    value, value_length, expiration,
                                    flags, cas, flush, reply, verb);
    }else if(ptr->is_core){
      #ifdef STORAGE_DEBUG
      printf("[LRC SEND TO NETWORK CORE] %d \n", ptr->network_core_server_key);
      #endif
      server_key = ptr->network_core_server_key;
      instance = memcached_instance_fetch(ptr, server_key);
      rc = memcached_send_binary(ptr, instance, server_key,
                                key, key_length,
                                value, value_length, expiration,
                                flags, cas, flush, reply, verb);
    }else{
      rc = memcached_send_binary(ptr, instance, server_key,
                                key, key_length,
                                value, value_length, expiration,
                                flags, cas, flush, reply, verb);
    }
  }
  else
  {
    rc= memcached_send_ascii(ptr, instance,
                             key, key_length,
                             value, value_length, expiration,
                             flags, cas, flush, reply, verb);
  }

  hashkit_string_free(destination);

  return rc;
}


memcached_return_t memcached_set(memcached_st *ptr, const char *key, size_t key_length,
                                 const char *value, size_t value_length,
                                 time_t expiration,
                                 uint32_t flags)
{
  memcached_return_t rc;
  LIBMEMCACHED_MEMCACHED_SET_START();
  rc= memcached_send(ptr, key, key_length,
                     key, key_length, value, value_length,
                     expiration, flags, 0, SET_OP);
  LIBMEMCACHED_MEMCACHED_SET_END();
  return rc;
}

memcached_return_t memcached_add(memcached_st *ptr,
                                 const char *key, size_t key_length,
                                 const char *value, size_t value_length,
                                 time_t expiration,
                                 uint32_t flags)
{
  memcached_return_t rc;
  LIBMEMCACHED_MEMCACHED_ADD_START();
  rc= memcached_send(ptr, key, key_length,
                     key, key_length, value, value_length,
                     expiration, flags, 0, ADD_OP);

  LIBMEMCACHED_MEMCACHED_ADD_END();
  return rc;
}

memcached_return_t memcached_replace(memcached_st *ptr,
                                     const char *key, size_t key_length,
                                     const char *value, size_t value_length,
                                     time_t expiration,
                                     uint32_t flags)
{
  memcached_return_t rc;
  LIBMEMCACHED_MEMCACHED_REPLACE_START();
  rc= memcached_send(ptr, key, key_length,
                     key, key_length, value, value_length,
                     expiration, flags, 0, REPLACE_OP);
  LIBMEMCACHED_MEMCACHED_REPLACE_END();
  return rc;
}

memcached_return_t memcached_prepend(memcached_st *ptr,
                                     const char *key, size_t key_length,
                                     const char *value, size_t value_length,
                                     time_t expiration,
                                     uint32_t flags)
{
  memcached_return_t rc;
  rc= memcached_send(ptr, key, key_length,
                     key, key_length, value, value_length,
                     expiration, flags, 0, PREPEND_OP);
  return rc;
}

memcached_return_t memcached_append(memcached_st *ptr,
                                    const char *key, size_t key_length,
                                    const char *value, size_t value_length,
                                    time_t expiration,
                                    uint32_t flags)
{
  memcached_return_t rc;
  rc= memcached_send(ptr, key, key_length,
                     key, key_length, value, value_length,
                     expiration, flags, 0, APPEND_OP);
  return rc;
}

memcached_return_t memcached_cas(memcached_st *ptr,
                                 const char *key, size_t key_length,
                                 const char *value, size_t value_length,
                                 time_t expiration,
                                 uint32_t flags,
                                 uint64_t cas)
{
  memcached_return_t rc;
  rc= memcached_send(ptr, key, key_length,
                     key, key_length, value, value_length,
                     expiration, flags, cas, CAS_OP);
  return rc;
}

memcached_return_t memcached_set_by_key(memcached_st *ptr,
                                        const char *group_key,
                                        size_t group_key_length,
                                        const char *key, size_t key_length,
                                        const char *value, size_t value_length,
                                        time_t expiration,
                                        uint32_t flags)
{
  memcached_return_t rc;
  LIBMEMCACHED_MEMCACHED_SET_START();
  rc= memcached_send(ptr, group_key, group_key_length,
                     key, key_length, value, value_length,
                     expiration, flags, 0, SET_OP);
  LIBMEMCACHED_MEMCACHED_SET_END();
  return rc;
}

memcached_return_t memcached_add_by_key(memcached_st *ptr,
                                        const char *group_key, size_t group_key_length,
                                        const char *key, size_t key_length,
                                        const char *value, size_t value_length,
                                        time_t expiration,
                                        uint32_t flags)
{
  memcached_return_t rc;
  LIBMEMCACHED_MEMCACHED_ADD_START();
  rc= memcached_send(ptr, group_key, group_key_length,
                     key, key_length, value, value_length,
                     expiration, flags, 0, ADD_OP);
  LIBMEMCACHED_MEMCACHED_ADD_END();
  return rc;
}

memcached_return_t memcached_replace_by_key(memcached_st *ptr,
                                            const char *group_key, size_t group_key_length,
                                            const char *key, size_t key_length,
                                            const char *value, size_t value_length,
                                            time_t expiration,
                                            uint32_t flags)
{
  memcached_return_t rc;
  LIBMEMCACHED_MEMCACHED_REPLACE_START();
  rc= memcached_send(ptr, group_key, group_key_length,
                     key, key_length, value, value_length,
                     expiration, flags, 0, REPLACE_OP);
  LIBMEMCACHED_MEMCACHED_REPLACE_END();
  return rc;
}

memcached_return_t memcached_prepend_by_key(memcached_st *ptr,
                                            const char *group_key, size_t group_key_length,
                                            const char *key, size_t key_length,
                                            const char *value, size_t value_length,
                                            time_t expiration,
                                            uint32_t flags)
{
  return memcached_send(ptr, group_key, group_key_length,
                        key, key_length, value, value_length,
                        expiration, flags, 0, PREPEND_OP);
}

memcached_return_t memcached_append_by_key(memcached_st *ptr,
                                           const char *group_key, size_t group_key_length,
                                           const char *key, size_t key_length,
                                           const char *value, size_t value_length,
                                           time_t expiration,
                                           uint32_t flags)
{
  return memcached_send(ptr, group_key, group_key_length,
                        key, key_length, value, value_length,
                        expiration, flags, 0, APPEND_OP);
}

memcached_return_t memcached_cas_by_key(memcached_st *ptr,
                                        const char *group_key, size_t group_key_length,
                                        const char *key, size_t key_length,
                                        const char *value, size_t value_length,
                                        time_t expiration,
                                        uint32_t flags,
                                        uint64_t cas)
{
  return  memcached_send(ptr, group_key, group_key_length,
                         key, key_length, value, value_length,
                         expiration, flags, cas, CAS_OP);
}

