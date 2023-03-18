/*  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 * 
 *  Libmemcached library
 *
 *  Copyright (C) 2011-2013 Data Differential, http://datadifferential.com/
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
extern "C"{
#include "../Jerasure-1.2A/cauchy.h"
#include "../Jerasure-1.2A/reed_sol.h"
#include "../Jerasure-1.2A/jerasure.h"
#include "../Jerasure-1.2A/galois.h"
}
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
// #define GET_DEBUG

//lrc add
//decoding
static void decoding_parity_ptrs_lrc(char **data_ptrs, uint32_t k, char **coding_ptrs, uint32_t m, size_t block_size,int *erasures) {
  int *matrix = reed_sol_vandermonde_coding_matrix(k, m, 8);
  int i = 0;
  i = jerasure_matrix_decode(k, m, 8, matrix, 1, erasures, data_ptrs, coding_ptrs, block_size);
  if (i == -1) {
      printf("Unsuccessful!\n");
  }
}
//encoding
static void calculate_parity_ptrs_rs(char** data_ptrs, uint32_t k, char** coding_ptrs, uint32_t m, size_t block_size)
{
  int* matrix = reed_sol_vandermonde_coding_matrix(k, m, 8);
  jerasure_matrix_encode(k, m, 8, matrix, data_ptrs, coding_ptrs, block_size);
  free(matrix);
}
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
//simulate cross-cluster transfering, set and get by once respectively
static void cross_networkcore_libmemcached(Memcached *ptr, const char *key, size_t key_length,
                                 const char *value, size_t value_length)
{
  size_t val_len = value_length;
  uint32_t flags = 0;
  memcached_return_t rc;
  ptr->is_core = true;

  rc = memcached_set(ptr, key, key_length, value, value_length, 86400, 0);
  if(rc != MEMCACHED_SUCCESS){
    printf("[NETWORK CORE] Set key failure! %s!\n", memcached_strerror(ptr, rc));
  }
  char *result = memcached_get_by_key(ptr, NULL, 0, key, key_length, &val_len, &flags, &rc);
  if(rc != MEMCACHED_SUCCESS){
    printf("[NETWORK CORE] Get value failure! %s!\n", memcached_strerror(ptr, rc));
  }

  ptr->is_core = false;
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
//get k data blocks to form the original data
//if there is a failed block, then repair it with other blocks in the same group (as degraded read)
//assume that there is no more than one failed block any time
static char *memcached_get_lrc(memcached_st *ptr, const char *key,
                    size_t key_length,
                    size_t *value_length,
                    uint32_t *flags,
                    memcached_return_t *error)
{ 
  uint32_t n = ptr->number_of_n;
  uint32_t k = ptr->number_of_k;
  uint32_t r = ptr->number_of_r;
  uint32_t l = ceil(double(k) / double(r));
  if(ptr->lrc_scheme == MEMCACHED_LRC_AZURE_1){
    l += 1;
  }
  uint32_t g = n - k - l;
  char **key_ptrs = new char*[k];
  generate_new_keys_rs(key_ptrs, k, key, key_length);
  size_t *key_len_ptrs = new size_t[k];
  for(uint32_t i = 0; i < k; i++)
    key_len_ptrs[i] = key_length + 2;

  memcached_return_t unused;
  if (error == NULL)
  {
    error= &unused;
  }

  uint32_t fail_nodes[k] = {0}; //record the failed node with 0
  int fail_index = -1;
  int *erasures = new int[r + 1];
  size_t block_size = determine_block_size_rs(*value_length, k);
  size_t last_block_size = *value_length - block_size * (k - 1);
  size_t res_value_length = 0;
  char **res_values = new char*[k];

  //get all the data blocks from a stripe and check if there is any failed block
  *error = memcached_mget(ptr, (const char * const *)key_ptrs, key_len_ptrs, k);
  if(*error == MEMCACHED_SUCCESS){
    memcached_result_st *result_buffer = &ptr->result;
    char *temp_key = new char[key_length + 3];
    for(uint32_t i = 0; i < k; i++){
      if((result_buffer = memcached_fetch_result(ptr, result_buffer, error)) != NULL){
        strncpy(temp_key, result_buffer->item_key, result_buffer->key_length);
        temp_key[key_length + 2] = '\0';
        uint32_t j;
        for(j = 0; j < k; j++){
          if(strcmp(temp_key, key_ptrs[j]) == 0)
            break;
        }
        fail_nodes[j] = 1;
        if(j == 0){
          block_size = memcached_result_length(result_buffer);
        }else if(i == k - 1){
          last_block_size = memcached_result_length(result_buffer);
        }
        res_value_length += memcached_result_length(result_buffer);

        res_values[j] = new char[block_size];
        memset(res_values[j], 0, block_size);
        memcpy(res_values[j], memcached_result_value(result_buffer), memcached_result_length(result_buffer));
        if(flags){
          *flags = result_buffer->item_flags;
        }
      }else{
        printf("%s\n", memcached_strerror(ptr, *error));
      }
    }
    //only consider single-node failure
    for(uint32_t i = 0; i < k; i++){
      if(fail_nodes[i] == 0){
        fail_index = i;
        erasures[0] = fail_index - int(fail_index / r) * r;
        break;
      }
    }

    //simulate fetch result across the network core
    for(uint32_t i = 0; i < k; i++){
      if(i != fail_index){
        cross_networkcore_libmemcached(ptr, key_ptrs[i], key_len_ptrs[i],
                                      res_values[i], block_size);
      }
    }
    if(fail_index != -1){ //there is one data block failure
      ptr->fail_node_index = fail_index;
      uint32_t group_id = int(fail_index / r);
      uint32_t repair_request_number = r + 1;
      uint32_t last_block_index = k - 1;
      if((group_id + 1) * r > k){
        repair_request_number = k - group_id * r + 1;
      }

      //get other data blocks of this local group
      res_values[fail_index] = new char[block_size];
      memset(res_values[fail_index], 0 ,block_size);
      char **data_ptrs = new char*[repair_request_number - 1];
      uint32_t cross_rack_num = 0;
      uint32_t start_index = group_id * r;
      uint32_t end_index = start_index + repair_request_number - 1;
      uint32_t cur_rack = ptr->node2rack[start_index];
      for(uint32_t i = start_index, j = 0; i < end_index; i++, j++)
      {
        if(cur_rack != ptr->node2rack[i]){
          cross_rack_num++;
          cur_rack = ptr->node2rack[i];
        }
        data_ptrs[j] = new char[block_size];
        memset(data_ptrs[j], 0, block_size);
        memcpy(data_ptrs[j], res_values[i], block_size);
      }
      //get the local parity block of this local group
      uint32_t local_parity_index = k + g + group_id;
      if(cur_rack != ptr->node2rack[local_parity_index]){
        cross_rack_num++;
      }
      char **local_parity_key_ptrs = new char*[1];
      size_t *local_parity_key_len_ptrs = new size_t[1];
      local_parity_key_ptrs[0] = new char[key_length + 3];
      memset(local_parity_key_ptrs[0], 0, key_length + 3);
      memcpy(local_parity_key_ptrs[0], key, key_length);
      char ten_bit_char = '0' + local_parity_index / 10;
      char one_bit_char = '0' + local_parity_index % 10;
      local_parity_key_ptrs[0][key_length] = ten_bit_char;
      local_parity_key_ptrs[0][key_length + 1] = one_bit_char;
      local_parity_key_ptrs[0][key_length + 2] = '\0';
      local_parity_key_len_ptrs[0] = key_length + 2;
      char **coding_ptrs = new char*[1];
      *error = memcached_mget(ptr, (const char * const *)local_parity_key_ptrs, local_parity_key_len_ptrs, 1);
      if(*error == MEMCACHED_SUCCESS){
        if((result_buffer = memcached_fetch_result(ptr, result_buffer, error)) != NULL){
          coding_ptrs[0] = new char[memcached_result_length(result_buffer)];
          memset(coding_ptrs[0], 0, block_size);
          memcpy(coding_ptrs[0], memcached_result_value(result_buffer), memcached_result_length(result_buffer));
          if(flags){
            *flags= result_buffer->item_flags;
          }
        }
      }
      //simulate transmission of cross networkcore
      if(cross_rack_num != 0){
        for(uint32_t i = 0; i < cross_rack_num; i++){
          cross_networkcore_libmemcached(ptr, local_parity_key_ptrs[0], local_parity_key_len_ptrs[0], coding_ptrs[0], block_size);
        }
      }
      #ifdef GET_DEBUG
      printf("[GET DEBUG] Failed block: %d, cross %d racks", fail_index, cross_rack_num);
      #endif
      //decode to repair the failed block
      erasures[1] = -1;
      data_ptrs[erasures[0]] = new char[block_size];
      memset(data_ptrs[erasures[0]], 0, block_size);
      decoding_parity_ptrs_lrc(data_ptrs, repair_request_number - 1, coding_ptrs, 1, block_size, erasures);
      memcpy(res_values[fail_index], data_ptrs[erasures[0]], block_size);

      //store the repaired failed block
      if(fail_index == k - 1){
        memcached_set(ptr, key_ptrs[fail_index], key_len_ptrs[fail_index],
                    res_values[fail_index], last_block_size, 86400, 0);
      }else{
        memcached_set(ptr, key_ptrs[fail_index], key_len_ptrs[fail_index],
                    res_values[fail_index], block_size, 86400, 0);
      }

      delete local_parity_key_ptrs[0];
      delete local_parity_key_ptrs;
      delete local_parity_key_len_ptrs;

      //integrate result
      *error = MEMCACHED_SUCCESS;
      char* temp_value = new char[*value_length];
      for(uint32_t i = 0; i < k; i++){
        if(i < k - 1){
          memcpy(temp_value + i * block_size, res_values[i], block_size);
        }else{
          memcpy(temp_value + i * block_size, res_values[i], last_block_size);
        }
      }
      delete coding_ptrs[0];
      delete coding_ptrs;
      //free space
      if(res_values != NULL){
        for(uint32_t i = 0; i < k; i++){
          if(res_values[i] != NULL){
            delete res_values[i];
          }
        }
        delete res_values;
      }
      if(key_ptrs != NULL){
        for(uint32_t i = 0; i < k; i++){
          if(key_ptrs[i] != NULL){
            delete key_ptrs[i];
          }
        }
        delete key_ptrs;
      }
      if(key_len_ptrs != NULL){
        delete key_len_ptrs;
      }
      if(erasures != NULL){
        delete erasures;
      }
      delete temp_key;
      return temp_value;
    }else{ // there is no failed block, directly integrate result
      char* temp_value = new char[*value_length];
      for(uint32_t i = 0; i < k; i++){
        if(i < k - 1){
          memcpy(temp_value + i * block_size, res_values[i], block_size);
        }else{
          memcpy(temp_value + i * block_size, res_values[i], last_block_size);
        }
      }
      //free
      if(res_values != NULL){
        for(uint32_t i = 0; i < k; i++){
          if(res_values[i] != NULL){
            delete res_values[i];
          }
        }
        delete res_values;
      }
      if(key_ptrs != NULL){
        for(uint32_t i = 0; i < k; i++){
          if(key_ptrs[i] != NULL){
            delete key_ptrs[i];
          }
        }
        delete key_ptrs;
      }
      if(key_len_ptrs != NULL){
        delete key_len_ptrs;
      }
      if(erasures != NULL){
        delete erasures;
      }
      delete temp_key;
      return temp_value;
    }
  }else{//can't successfully get data
    if(res_values != NULL){
      delete res_values;
    }
    if(key_ptrs != NULL){
      for(uint32_t i = 0; i < k; i++){
        if(key_ptrs[i] != NULL){
          delete key_ptrs[i];
        }
      }
      delete key_ptrs;
    }
    if(key_len_ptrs != NULL){
      delete key_len_ptrs;
    }
    if(erasures != NULL){
      delete erasures;
    }
  }
  return NULL;
}

/*
  What happens if no servers exist?
*/
char *memcached_get(memcached_st *ptr, const char *key,
                    size_t key_length,
                    size_t *value_length,
                    uint32_t *flags,
                    memcached_return_t *error)
{
  if(ptr->is_lrc && !ptr->is_core){
    #ifdef GET_DEBUG
    printf("[LRC GET]\n");
    #endif
    return memcached_get_lrc(ptr, key, key_length, value_length, flags, error);
  }
  return memcached_get_by_key(ptr, NULL, 0, key, key_length, value_length,
                              flags, error);
}

