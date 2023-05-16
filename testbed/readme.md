## Testbed experiment

We deploy multiple Memcached servers as storage servers and Libmemcached as the proxy. We leverage the Jerasure Library for erasure coding.

### Testing files

- main.cc
  - test SET, GET, DELETE in order for n times
- client.cc
  - test SET for n times, GET for n times, DELETE for n times, in order
- The results of two ways of testing are not much different.

### Installation of the Memcached servers

In each physical machine that runs as a Memcached server, do the following.

- Downloading the sourcecode of Memcached server:

https://memcached.org/downloads

- Unzipping:

```
$tar -zvxf memcached-1.5.20.tar.gz
```

- Entering the memcached directory, compling and installing:

```
$./configure --prefix=$(install_dir1)
$make & sudo make install
```

- Configure environment variables 

  - Run the following commands. (Temporary)

  - Or add them  to `~/.bashrc` and run `source ~/.bashrc`. (Permanent)

```
$ export PATH=$(install_dir1)/bin:$PATH
```

### Installation of Libmemcached

- Entering the libmemcached directory, compiling and installing

```
$ cd libmemcached-1.0.18/
$ ./configure --prefix=$(install_dir2)
$ make & sudo make install
```

- Configure environment variables 
  - Run the following commands. (Temporary)
  - Or add them  to `~/.bashrc` and run `source ~/.bashrc`. (Permanent)

```
$ export PATH=$(install_dir2)/bin:$PATH
$ export LD_LIBRARY_PATH=$(install_dir2)/lib:$LD_LIBRARY_PATH
```

- Entering testbed/ directory, compiling  the test file

```
$ make
```

> If there is a compiling error which notes that there is no `lrc_placement.h` included, then
>
> ```
> $ cp libmemcached-1.0.18/libmemcached-1.0/types/lrc_placement.h $(your_install_dir)/include/libmemcached-1.0/types/lrc_placement.h
> ```
>
> Notes that the path maybe different in different system.

### Running

```
$ ./test n k r lrc_scheme placement_scheme value_size
```

- parameters

  - `lrc_scheme`:  `0` - Azure-LRC, `1` - Azure-LRC+1
  - `placement_scheme`: `0` - flat placement, `1` - random placement, `2` - best_placement
  - `value_size`: the size of value(Kbytes)

- example

  - ```
    $ ./test 16 10 5 0 2 1
    ```

