#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <libmemcached/memcached.h>
using namespace std;

string lrc = "Azure-LRC";
string placement = "optimal";

memcached_st* init_memcached_server(uint32_t n, uint32_t k, uint32_t r)
{
	memcached_st* memc;
	memcached_return_t rc;
	memcached_server_list_st servers;
	memc = memcached_create(NULL);
	servers = memcached_server_list_append(NULL, "10.0.0.61", 11241, &rc);

	servers = memcached_server_list_append(servers, "10.0.0.54", 11212, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.54", 11213, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.54", 11214, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.54", 11215, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.54", 11216, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.54", 11217, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.54", 11218, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.54", 11219, &rc);

	servers = memcached_server_list_append(servers, "10.0.0.55", 11220, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.55", 11221, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.55", 11222, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.55", 11223, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.55", 11224, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.55", 11225, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.55", 11226, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.55", 11227, &rc);

	servers = memcached_server_list_append(servers, "10.0.0.56", 11228, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.56", 11229, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.56", 11230, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.56", 11231, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.56", 11232, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.56", 11233, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.56", 11234, &rc);
	servers = memcached_server_list_append(servers, "10.0.0.56", 11235, &rc);
	rc = memcached_server_push(memc, servers);
	if(rc != MEMCACHED_SUCCESS){
		printf("Add servers failure!\n");
	}
	memcached_server_list_free(servers);
	memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_DISTRIBUTION, MEMCACHED_DISTRIBUTION_CONSISTENT);
  	memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, (uint64_t)1);
  	memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_IS_LRC, (uint32_t)1);	//apply lrc mechanism
  	if(lrc == "Azure-LRC"){	//scheme of lrc
  		memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_LRC_SCHEME, MEMCACHED_LRC_AZURE);
  	}else if(lrc == "Azure-LRC+1"){
  		memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_LRC_SCHEME, MEMCACHED_LRC_AZURE_1);
  	}
  	if(placement == "flat"){	//scheme of data placement
  		memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_PLACEMENT_SCHEME, MEMCACHED_PLACEMENT_FLAT);
  	}else if(placement == "random"){
  		memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_PLACEMENT_SCHEME, MEMCACHED_PLACEMENT_RANDOM);
  	}else if(placement == "optimal"){
  		memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_PLACEMENT_SCHEME, MEMCACHED_PLACEMENT_OPTIMAL);
  	}
  	//(n, k, r)
	memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_NUMBER_OF_N, n);
	memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_NUMBER_OF_K, k);
	memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_NUMBER_OF_R, r);
	memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_NUMBER_OF_RACK, n);
	return memc;
}