static memcached_return_t __mget_by_key_real(memcached_st *ptr,
                                             const char *group_key,
                                             size_t group_key_length,
                                             const char * const *keys,
                                             const size_t *key_length,
                                             size_t number_of_keys,
                                             const bool mget_mode);
char *memcached_get_by_key(memcached_st *shell,
                           const char *group_key,
                           size_t group_key_length,
                           const char *key, size_t key_length,
                           size_t *value_length,
                           uint32_t *flags,
                           memcached_return_t *error)
{
  Memcached* ptr= memcached2Memcached(shell);
  memcached_return_t unused;
  if (error == NULL)
  {
    error= &unused;
  }

  uint64_t query_id= 0;
  if (ptr)
  {
    query_id= ptr->query_id;
  }

  /* Request the key */
  *error= __mget_by_key_real(ptr, group_key, group_key_length,
                             (const char * const *)&key, &key_length, 
                             1, false);
  if (ptr)
  {
    assert_msg(ptr->query_id == query_id +1, "Programmer error, the query_id was not incremented.");
  }

  if (memcached_failed(*error))
  {
    if (ptr)
    {
      if (memcached_has_current_error(*ptr)) // Find the most accurate error
      {
        *error= memcached_last_error(ptr);
      }
    }

    if (value_length) 
    {
      *value_length= 0;
    }

    return NULL;
  }

  char *value= memcached_fetch(ptr, NULL, NULL,
                               value_length, flags, error);
  assert_msg(ptr->query_id == query_id +1, "Programmer error, the query_id was not incremented.");

  /* This is for historical reasons */
  if (*error == MEMCACHED_END)
  {
    *error= MEMCACHED_NOTFOUND;
  }
  if (value == NULL)
  {
    if (ptr->get_key_failure and *error == MEMCACHED_NOTFOUND)
    {
      memcached_result_st key_failure_result;
      memcached_result_st* result_ptr= memcached_result_create(ptr, &key_failure_result);
      memcached_return_t rc= ptr->get_key_failure(ptr, key, key_length, result_ptr);

      /* On all failure drop to returning NULL */
      if (rc == MEMCACHED_SUCCESS or rc == MEMCACHED_BUFFERED)
      {
        if (rc == MEMCACHED_BUFFERED)
        {
          uint64_t latch; /* We use latch to track the state of the original socket */
          latch= memcached_behavior_get(ptr, MEMCACHED_BEHAVIOR_BUFFER_REQUESTS);
          if (latch == 0)
          {
            memcached_behavior_set(ptr, MEMCACHED_BEHAVIOR_BUFFER_REQUESTS, 1);
          }

          rc= memcached_set(ptr, key, key_length,
                            (memcached_result_value(result_ptr)),
                            (memcached_result_length(result_ptr)),
                            0,
                            (memcached_result_flags(result_ptr)));

          if (rc == MEMCACHED_BUFFERED and latch == 0)
          {
            memcached_behavior_set(ptr, MEMCACHED_BEHAVIOR_BUFFER_REQUESTS, 0);
          }
        }
        else
        {
          rc= memcached_set(ptr, key, key_length,
                            (memcached_result_value(result_ptr)),
                            (memcached_result_length(result_ptr)),
                            0,
                            (memcached_result_flags(result_ptr)));
        }

        if (rc == MEMCACHED_SUCCESS or rc == MEMCACHED_BUFFERED)
        {
          *error= rc;
          *value_length= memcached_result_length(result_ptr);
          *flags= memcached_result_flags(result_ptr);
          char *result_value=  memcached_string_take_value(&result_ptr->value);
          memcached_result_free(result_ptr);

          return result_value;
        }
      }

      memcached_result_free(result_ptr);
    }
    assert_msg(ptr->query_id == query_id +1, "Programmer error, the query_id was not incremented.");

    return NULL;
  }

  return value;
}

