/*******************************************************************************
* lrtimer.cpp                                                                  *
*                                                                              *
* Written by Max Gurdziel 2005 under GNU General Public License                *
* contact me: max[at]remoteSOS[dot]com                                         *
*                                                                              *
* LRTimer is a low resolution timer class with own timing thread. It allows    *
*  an external callback function to be supplied that will be called in         *
*  pre-defined time intervals. The smallest timer interval you can use is 1ms. *
*                                                                              *
*  See header file for more info, usage information and example                *
*                                                                              *
*                                                                              *
*                                                                              *
* Permission to use, copy, modify, and distribute this software and its        *
* documentation under the terms of the GNU General Public License is hereby    *
* granted. No representations are made about the suitability of this software  *
* for any purpose. It is provided "as is" without express or implied warranty. *
* See http://www.gnu.org/copyleft/gpl.html for more details.                   *
*                                                                              *
* All I ask is that if you use LRTimer in your project you retain the          *
* copyright notice. If you have any comments and suggestions please email me   *
* max[at]remoteSOS[dot]com                                                     *
*                                                                              *
*******************************************************************************/

#include "lrtimer.h"

LRTimer::LRTimer():
	m_dwInterval(1000),
	m_bRunning(FALSE),
  	m_pCallback(NULL),
    m_hTimerThread(0) {}

LRTimer::~LRTimer(){
}

VOID CALLBACK LRTimer::TimerAPCProc(LPVOID, DWORD, DWORD) {
  if (NULL != m_pCallback) {
  	(*m_pCallback)();                  // call custom callback function
  } else {
    printf("No callback function set\n");
  }
}

DWORD WINAPI LRTimer::timerThread() {
  HANDLE          hTimer;
  BOOL            bSuccess;
  LARGE_INTEGER   liDueTime;

  if ( hTimer = CreateWaitableTimer(
           NULL,                       // Default security attributes
           FALSE,                      // Create auto-reset timer
		   (LPCWSTR)"LRTimer" ) ) {    // Name of waitable timer
    liDueTime.QuadPart=-(LONGLONG)m_dwInterval * _SECOND;
  }
  bSuccess = SetWaitableTimer(
  	  hTimer,                          // Handle to the timer object
      &liDueTime,                      // When timer will become signaled first time
      m_dwInterval,                    // Periodic timer interval
      TimerAPCProcAdapter,             // Completion routine
      this,                            // Argument to the completion routine
      FALSE );                         // Do not restore a suspended system
  if ( bSuccess ) {
    while (m_bRunning) {
   	  SleepEx(1000, TRUE);
	}
    CancelWaitableTimer(hTimer);
  } else {
	return 1;
  }
  CloseHandle(hTimer);
  return 0;
}

VOID LRTimer::start() {
  m_bRunning = TRUE;
  if (m_hTimerThread != 0) {
    stop();
  }
  m_hTimerThread = CreateThread(NULL, 0, timerThreadAdapter, this ,0,&m_iID);
  if (m_hTimerThread == NULL) {
    printf( "CreateThread failed (%d)\n", GetLastError() );
    return;
  }
}

VOID LRTimer::start(DWORD _interval_ms) {
  setInterval(_interval_ms);
  start();
}

VOID LRTimer::stop() {
  m_bRunning = FALSE;
  CloseHandle(m_hTimerThread);
  m_hTimerThread = 0;
}

VOID LRTimer::setInterval(DWORD _interval_ms){
  m_dwInterval = _interval_ms;
}

DWORD LRTimer::getInterval(){
  return m_dwInterval;
}

VOID LRTimer::setCallbackFunction( VOID (*_pCallback)(VOID)) {
  m_pCallback = _pCallback;
}

BOOL LRTimer::isRunning() {
  return m_bRunning;
}