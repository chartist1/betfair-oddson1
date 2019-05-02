// mtrand.cpp, see include file mtrand.h for information
//
// Used for development to give an outcome for races which don't finish
// cleanly, with live bets being placed then the result can be determined
// by checking other values on Betfair, without bets being placed this
// gives a weighted random answer.
//

// Sample use
//#define NUMCHOICES 2
//int winorlose( void )
//{
  // Initialise the Mersenne Twister int32 random number generator
//  MTRand_int32 mt((int)time(NULL));
//  int choice_weight[NUMCHOICES] = {55, 45};
//  int sum_of_weight = 0;
  // Sum up the choic weighting values
//  for(int i=0; i<NUMCHOICES; i++) {
//    sum_of_weight += choice_weight[i];
//  }
//  int rnd;
  // Generate a random number in the required  range
//  do { 
//    rnd  = mt() / sum_of_weight;
//  } while (rnd > sum_of_weight);
  // Detemine which choice was selected
//  for(int i=0; i<NUMCHOICES; i++) {
//    if(rnd < choice_weight[i]) {
//      return i;
//	}
//    rnd -= choice_weight[i];
//  }
//  return(rnd);
//}

#include "mtrand.h"
// non-inline function definitions and static member definitions cannot
// reside in header file because of the risk of multiple declarations

// initialization of static private members
unsigned long MTRand_int32::state[n] = {0x0UL};
int MTRand_int32::p = 0;
bool MTRand_int32::init = false;

void MTRand_int32::gen_state() { // generate new state vector
  for (int i = 0; i < (n - m); ++i)
    state[i] = state[i + m] ^ twiddle(state[i], state[i + 1]);
  for (int i = n - m; i < (n - 1); ++i)
    state[i] = state[i + m - n] ^ twiddle(state[i], state[i + 1]);
  state[n - 1] = state[m - 1] ^ twiddle(state[n - 1], state[0]);
  p = 0; // reset position
}

void MTRand_int32::seed(unsigned long s) {  // init by 32 bit seed
  state[0] = s & 0xFFFFFFFFUL; // for > 32 bit machines
  for (int i = 1; i < n; ++i) {
    state[i] = 1812433253UL * (state[i - 1] ^ (state[i - 1] >> 30)) + i;
// see Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier
// in the previous versions, MSBs of the seed affect only MSBs of the array state
// 2002/01/09 modified by Makoto Matsumoto
    state[i] &= 0xFFFFFFFFUL; // for > 32 bit machines
  }
  p = n; // force gen_state() to be called for next random number
}

void MTRand_int32::seed(const unsigned long* array, int size) { // init by array
  seed(19650218UL);
  int i = 1, j = 0;
  for (int k = ((n > size) ? n : size); k; --k) {
    state[i] = (state[i] ^ ((state[i - 1] ^ (state[i - 1] >> 30)) * 1664525UL))
      + array[j] + j; // non linear
    state[i] &= 0xFFFFFFFFUL; // for > 32 bit machines
    ++j; j %= size;
    if ((++i) == n) { state[0] = state[n - 1]; i = 1; }
  }
  for (int k = n - 1; k; --k) {
    state[i] = (state[i] ^ ((state[i - 1] ^ (state[i - 1] >> 30)) * 1566083941UL)) - i;
    state[i] &= 0xFFFFFFFFUL; // for > 32 bit machines
    if ((++i) == n) { state[0] = state[n - 1]; i = 1; }
  }
  state[0] = 0x80000000UL; // MSB is 1; assuring non-zero initial array
  p = n; // force gen_state() to be called for next random number
}