memcached_return_t memcached_mget(memcached_st *ptr,
                                  const char * const *keys,
                                  const size_t *key_length,
                                  size_t number_of_keys)
{
  return memcached_mget_by_key(ptr, NULL, 0, keys, key_length, number_of_keys);
}

static memcached_return_t binary_mget_by_key(memcached_st *ptr,
                                             const uint32_t master_server_key,
                                             const bool is_group_key_set,
                                             const char * const *keys,
                                             const size_t *key_length,
                                             const size_t number_of_keys,
                                             const bool mget_mode);

static memcached_return_t __mget_by_key_real(memcached_st *ptr,
                                             const char *group_key,
                                             const size_t group_key_length,
                                             const char * const *keys,
                                             const size_t *key_length,
                                             size_t number_of_keys,
                                             const bool mget_mode)
{
  bool failures_occured_in_sending= false;
  const char *get_command= "get";
  uint8_t get_command_length= 3;
  unsigned int master_server_key= (unsigned int)-1; /* 0 is a valid server id! */

  memcached_return_t rc;
  if (memcached_failed(rc= initialize_query(ptr, true)))
  {
    return rc;
  }

  if (memcached_is_udp(ptr))
  {
    return memcached_set_error(*ptr, MEMCACHED_NOT_SUPPORTED, MEMCACHED_AT);
  }

  LIBMEMCACHED_MEMCACHED_MGET_START();

  if (number_of_keys == 0)
  {
    return memcached_set_error(*ptr, MEMCACHED_INVALID_ARGUMENTS, MEMCACHED_AT, memcached_literal_param("Numbers of keys provided was zero"));
  }

  if (memcached_failed((rc= memcached_key_test(*ptr, keys, key_length, number_of_keys))))
  {
    assert(memcached_last_error(ptr) == rc);

    return rc;
  }

  bool is_group_key_set= false;
  if (group_key and group_key_length)
  {
    master_server_key= memcached_generate_hash_with_redistribution(ptr, group_key, group_key_length);
    is_group_key_set= true;
  }

  /*
    Here is where we pay for the non-block API. We need to remove any data sitting
    in the queue before we start our get.

    It might be optimum to bounce the connection if count > some number.
  */
  for (uint32_t x= 0; x < memcached_server_count(ptr); x++)
  {
    memcached_instance_st* instance= memcached_instance_fetch(ptr, x);

    if (instance->response_count())
    {
      char buffer[MEMCACHED_DEFAULT_COMMAND_SIZE];

      if (ptr->flags.no_block)
      {
        memcached_io_write(instance);
      }

      while(instance->response_count())
      {
        (void)memcached_response(instance, buffer, MEMCACHED_DEFAULT_COMMAND_SIZE, &ptr->result);
      }
    }
  }

  if (memcached_is_binary(ptr))
  {
    return binary_mget_by_key(ptr, master_server_key, is_group_key_set, keys,
                              key_length, number_of_keys, mget_mode);
  }

  if (ptr->flags.support_cas)
  {
    get_command= "gets";
    get_command_length= 4;
  }

  /*
    If a server fails we warn about errors and start all over with sending keys
    to the server.
  */
  WATCHPOINT_ASSERT(rc == MEMCACHED_SUCCESS);
  size_t hosts_connected= 0;
  for (uint32_t x= 0; x < number_of_keys; x++)
  {
    uint32_t server_key;

    if (is_group_key_set)
    {
      server_key= master_server_key;
    }
    else
    {
      server_key= memcached_generate_hash_with_redistribution(ptr, keys[x], key_length[x]);
    }

    memcached_instance_st* instance= memcached_instance_fetch(ptr, server_key);

    libmemcached_io_vector_st vector[]=
    {
      { get_command, get_command_length },
      { memcached_literal_param(" ") },
      { memcached_array_string(ptr->_namespace), memcached_array_size(ptr->_namespace) },
      { keys[x], key_length[x] }
    };


    if (instance->response_count() == 0)
    {
      rc= memcached_connect(instance);

      if (memcached_failed(rc))
      {
        memcached_set_error(*instance, rc, MEMCACHED_AT);
        continue;
      }
      hosts_connected++;

      if ((memcached_io_writev(instance, vector, 1, false)) == false)
      {
        failures_occured_in_sending= true;
        continue;
      }
      WATCHPOINT_ASSERT(instance->cursor_active_ == 0);
      memcached_instance_response_increment(instance);
      WATCHPOINT_ASSERT(instance->cursor_active_ == 1);
    }

    {
      if ((memcached_io_writev(instance, (vector + 1), 3, false)) == false)
      {
        memcached_instance_response_reset(instance);
        failures_occured_in_sending= true;
        continue;
      }
    }
  }

  if (hosts_connected == 0)
  {
    LIBMEMCACHED_MEMCACHED_MGET_END();

    if (memcached_failed(rc))
    {
      return rc;
    }

    return memcached_set_error(*ptr, MEMCACHED_NO_SERVERS, MEMCACHED_AT);
  }


  /*
    Should we muddle on if some servers are dead?
  */
  bool success_happened= false;
  for (uint32_t x= 0; x < memcached_server_count(ptr); x++)
  {
    memcached_instance_st* instance= memcached_instance_fetch(ptr, x);

    if (instance->response_count())
    {
      /* We need to do something about non-connnected hosts in the future */
      if ((memcached_io_write(instance, "\r\n", 2, true)) == -1)
      {
        failures_occured_in_sending= true;
      }
      else
      {
        success_happened= true;
      }
    }
  }

  LIBMEMCACHED_MEMCACHED_MGET_END();

  if (failures_occured_in_sending and success_happened)
  {
    return MEMCACHED_SOME_ERRORS;
  }

  if (success_happened)
  {
    return MEMCACHED_SUCCESS;
  }

  return MEMCACHED_FAILURE; // Complete failure occurred
}