void lrc_upload_download_test(string key, int value_size, string input_file, string output_file, uint32_t n, uint32_t k, uint32_t r)
{
	memcached_st* memc = init_memcached_server(n, k, r);
	size_t key_length = key.length();
	char* value = new char[value_size];
	FILE* fp = fopen(input_file.c_str(), "r");
	fread(value, 1, value_size, fp);
	fclose(fp);
	value[value_size] = '\0';

	uint32_t run_times = 10;
	double temp_time = 0.0;
	struct timeval start_time, end_time;
	struct timeval s_start_time, s_end_time;

	memcached_return_t rc;
	time_t expiration = 0;
	uint32_t flags = 0;
	size_t value_length = value_size;

	fprintf(stderr, "------Test result of %s(%d,%d,%d) with %s data placement------\n", lrc.c_str(), n, k, r, placement.c_str());

	double write_time = 0.0;
	double max_wtime = 0.0, min_wtime = 0.0;
	double **wtime_details = new double*[run_times];
	double fetch_time = 0.0;
	double max_ftime = 0.0, min_ftime = 0.0;
	char* fetch_val;
	double repair_time = 0.0;
	double max_rtime = 0.0, min_rtime = 0.0;
	double rt_time = 0.0;
	double max_rt_time = 0.0, min_rt_time = 0.0;
	double repair_time_ = 0.0;
	double max_rtime_ = 0.0, min_rtime_ = 0.0;
	double rt_time_ = 0.0;
	double max_rt_time_ = 0.0, min_rt_time_ = 0.0;
    double delete_time = 0.0;
	double max_dtime = 0.0, min_dtime = 0.0;
	for(uint32_t i = 0; i < run_times; i++)
	{
		//SET
		temp_time = 0.0;
		wtime_details[i] = new double[3];
		gettimeofday(&s_start_time, NULL);
		rc = memcached_set(memc, key.c_str(), key.length(), value, value_length, expiration, flags);
        if(rc != MEMCACHED_SUCCESS){
			printf("Set value failure! %s!\n", memcached_strerror(memc, rc));
			break;
		}
		gettimeofday(&s_end_time, NULL);
		temp_time = s_end_time.tv_sec - s_start_time.tv_sec + (s_end_time.tv_usec - s_start_time.tv_usec) * 1.0 / 1000000;
		get_times(memc, wtime_details[i]);
		if(temp_time > max_wtime){
			max_wtime = temp_time;
		}
		if(temp_time < min_wtime || i == 0){
			min_wtime = temp_time;
		}
		write_time += temp_time;

		//GET
		temp_time = 0.0;
		gettimeofday(&s_start_time, NULL);
		fetch_val = memcached_get(memc, key.c_str(), key.length(), &value_length, &flags, &rc);
		gettimeofday(&s_end_time, NULL);
		if(rc != MEMCACHED_SUCCESS){
			printf("Fetch value failure!\n");
			break;
		}
		temp_time = s_end_time.tv_sec - s_start_time.tv_sec + (s_end_time.tv_usec - s_start_time.tv_usec) * 1.0 / 1000000;
		if(temp_time > max_ftime){
			max_ftime = temp_time;
		}
		if(temp_time < min_ftime || i == 0){
			min_ftime = temp_time;
		}
		fetch_time += temp_time;

		//REPAIR
		//NRC
		temp_time = 0.0;
		gettimeofday(&s_start_time, NULL);
		for(uint32_t j = 0; j < n; j++){
			memcached_repair(memc, key.c_str(), key.length(), value_length, &flags, &rc, j);
			double tmp = get_times(memc, NULL);
			temp_time += tmp;
		}
		gettimeofday(&s_end_time, NULL);
		rt_time += temp_time; 
		if(temp_time > max_rt_time){
			max_rt_time = temp_time;
		}
		if(temp_time < min_rt_time || i == 0){
			min_rt_time = temp_time;
		}
		temp_time = s_end_time.tv_sec - s_start_time.tv_sec + (s_end_time.tv_usec - s_start_time.tv_usec) * 1.0 / 1000000;
		if(temp_time > max_rtime){
			max_rtime = temp_time;
		}
		if(temp_time < min_rtime || i == 0){
			min_rtime = temp_time;
		}
		repair_time += temp_time;
		//DRC
		temp_time = 0.0;
		gettimeofday(&s_start_time, NULL);
		for(uint32_t j = 0; j < k; j++){
			memcached_repair(memc, key.c_str(), key.length(), value_length, &flags, &rc, j);
			double tmp = get_times(memc, NULL);
			temp_time += tmp;
		}
		gettimeofday(&s_end_time, NULL);
		rt_time_ += temp_time; 
		if(temp_time > max_rt_time_){
			max_rt_time_ = temp_time;
		}
		if(temp_time < min_rt_time_ || i == 0){
			min_rt_time_ = temp_time;
		}
		temp_time = s_end_time.tv_sec - s_start_time.tv_sec + (s_end_time.tv_usec - s_start_time.tv_usec) * 1.0 / 1000000;
		if(temp_time > max_rtime_){
			max_rtime_ = temp_time;
		}
		if(temp_time < min_rtime_ || i == 0){
			min_rtime_ = temp_time;
		}
		repair_time_ += temp_time;

		//DELETE
		temp_time = 0.0;
		gettimeofday(&s_start_time, NULL);
		memcached_delete(memc, key.c_str(), key.length(), 0);
		gettimeofday(&s_end_time, NULL);
		temp_time = s_end_time.tv_sec - s_start_time.tv_sec + (s_end_time.tv_usec - s_start_time.tv_usec) * 1.0 / 1000000;
		if(temp_time > max_dtime){
			max_dtime = temp_time;
		}
		if(temp_time < min_dtime || i == 0){
			min_dtime = temp_time;
		}
		delete_time += temp_time;
	}

	fprintf(stderr, "Write time:\n");
	fprintf(stderr, "Average: %.6lfs, Min: %.6lfs, Max: %.6lfs\n", write_time / run_times, min_wtime, max_wtime);
	double divide_latency = 0.0, encoding_latency = 0.0, transmission_latency = 0.0;
	double max_dl = 0.0, min_dl = 0.0, max_el = 0.0, min_el = 0.0, max_tl = 0.0, min_tl = 0.0;
	for(uint32_t i = 0; i < run_times; i++){
		divide_latency += wtime_details[i][0];
		if(wtime_details[i][0] > max_dl){
			max_dl = wtime_details[i][0];
		}
		if(wtime_details[i][0] < min_dl || i == 0){
			min_dl = wtime_details[i][0];
		}
		encoding_latency += wtime_details[i][1];
		if(wtime_details[i][1] > max_el){
			max_el = wtime_details[i][1];
		}
		if(wtime_details[i][1] < min_el || i == 0){
			min_el = wtime_details[i][1];
		}
		transmission_latency += wtime_details[i][2];
		if(wtime_details[i][2] > max_tl){
			max_tl = wtime_details[i][2];
		}
		if(wtime_details[i][2] < min_tl || i == 0){
			min_tl = wtime_details[i][2];
		}
	}
	fprintf(stderr, " Divide latency:\n");
	fprintf(stderr, " Average: %.6lfs, Min: %.6lfs, Max: %.6lfs\n", divide_latency / run_times, min_dl, max_dl);
	fprintf(stderr, " Encoding latency:\n");
	fprintf(stderr, " Average: %.6lfs, Min: %.6lfs, Max: %.6lfs\n", encoding_latency / run_times, min_el, max_el);
	fprintf(stderr, " Transmission latency:\n");
	fprintf(stderr, " Average: %.6lfs, Min: %.6lfs, Max: %.6lfs\n", transmission_latency / run_times, min_tl, max_tl);
	
	fprintf(stderr, "Fetch time :\n");
	fprintf(stderr, "Average: %.6lfs, Min: %.6lfs, Max: %.6lfs\n", fetch_time / run_times, min_ftime, max_ftime);
	if(rc == MEMCACHED_SUCCESS){
		// printf("Fetch value:\n%s\n", fetch_val);
		fp = fopen(output_file.c_str(), "w");
    	fwrite(fetch_val, 1, value_length, fp);
    	fclose(fp);
	}

	//NRC
	fprintf(stderr, "Repair time (NRC):\n");
	fprintf(stderr, "Average: %.6lfs, Min: %.6lfs, Max: %.6lfs\n",repair_time / (run_times * k), min_rtime / k, max_rtime / k);
	fprintf(stderr, " Transmission latency:\n");
	fprintf(stderr, " Average: %.6lfs, Min: %.6lfs, Max: %.6lfs\n", rt_time / (run_times * k), min_rt_time / k, max_rt_time / k);
	
	//DRC
	fprintf(stderr, "Repair time (DRC):\n");
	fprintf(stderr, "Average: %.6lfs, Min: %.6lfs, Max: %.6lfs\n", repair_time_ / (run_times * k), min_rtime_ / k, max_rtime_ / k);
	fprintf(stderr, " Transmission latency:\n");
	fprintf(stderr, " Average: %.6lfs, Min: %.6lfs, Max: %.6lfs\n", rt_time_ / (run_times * k), min_rt_time_ / k, max_rt_time_ / k);
	
	fprintf(stderr, "Delete time:\n");
	fprintf(stderr, "Average: %.6lfs, Min: %.6lfs, Max: %.6lfs\n", delete_time / run_times, min_dtime, max_dtime);
	
	memcached_free(memc);
}

