#ifndef RTO_H
#define RTO_H

#include <stdlib.h>
#include <stdio.h>

class RTO{
 public:
  int getRTO(int m);
  

 private:
  void init(int m);
  int rtt_array[64 * 1024];
  int rtt_smoothed_avg[64 * 1024];
  int rtt_smoothed_mean_dev[64 * 1024];
  int current_rtt_sum;
  int rtt_counter;
  int g;
  int h;
}

#endif
