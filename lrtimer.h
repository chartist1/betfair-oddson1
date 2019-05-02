/*******************************************************************************
* lrtimer.h                                                                    *
*                                                                              *
* Written by Max Gurdziel 2005 under GNU General Public License                *
* contact me: max[at]remoteSOS[dot]com                                         *
*                                                                              *
* LRTimer is a low resolution timer class with own timing thread. It allows    *
*  an external callback function to be supplied that will be called in         *
*  pre-defined time intervals. The smallest timer interval you can use is 1ms. *
*                                                                              *
* Tested with gcc mingw & Visual C++ 6.0 under WindowsXP Home and Pro          *
*                                                                              *
*                                                                              *
*     LRTimer timer;                                  // define LRTimer object *
*     timer.setInterval(100);                         //set interval of 100ms  *
*     timer.setCallbackFunction(&myCallbackFunction); //set callback function  *
*           // callback function takes and returns no parameters               *
*           // it's prototype must be: void myCallbackFunction(void);          *
*                                                                              *
*     timer.start();         // start the timer                                *
*     ....                                                                     *
*     timer.stop();          // stops the timer                                *
*     ....                                                                     *
*     timer.start(200);      // starts timer with new interval                 *
*                                                                              *
*                                                                              *
*                                                                              *
*                                                                              *
*                                                                              *
*   Example code:                                                              *
*   Copy and paste below sample code to test LRTimer                           *
*                                                                              *
________________________________________________________________________________

#include <stdlib.h>
#include "lrtimer.h"


// define callback function
void callback() {
  static DWORD cnt = 0;
  char c;
  cnt++;
  switch (cnt % 4) {
    case 0: c = '|'; break;
    case 1: c = '/'; break;
    case 2: c = '-'; break;
    case 3: c = '\\';
  }
  printf("\b%c",c);
}

int main(int argc, char *argv[]) {

  LRTimer lrt;
  lrt.setCallbackFunction(&callback);  // set the callback function by reference
  lrt.setInterval(50);                 // set delay interval in miliseconds
  lrt.start();                         // start the timer
  getchar();                           // let it run for a while - press Enter
  lrt.stop();                          // stop the timer
  getchar();                           // wait to show it's stopped - Enter
  lrt.start(200);                      // start with different delay
  getchar();
  lrt.stop();
  system("PAUSE");
  return 0;
}
________________________________________________________________________________
*                                                                              *
* Permission to use, copy, modify, and distribute this software and its        *
* documentation under the terms of the GNU General Public License is hereby    *
* granted. No representations are made about the suitability of this software  *
* for any purpose. It is provided "as is" without express or implied warranty. *
* See http://www.gnu.org/copyleft/gpl.html for more details.                   *
*                                                                              *
* All I ask is that if you use LRTimer in your project retain the              *
* copyright notice. If you have any comments and suggestions please email me   *
* max[at]remoteSOS[dot]com                                                     *
*                                                                              *
*******************************************************************************/

#ifndef LRTIMER_H__
#define LRTIMER_H__

#include <windows.h>
#include <stdio.h>

// define a second in terms of 100ns - used with waitable timer API
#define _SECOND 10000

class LRTimer {

  public:
	// default constructor
	LRTimer();
  	
	// default destructor
	~LRTimer();
		
	// starts timer by creating new thread. interval must be set earlier
	VOID start();
		
	// starts timer with given interval in miliseconds
	VOID start(DWORD _interval_ms);
		
	// stops the timer
	VOID stop();
		
	// sets time interval in miliseconds
	VOID setInterval(DWORD _interval_ms);
		
	// returns time interval in ms
	DWORD getInterval();
		
	// sets function that will be called on time expiration
	VOID setCallbackFunction( VOID (*_pCallback)(VOID));

	// returns true if LRtimer is currently running
	BOOL isRunning();

	// returns handle of Mutex used for timers
	HANDLE waithandle();

  private:
  	DWORD m_dwInterval;				// interval between alarms
  	VOID (*m_pCallback)(VOID);		// pointer to user callback function
  	BOOL m_bRunning;				// timer running state
  	HANDLE m_hTimerThread;			// handle to timer thread
  	DWORD m_iID;					// timer thread id - added for compatibility with Win95/98
  	
	// timer clocking tread runtine
	virtual DWORD WINAPI timerThread();

	// wrapper to thread runtine so it can be used within a class
  	static DWORD WINAPI timerThreadAdapter(PVOID _this) {
  		return ((LRTimer*) _this)->timerThread();
 	}

	// timer callback APC procedure called when timer is signaled
 	virtual VOID CALLBACK TimerAPCProc(LPVOID, DWORD, DWORD);

	// wrapper to callback APC procedure so it can be used within a class
 	static  VOID CALLBACK TimerAPCProcAdapter(PVOID _this, DWORD a1=0, DWORD a2=0) {
 			((LRTimer*) _this)->TimerAPCProc( NULL, a1, a2 );
	}
};
#endif