memcached_return_t memcached_mget_by_key(memcached_st *shell,
                                         const char *group_key,
                                         size_t group_key_length,
                                         const char * const *keys,
                                         const size_t *key_length,
                                         size_t number_of_keys)
{
  Memcached* ptr= memcached2Memcached(shell);
  return __mget_by_key_real(ptr, group_key, group_key_length, keys, key_length, number_of_keys, true);
}

memcached_return_t memcached_mget_execute(memcached_st *ptr,
                                          const char * const *keys,
                                          const size_t *key_length,
                                          size_t number_of_keys,
                                          memcached_execute_fn *callback,
                                          void *context,
                                          unsigned int number_of_callbacks)
{
  return memcached_mget_execute_by_key(ptr, NULL, 0, keys, key_length,
                                       number_of_keys, callback,
                                       context, number_of_callbacks);
}

memcached_return_t memcached_mget_execute_by_key(memcached_st *shell,
                                                 const char *group_key,
                                                 size_t group_key_length,
                                                 const char * const *keys,
                                                 const size_t *key_length,
                                                 size_t number_of_keys,
                                                 memcached_execute_fn *callback,
                                                 void *context,
                                                 unsigned int number_of_callbacks)
{
  Memcached* ptr= memcached2Memcached(shell);
  memcached_return_t rc;
  if (memcached_failed(rc= initialize_query(ptr, false)))
  {
    return rc;
  }

  if (memcached_is_udp(ptr))
  {
    return memcached_set_error(*ptr, MEMCACHED_NOT_SUPPORTED, MEMCACHED_AT);
  }

  if (memcached_is_binary(ptr) == false)
  {
    return memcached_set_error(*ptr, MEMCACHED_NOT_SUPPORTED, MEMCACHED_AT,
                               memcached_literal_param("ASCII protocol is not supported for memcached_mget_execute_by_key()"));
  }

  memcached_callback_st *original_callbacks= ptr->callbacks;
  memcached_callback_st cb= {
    callback,
    context,
    number_of_callbacks
  };

  ptr->callbacks= &cb;
  rc= memcached_mget_by_key(ptr, group_key, group_key_length, keys,
                            key_length, number_of_keys);
  ptr->callbacks= original_callbacks;

  return rc;
}

