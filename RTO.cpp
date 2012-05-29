#include "RTO.h"
#includ <stdlib.h>
#include <stdio.h>
// m is the last measured RTT
// a is the smoothed average RTT
// d is the smoothed mean deviation RTT



	// array to hold the updated RTTs
	int rtt_array[64 * 1024];
	int rtt_smoothed_avg[64 * 1024];
	int rtt_smoothed_mean_dev[64 * 1024];
	int current_rtt_sum = 0;
	int rtt_counter = 0;
	int g = 0.125;
	
	int RTO::getRTO(int m){
	
		if (rtt_counter == 0){
			init(m);
		}
		
		// error is the difference between the actual rtt and the estimated rtt
		int err = m - rtt_smoothed_avg[rtt_counter];
		
		// calculate a new value for the avg, based on the new rtt measurement and error
		if(rtt_counter + 1 > (64 * 1024)){
			rtt_smoothed_avg[0] = rtt_smoothed_avg[rtt_counter] + (g * err);
		}
		else{
			rtt_smoothed_avg[rtt_counter + 1] = rtt_smoothed_avg[rtt_counter] + (g * err);
		}
		
		// calculate a new mean deviation, using the previous and error
		if(rtt_counter + 1 > (64 * 1024)){
			rtt_smoothed_mean_dev[0] = rtt_smoothed_mean_dev[rtt_counter] + (h * (abs(err) - rtt_smoothed_mean_dev[rtt_counter]));
		}
		rtt_smoothed_mean_dev[rtt_counter + 1] = rtt_smoothed_mean_dev[rtt_counter] + (h * (abs(err) - rtt_smoothed_mean_dev[rtt_counter]));
		
		// update the current rtt
		if(rtt_counter + 1 > (64 * 1024)){
			rtt_counter = 0;
		}
		else{
			rtt_counter++;
		}
		
		return rtt_smoothed_avg[counter] + (4 * (rtt_smoothed_mean_dev[counter]));
	}
	
	void RTO::init(int m){
	
		rtt_array[0] = m;
		rtt_smoothed_avg[0] = m;
		rtt_smooted_mean_dev[0] = 0;
	}

