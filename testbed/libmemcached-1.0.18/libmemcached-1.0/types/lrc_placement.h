#pragma once

enum memcached_lrc_t {
  MEMCACHED_LRC_AZURE,
  MEMCACHED_LRC_AZURE_1
};

enum memcached_placement_t {
  MEMCACHED_PLACEMENT_FLAT,
  MEMCACHED_PLACEMENT_RANDOM,
  MEMCACHED_PLACEMENT_OPTIMAL
};

#ifndef __cplusplus
typedef enum memcached_lrc_t memcached_lrc_t;
typedef enum memcached_placement_t memcached_placement_t;
#endif