static memcached_return_t simple_binary_mget(memcached_st *ptr,
                                             const uint32_t master_server_key,
                                             bool is_group_key_set,
                                             const char * const *keys,
                                             const size_t *key_length,
                                             const size_t number_of_keys, const bool mget_mode)
{
  memcached_return_t rc= MEMCACHED_NOTFOUND;

  bool flush= (number_of_keys == 1);

  if (memcached_failed(rc= memcached_key_test(*ptr, keys, key_length, number_of_keys)))
  {
    return rc;
  }

  /*
    If a server fails we warn about errors and start all over with sending keys
    to the server.
  */
  for (uint32_t x= 0; x < number_of_keys; ++x)
  {
    uint32_t server_key;

    if(number_of_keys > 1 && ptr->is_lrc && !ptr->is_core){
      int one = int(keys[x][key_length[x] - 1]) - '0';
      int ten = int(keys[x][key_length[x] - 2]) - '0';
      uint32_t index = ten * 10 + one;
      server_key = ptr->node2server[index];
      #ifdef GET_DEBUG
      printf("[LRC GET] index %d, server %d \n", index, server_key);
      #endif
    }else if(ptr->is_core){
      server_key = ptr->network_core_server_key;
      #ifdef GET_DEBUG
      printf("[LRC GET] network core server %d \n", server_key);
      #endif
    }else{
      if(is_group_key_set)
      {
        server_key= master_server_key;
      }
      else
      {
        server_key= memcached_generate_hash_with_redistribution(ptr, keys[x], key_length[x]);
      }
    }      

    memcached_instance_st* instance= memcached_instance_fetch(ptr, server_key);

    if (instance->response_count() == 0)
    {
      rc= memcached_connect(instance);
      if (memcached_failed(rc))
      {
        continue;
      }
    }

    protocol_binary_request_getk request= { }; //= {.bytes= {0}};
    initialize_binary_request(instance, request.message.header);
    if (mget_mode)
    {
      request.message.header.request.opcode= PROTOCOL_BINARY_CMD_GETKQ;
    }
    else
    {
      request.message.header.request.opcode= PROTOCOL_BINARY_CMD_GETK;
    }

#if 0
    {
      memcached_return_t vk= memcached_validate_key_length(key_length[x], ptr->flags.binary_protocol);
      if (memcached_failed(rc= memcached_key_test(*memc, (const char **)&key, &key_length, 1)))
      {
        memcached_set_error(ptr, vk, MEMCACHED_AT, memcached_literal_param("Key was too long."));

        if (x > 0)
        {
          memcached_io_reset(instance);
        }

        return vk;
      }
    }
#endif

    request.message.header.request.keylen= htons((uint16_t)(key_length[x] + memcached_array_size(ptr->_namespace)));
    request.message.header.request.datatype= PROTOCOL_BINARY_RAW_BYTES;
    request.message.header.request.bodylen= htonl((uint32_t)( key_length[x] + memcached_array_size(ptr->_namespace)));

    libmemcached_io_vector_st vector[]=
    {
      { request.bytes, sizeof(request.bytes) },
      { memcached_array_string(ptr->_namespace), memcached_array_size(ptr->_namespace) },
      { keys[x], key_length[x] }
    };

    if (memcached_io_writev(instance, vector, 3, flush) == false)
    {
      memcached_server_response_reset(instance);
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }

    /* We just want one pending response per server */
    memcached_server_response_reset(instance);
    memcached_server_response_increment(instance);
    if ((x > 0 and x == ptr->io_key_prefetch) and memcached_flush_buffers(ptr) != MEMCACHED_SUCCESS)
    {
      rc= MEMCACHED_SOME_ERRORS;
    }
  }

  if (mget_mode)
  {
    /*
      Send a noop command to flush the buffers
    */
    protocol_binary_request_noop request= {}; //= {.bytes= {0}};
    request.message.header.request.opcode= PROTOCOL_BINARY_CMD_NOOP;
    request.message.header.request.datatype= PROTOCOL_BINARY_RAW_BYTES;

    for (uint32_t x= 0; x < memcached_server_count(ptr); ++x)
    {
      memcached_instance_st* instance= memcached_instance_fetch(ptr, x);

      if (instance->response_count())
      {
        initialize_binary_request(instance, request.message.header);
        if ((memcached_io_write(instance) == false) or
            (memcached_io_write(instance, request.bytes, sizeof(request.bytes), true) == -1))
        {
          memcached_instance_response_reset(instance);
          memcached_io_reset(instance);
          rc= MEMCACHED_SOME_ERRORS;
        }
      }
    }
  }

  return rc;
}