void print_help()
{
	printf("\nUsage:\n");
	printf("Format. ./test n k r lrc_scheme placement_scheme value_size\n");
	printf("e.i. for lrc_num: 0 - Azure-LRC, 1 - Azure-LRC+1\n");
	printf("e.i. for placement_scheme: 0 - flat_placement, 1 - random_placement, 2 - best_placement\n");
	printf("e.i. for value_size: the size of value(Kbytes)\n");
	printf("default: ./test 16 10 5 0 2 1");
}

int main(int argc, char** argv)
{
	int n = 16, k = 10, r = 5, lnum = 0, pnum = 2, value_size = 1;
	if(argc == 7){
		n = atoi(argv[1]);
		k = atoi(argv[2]);
		r = atoi(argv[3]);
		lnum = atoi(argv[4]);
		pnum = atoi(argv[5]);
		value_size = atoi(argv[6]);
		if(lnum == 0){
			lrc = "Azure-LRC";
		}else if(lnum == 1){
			lrc = "Azure-LRC+1";
		}
		if(pnum == 0){
			placement = "flat";
		}else if(pnum == 1){
			placement = "random";
		}else if(pnum == 2){
			placement = "optimal";
		}
	}else if(argc > 1){
		print_help();
		exit(0);
	}
	string key;
	string input_file;
	string output_file;
	key = "lrc_big_obj_" + to_string(value_size) + "K";
	input_file = "./data/input_item_" + to_string(value_size) + "K.txt";
	output_file = "./data/output_item_" + to_string(value_size) + "K.txt";
	printf("input_file: %s\n", input_file.c_str());
	lrc_upload_download_test(key, value_size * 1024, input_file, output_file, n, k, r);
	return 0;
}