static char *memcached_repair_by_index(memcached_st *ptr, const char *key,
                                        size_t key_length,
                                        size_t value_length,
                                        uint32_t *flags,
                                        memcached_return_t *error,uint32_t fail_index,uint32_t inner)
{
  uint32_t n = ptr->number_of_n;
  uint32_t k = ptr->number_of_k;
  uint32_t r = ptr->number_of_r;
  uint32_t l = ceil(double(k) / double(r));
  if(ptr->lrc_scheme == MEMCACHED_LRC_AZURE_1){
    l += 1;
  }
  uint32_t g = n - k - l;
  uint32_t group_id = ptr->node2group[fail_index];
  uint32_t group_number = 0;
  uint32_t group_indexs[k+1];
  int *erasures = new int[r+1];
  // if Azure_LRC and global parity block failed, fetch k data blocks to repair 
  if(ptr->lrc_scheme == MEMCACHED_LRC_AZURE && fail_index >= k && fail_index < k + g){
    for(uint32_t i = 0; i < n; i++)
    {
      if(i < k || i == fail_index)
        group_indexs[group_number++] = i;
    }
    erasures[0] = group_number - 1;
  }else{
    for(uint32_t i = 0; i < n; i++)
    {
      if(ptr->node2group[i] == group_id){
        if(i == fail_index)
          erasures[0] = group_number;
        group_indexs[group_number++] = i;
      }
    }
  }
  erasures[1] = -1;
  #ifdef GET_DEBUG
  printf("(n, k, r, l, g) %d, %d, %d, %d, %d\n", n, k, r, l, g);
  printf("[Repair] repair block %d, group id %d, group number %d.\n", fail_index, group_id, group_number);
  #endif

  char **key_ptrs = new char*[group_number];
  size_t *key_len_ptrs = new size_t[group_number];
  //generate new keys for each block
  for(uint32_t i = 0; i < group_number; i++){
    key_ptrs[i] = new char[key_length+3];
    memset(key_ptrs[i], 0, key_length+3);
    memcpy(key_ptrs[i], key, key_length);
    int ten = group_indexs[i] / 10;
    int one = group_indexs[i] % 10;
    char ten_bit_char = '0' + ten;
    char one_bit_char = '0' + one;
    key_ptrs[i][key_length] = ten_bit_char;
    key_ptrs[i][key_length+1] = one_bit_char;
    key_ptrs[i][key_length+2] = '\0';
    key_len_ptrs[i] = key_length + 2;
  }

  memcached_return_t unused;
  if(error == NULL){
    error = &unused;
  }
  
  char **sub_values = new char*[group_number-1];
  char **coding_ptrs = new char*[1];
  size_t block_size = determine_block_size_rs(value_length, k);
  size_t last_data_request_size = value_length - block_size * (k - 1);
  uint32_t cross_rack_num = 0;
  uint32_t first_rack = ptr->node2rack[fail_index];

  //get the blocks in the same local group
  struct timeval start_time, end_time;
  *error = memcached_mget(ptr, (const char * const *)key_ptrs, key_len_ptrs, group_number);
  if(*error == MEMCACHED_SUCCESS) {
    memcached_result_st *result_buffer = &ptr->result;
    char *temp_key = new char[key_length + 3];
    for(uint32_t i = 0; i < group_number; i++)
    {
      if(first_rack != ptr->node2rack[group_indexs[i]]){
        cross_rack_num++;
        first_rack = ptr->node2rack[group_indexs[i]];
      }
      if((result_buffer = memcached_fetch_result(ptr, result_buffer, error)) != NULL){
        strncpy(temp_key, result_buffer->item_key, result_buffer->key_length);
        temp_key[key_length+2] = '\0';
        uint32_t j;
        for(j = 0; j < group_number; j++){
          if(strcmp(temp_key, key_ptrs[j]) == 0)
            break;
        }
        if(group_indexs[j] == k - 1){
          last_data_request_size = memcached_result_length(result_buffer);
        }else{
          block_size = memcached_result_length(result_buffer);
        }
        if(j == group_number - 1){
          coding_ptrs[0] = new char[block_size];
          memset(coding_ptrs[0], 0, block_size);
          coding_ptrs[0][block_size] = '\0';
          memcpy(coding_ptrs[0], memcached_result_value(result_buffer), memcached_result_length(result_buffer));
        }else{
          sub_values[j] = new char[block_size];
          memset(sub_values[j], 0, block_size);
          sub_values[j][block_size] = '\0';
          memcpy(sub_values[j], memcached_result_value(result_buffer), memcached_result_length(result_buffer));
        }
        if(flags){
          *flags = result_buffer->item_flags;
        }
      }else{
        printf("%s\n", memcached_strerror(ptr, *error));
      }
    }
    #ifdef GET_DEBUG
    printf("Group value:\n");
    for(uint32_t i = 0; i < group_number - 1; i++){
      printf("key:%s, value:%s\n",key_ptrs[i], sub_values[i]);
    }
    printf("key:%s, value:%s\n",key_ptrs[group_number - 1], coding_ptrs[0]);
    printf("cross_rack_num:%d\n", cross_rack_num);
    #endif
    
    gettimeofday(&start_time, NULL);
    if(cross_rack_num != 0 && inner){
      for(uint32_t i = 0; i < cross_rack_num; i++){
        cross_networkcore_libmemcached(ptr, key_ptrs[0], key_len_ptrs[0], sub_values[0], block_size);
      }
    }
    gettimeofday(&end_time, NULL);
    ptr->repair_transmission_latency = end_time.tv_sec - start_time.tv_sec + (end_time.tv_usec - start_time.tv_usec) * 1.0 / 1000000;
    
    if(erasures[0] == group_number - 1){
      coding_ptrs[0] = new char[block_size];
      memset(coding_ptrs[0], 0, block_size);
      coding_ptrs[0][block_size] = '\0';
    }else{
      sub_values[erasures[0]] = new char[block_size];
      memset(sub_values[erasures[0]], 0, block_size);
      sub_values[erasures[0]][block_size] = '\0';
    }
    // printf("failed_index: %d\n", fail_index);
    // for(uint32_t i = 0; i < group_number; i++){
    //   printf("%d ", group_indexs[i]);
    // }
    // printf("\n");

    //decoding and repairing
    if(ptr->lrc_scheme == MEMCACHED_LRC_AZURE && fail_index >= k && fail_index < k + g){
      char **global_coding_ptrs = new char*[g];
      for(uint32_t i = 0; i < g; i++){
        global_coding_ptrs[i] = new char[block_size];
        memset(global_coding_ptrs[i], 0, block_size);
        global_coding_ptrs[i][block_size] = '\0';
      }
      calculate_parity_ptrs_rs(sub_values, k, global_coding_ptrs, g, block_size);
      memcpy(coding_ptrs[0], global_coding_ptrs[fail_index-k], block_size);
      for(uint32_t i = 0; i < g; i++){
        delete global_coding_ptrs[i];
      }
      delete global_coding_ptrs;
    }else{
      decoding_parity_ptrs_lrc(sub_values, group_number - 1, coding_ptrs, 1, block_size, erasures);
    }
    // printf("decoding end!\n");
    
    //store the repaired failed blocks
    ptr->fail_node_index = fail_index;
    ptr->number_of_b = 1;
    if(erasures[0] == group_number - 1){
      memcached_set(ptr, key_ptrs[erasures[0]], key_len_ptrs[erasures[0]], coding_ptrs[0], block_size,86400,0);
    }else{
      if(fail_index == k - 1){
        memcached_set(ptr, key_ptrs[erasures[0]], key_len_ptrs[erasures[0]], sub_values[erasures[0]], last_data_request_size,86400,0);
      }else{
        memcached_set(ptr, key_ptrs[erasures[0]], key_len_ptrs[erasures[0]], sub_values[erasures[0]], block_size,86400,0);
      }
    }
    ptr->number_of_b = 0;
  }
  delete erasures;
  delete key_len_ptrs;
  for(uint32_t i = 0; i < group_number; i++){
    if(key_ptrs[i] != NULL)
      delete key_ptrs[i];
  }
  for(uint32_t i = 0; i < group_number - 1; i++){
    if(sub_values[i] != NULL)
      delete sub_values[i];
  }
  delete coding_ptrs[0];

  return NULL;
}

// lrc add
char *memcached_repair(memcached_st *ptr, const char *key,
                    size_t key_length,
                    size_t value_length,
                    uint32_t *flags,
                    memcached_return_t *error,uint32_t fail_index)
{
  return memcached_repair_by_index(ptr,key,
                    key_length,value_length,flags,
                    error,fail_index,1);
}

static memcached_return_t replication_binary_mget(memcached_st *ptr,
                                                  uint32_t* hash,
                                                  bool* dead_servers,
                                                  const char *const *keys,
                                                  const size_t *key_length,
                                                  const size_t number_of_keys)
{
  memcached_return_t rc= MEMCACHED_NOTFOUND;
  uint32_t start= 0;
  uint64_t randomize_read= memcached_behavior_get(ptr, MEMCACHED_BEHAVIOR_RANDOMIZE_REPLICA_READ);

  if (randomize_read)
  {
    start= (uint32_t)random() % (uint32_t)(ptr->number_of_replicas + 1);
  }

  /* Loop for each replica */
  for (uint32_t replica= 0; replica <= ptr->number_of_replicas; ++replica)
  {
    bool success= true;

    for (uint32_t x= 0; x < number_of_keys; ++x)
    {
      if (hash[x] == memcached_server_count(ptr))
      {
        continue; /* Already successfully sent */
      }

      uint32_t server= hash[x] +replica;

      /* In case of randomized reads */
      if (randomize_read and ((server + start) <= (hash[x] + ptr->number_of_replicas)))
      {
        server+= start;
      }

      while (server >= memcached_server_count(ptr))
      {
        server -= memcached_server_count(ptr);
      }

      if (dead_servers[server])
      {
        continue;
      }

      memcached_instance_st* instance= memcached_instance_fetch(ptr, server);

      if (instance->response_count() == 0)
      {
        rc= memcached_connect(instance);

        if (memcached_failed(rc))
        {
          memcached_io_reset(instance);
          dead_servers[server]= true;
          success= false;
          continue;
        }
      }

      protocol_binary_request_getk request= {};
      initialize_binary_request(instance, request.message.header);
      request.message.header.request.opcode= PROTOCOL_BINARY_CMD_GETK;
      request.message.header.request.keylen= htons((uint16_t)(key_length[x] + memcached_array_size(ptr->_namespace)));
      request.message.header.request.datatype= PROTOCOL_BINARY_RAW_BYTES;
      request.message.header.request.bodylen= htonl((uint32_t)(key_length[x] + memcached_array_size(ptr->_namespace)));

      /*
       * We need to disable buffering to actually know that the request was
       * successfully sent to the server (so that we should expect a result
       * back). It would be nice to do this in buffered mode, but then it
       * would be complex to handle all error situations if we got to send
       * some of the messages, and then we failed on writing out some others
       * and we used the callback interface from memcached_mget_execute so
       * that we might have processed some of the responses etc. For now,
       * just make sure we work _correctly_
     */
      libmemcached_io_vector_st vector[]=
      {
        { request.bytes, sizeof(request.bytes) },
        { memcached_array_string(ptr->_namespace), memcached_array_size(ptr->_namespace) },
        { keys[x], key_length[x] }
      };

      if (memcached_io_writev(instance, vector, 3, true) == false)
      {
        memcached_io_reset(instance);
        dead_servers[server]= true;
        success= false;
        continue;
      }

      memcached_server_response_increment(instance);
      hash[x]= memcached_server_count(ptr);
    }

    if (success)
    {
      break;
    }
  }

  return rc;
}

static memcached_return_t binary_mget_by_key(memcached_st *ptr,
                                             const uint32_t master_server_key,
                                             bool is_group_key_set,
                                             const char * const *keys,
                                             const size_t *key_length,
                                             const size_t number_of_keys,
                                             const bool mget_mode)
{
  if (ptr->number_of_replicas == 0)
  {
    return simple_binary_mget(ptr, master_server_key, is_group_key_set,
                              keys, key_length, number_of_keys, mget_mode);
  }

  uint32_t* hash= libmemcached_xvalloc(ptr, number_of_keys, uint32_t);
  bool* dead_servers= libmemcached_xcalloc(ptr, memcached_server_count(ptr), bool);

  if (hash == NULL or dead_servers == NULL)
  {
    libmemcached_free(ptr, hash);
    libmemcached_free(ptr, dead_servers);
    return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
  }

  if (is_group_key_set)
  {
    for (size_t x= 0; x < number_of_keys; x++)
    {
      hash[x]= master_server_key;
    }
  }
  else
  {
    for (size_t x= 0; x < number_of_keys; x++)
    {
      hash[x]= memcached_generate_hash_with_redistribution(ptr, keys[x], key_length[x]);
    }
  }

  memcached_return_t rc= replication_binary_mget(ptr, hash, dead_servers, keys,
                                                 key_length, number_of_keys);

  WATCHPOINT_IFERROR(rc);
  libmemcached_free(ptr, hash);
  libmemcached_free(ptr, dead_servers);

  return MEMCACHED_SUCCESS;
}
