/*******************************************************************************
* BetFairAPIFree_5.cpp                                                         *
*                                                                              *
* Copyright 2006-2013 Colin Hart (colinh@nxtgn.com)                            *
*                                                                              *
* Firstly let me state I am an independent software developer and nothing here *
* should be taken as representing the views or opinions of BetFair, whom I     *
* have no association with other than as a user of their services.             *
* All views and opinions offered here are entirely my own, if you don't        *
* like them then don't read them.                                              *
*                                                                              *
* BetFair offer on Online API to access their services programatically over    *
* the Internet. They provide this as a SOAP interface for which there are many *
* libraries (of varying quality and completeness) for most programming         *
* languages. Looking to develop some applications in C++ for my own use and to *
* give away as freebies I looked around for some samples to get me started.    *
*                                                                              *
* Very little was available in C++, I actually found nothing. Lots of people   *
* wanting software developed and asking questions but no answers. So I         *
* experimented with several Open Source SOAP libraries and settled on gSOAP as *
* it actually worked very nicely (better than some I won't mention) then added *
* OpenSSL as the BetFair SOAP interface is secure over SSL only, then looked   *
* at some PHP samples and studied the BetFair documentation and came up with   *
* this C++ class.                                                              *
*                                                                              *
* BetFair offer several subscription levels of access to their SOAP API, this  *
* class BetFairAPIFree_5 by default accesses the free level (it's in the name) *
* of version 5 of the Betfair API. There are some restrictions on the free     *
* level, such as not all functions are available and limited  access to some   *
* functions, that is only so many calls per minute. However there is enough    *
* functionality to do a lot of good stuff.                                     *
*                                                                              *
* Documentation on the BetFair API is available from their website, I will     *
* not attempt to replicate or paraphrase that here as it would probably        *
* infringe their copyright and what I put here would get out of date. Visit    *
* the BetFair site to get a copy of their API documentation. The documentation *
* with this software will describe the interface this class provides.          *
*                                                                              *
* This coftware is Copyright 2006-2013 Colin Hart                              *
*                                                                              *
* TODO:                                                                        *
* Want to implement login back in from keepalive if not logged in              *
*  maybe do this from all methods.                                             *
*******************************************************************************/

#include <string.h>
#include <string>
#include <sstream>
#include <vector>
#include "BetFairAPIFree_5.h"
#include "lrtimer.h"

bool BetFairAPIFree_5::instanceFlag = false;
BetFairAPIFree_5* BetFairAPIFree_5::single = NULL;

//
//
// Part of Singleton implementation static method gets pointer to object, creating object if necessary
BetFairAPIFree_5* BetFairAPIFree_5::getInstance(void)
{
  if(! instanceFlag) {
    single = new BetFairAPIFree_5();
    instanceFlag = true;
    return single;
  } else {
    return single;
  }
}
//
//
// Private Constructor object is a singleton
BetFairAPIFree_5::BetFairAPIFree_5()
{
  InitializeCriticalSection( &m_cs );
  // Initialise object variables
  m_username = "";
  m_password = "";
  m_locationId = 0;             // Free API
  m_productId = 82;             // Free API
  m_vendorSoftwareId = 0;       // Free API
  m_SOAPInUse = false;
  m_LogInRequested = false;
  m_LoggedIn = false;
  m_sessiontimer = NULL;
  soap_init(&m_soap); 
  m_soap.bind_flags=SO_REUSEADDR;
  // Create a 5 minute timer in another thread to use for maintaining login session
  m_sessiontimer = new( LRTimer );
  m_sessiontimer->setCallbackFunction( &BetFairAPIFree_5::sessionTimerCallback );
  // Call the keepalive every five minutes
  m_sessiontimer->setInterval(5 * 60 * 1000);
  m_sessiontimer->start();
}
//
//
// Public Destructor
BetFairAPIFree_5::~BetFairAPIFree_5()
{
  // Logout from Betfair if logged in

  // Stop and remove the session timer object
  m_sessiontimer->stop();
  delete( m_sessiontimer );
  // Tidy up other objects and variables used
  soap_destroy(&m_soap);                 // delete deserialized class instances (for C++ only)
  soap_end(&m_soap);                     // remove deserialized data and clean up
  soap_done(&m_soap);                    // detach the gSOAP environment
  DeleteCriticalSection( &m_cs );
}
// Private utility method for internal use only
// This sets the API in use flag and uses critical sections to make it thread
// safe so the timer keep alive thread and main application thread do not clash
bool BetFairAPIFree_5::getSOAPFlag(bool relogin)
{
  bool mysoap = false;
  int timeout = BFAPI_Timeout;
  // Prevent clashes on m_SOAPInUse
  while ( !mysoap ) {
    EnterCriticalSection( &m_cs );
    // Attempt to get the m_SOAPInUse flag
    if ( (!m_SOAPInUse) || relogin ) {
      m_SOAPInUse = true;
      mysoap = true;
    }
    LeaveCriticalSection( &m_cs );
    // Check if we got the m_SOAPInUse flag
    if ( !mysoap ) {
      // If not then wait for 1 second
      Sleep( 1000 );
      timeout--;
      // Did a timeout occur
      if ( timeout <= 0 ) {
        break;
      }
    }
  }
  return ( mysoap );
}
//
//
// Overload getSOAPFlag method for occasions when no flag passed so default it to false
bool BetFairAPIFree_5::getSOAPFlag(void)
{
  return ( this->getSOAPFlag( false ) );
}
//
//
//
BFAPI_Retvals BetFairAPIFree_5::reSetSOAP( void )
{
  bool mysoap = false;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    m_SOAPInUse = true; 
    soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
	soap_done(&m_soap);      // dealloc data and clean up
	Sleep(50);
    soap_init(&m_soap);      // initialise soap again
	m_soap.bind_flags=SO_REUSEADDR;
	m_SOAPInUse = false; 
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
    retval = BFAPI_OK;
  }
  return ( retval );
}
//
//
//
BFAPI_Retvals BetFairAPIFree_5::destroySOAP( void )
{
  bool mysoap = false;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    m_SOAPInUse = true; 
    soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
	soap_done(&m_soap);      // dealloc data and clean up
	m_SOAPInUse = false; 
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
    retval = BFAPI_OK;
  }
  return ( retval );
}


// Utility method
// Given a timestamp in GMT/UTC time converts it to UK time taking into account DST
// See relevant legislation http://www.legislation.hmso.gov.uk/si/si2002/20020262.htm
// Timestamps generated from here: http://www.onlineconversion.com/unix_time.htm
time_t BetFairAPIFree_5::timeGMTtoUK(time_t evttime)
{
  time_t ukdsts[29][2] = {     // Array of unix timestamps of UK DST clock changes
	{1269738000, 1288486800},  // 2010 Sunday, 28 March, 01:00 GMT Sunday, 31 October, 01:00 GMT
    {1301187600, 1319936400},  // 2011 Sunday, 27 March, 01:00 GMT Sunday, 30 October, 01:00 GMT
    {1332637200, 1351386000},  // 2012 Sunday, 25 March, 01:00 GMT Sunday, 28 October, 01:00 GMT
    {1364691600, 1382835600},  // 2013 Sunday, 31 March, 01:00 GMT Sunday, 27 October, 01:00 GMT
    {1396141200, 1414285200},  // 2014 Sunday, 30 March, 01:00 GMT Sunday, 26 October, 01:00 GMT
    {1427590800, 1445734800},  // 2015 Sunday, 29 March, 01:00 GMT Sunday, 25 October, 01:00 GMT
    {1459040400, 1477789200},  // 2016 Sunday, 27 March, 01:00 GMT Sunday, 30 October, 01:00 GMT
    {1490490000, 1509238800},  // 2017 Sunday, 26 March, 01:00 GMT Sunday, 29 October, 01:00 GMT
    {1521939600, 1540688400},  // 2018 Sunday, 25 March, 01:00 GMT Sunday, 28 October, 01:00 GMT
    {1553994000, 1572138000},  // 2019 Sunday, 31 March, 01:00 GMT Sunday, 27 October, 01:00 GMT
	{1585443600, 1603587600},  // 2020 Sunday, 29 March, 01:00 GMT Sunday, 25 October, 01:00 GMT
	{1616893200, 1635642000},  // 2021 Sunday, 28 March, 01:00 GMT Sunday, 31 October, 01:00 GMT
	{1648342800, 1667091600},  // 2022 Sunday, 27 March, 01:00 GMT Sunday, 30 October, 01:00 GMT
	{1679792400, 1698541200},  // 2023 Sunday, 26 March, 01:00 GMT Sunday, 29 October, 01:00 GMT
	{1711846800, 1729990800},  // 2024 Sunday, 31 March, 01:00 GMT Sunday, 27 October, 01:00 GMT
	{1743296400, 1761440400},  // 2025 Sunday, 30 March, 01:00 GMT Sunday, 26 October, 01:00 GMT
    {1774746000, 1792890000},  // 2026 Sunday, 29 March, 01:00 GMT Sunday, 25 October, 01:00 GMT
	{1806195600, 1824944400},  // 2027 Sunday, 28 March, 01:00 GMT Sunday, 31 October, 01:00 GMT
	{1837645200, 1856394000},  // 2028 Sunday, 26 March, 01:00 GMT Sunday, 29 October, 01:00 GMT
	{1869094800, 1887843600},  // 2029 Sunday, 25 March, 01:00 GMT Sunday, 28 October, 01:00 GMT
	{1901149200, 1919293200},  // 2030 Sunday, 31 March, 01:00 GMT Sunday, 27 October, 01:00 GMT
	{1932598800, 1950742800},  // 2031 Sunday, 30 March, 01:00 GMT Sunday, 26 October, 01:00 GMT
	{1964048400, 1982797200},  // 2032 Sunday, 28 March, 01:00 GMT Sunday, 31 October, 01:00 GMT
	{1995498000, 2014246800},  // 2033 Sunday, 27 March, 01:00 GMT Sunday, 30 October, 01:00 GMT
	{2026947600, 2045696400},  // 2034 Sunday, 26 March, 01:00 GMT Sunday, 29 October, 01:00 GMT
	{2058397200, 2077146000},  // 2035 Sunday, 25 March, 01:00 GMT Sunday, 28 October, 01:00 GMT
	{2090451600, 2108595600},  // 2036 Sunday, 30 March, 01:00 GMT Sunday, 26 October, 01:00 GMT
	{2121901200, 2140045200},  // 2037 Sunday, 29 March, 01:00 GMT Sunday, 25 October, 01:00 GMT
	{2153350800, 2172099600}   // 2038 Sunday, 28 March, 01:00 GMT Sunday, 31 October, 01:00 GMT
  };
   
  time_t retval = evttime;
  // Now subtract add hour if on DST
  for( int x=0; x<29; x++ ) {
    if ( evttime > ukdsts[x][0] && evttime < ukdsts[x][1] ) {
	  retval += 3600;
	  break;
	}
  }
  return retval;
}

//
//
//
BFAPI_Retvals BetFairAPIFree_5::login(bool relogin)
{
  bool mysoap = false;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag(relogin);
  if ( mysoap ) {
    m_SOAPInUse = true; 
    gs2__LoginReq BFLoginRef;              // Login requirements class
    _gs1__login BFLoginI;                  // Login requirements carrier class
    _gs1__loginResponse Response;          // Login response carrier class
    // Set up where to respond to
    Response.soap = &m_soap;
    // Set up parameters for login
    BFLoginRef.password = (char *)m_password.c_str();
    BFLoginRef.username = (char *)m_username.c_str();
    BFLoginRef.locationId = m_locationId;
    BFLoginRef.productId = m_productId;
    BFLoginRef.vendorSoftwareId = m_vendorSoftwareId;
    // Set up parameter carrier
    BFLoginI.request = &BFLoginRef;
    BFLoginI.soap = &m_soap;
    // Set the flag to indicate login has been requested
    m_LogInRequested = true;
    // Make the Betfair API Login call, use defaults for SOAP Endpoint and API command 
    if (SOAP_OK == soap_call___gs1__login(&m_soap, NULL, NULL, &BFLoginI, &Response )) {
      if (  gs2__APIErrorEnum__OK != Response.Result->errorCode ) {
        // Error at the BetFair level
        m_lasterrormsg = soap_gs2__LoginErrorEnum2s(&m_soap, Response.Result->errorCode);
        m_sessiontoken = "";
        m_lasttimestamp = 0;
        retval = BFAPI_BFERROR;
      } else {
        // Successful login save the session information
        m_sessiontoken = Response.Result->header->sessionToken;
        m_lasttimestamp = Response.Result->header->timestamp;
        m_LoggedIn = true;
        retval = BFAPI_OK;
	  } 
    } else {
      // Error at the SOAP level
      m_sessiontoken = "";
      m_lasttimestamp = 0;
      retval = BFAPI_SOAPERROR;
    }
	soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
  }
  return ( retval );
}
//
//
//
BFAPI_Retvals BetFairAPIFree_5::login(void)
{
  return ( this->login( false ) );
}
//
//
// See if login has been requested
bool BetFairAPIFree_5::getLogInRequested(void)
{
  return ( this->m_LogInRequested );
}
//
//
// See if have logged in
bool BetFairAPIFree_5::getLoggedIn(void)
{
  return ( this->m_LoggedIn );
}
//
//
// See if have logged in
void BetFairAPIFree_5::setLoggedIn(bool loggedin)
{
  this->m_LoggedIn = loggedin;
}

//
//
//
BFAPI_Retvals BetFairAPIFree_5::keepAlive(void)
{
  bool mysoap = false;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    gs2__APIRequestHeader BFKeepAliveheader;     // Header to put session key into   
    gs2__KeepAliveReq BFKeepAliveRef;            // Keep alive requirements class
    _gs1__keepAlive BFKeepAliveI;                // Keep alive requirements carrier class
    _gs1__keepAliveResponse Response;            // Keep alive response carrier class
    // Set up where to respond to
    Response.soap = &m_soap;
    // Set up keepalive parameters
    BFKeepAliveheader.sessionToken = (char *)m_sessiontoken.c_str();
    BFKeepAliveRef.header = &BFKeepAliveheader;
    // Set up parameter carrier
    BFKeepAliveI.request = &BFKeepAliveRef;
    BFKeepAliveI.soap = &m_soap;
    // Make the Betfair API keepAlive call, use defaults for SOAP Endpoint and API command 
    if (SOAP_OK == soap_call___gs1__keepAlive(&m_soap, NULL, NULL, &BFKeepAliveI, &Response )) {
      if (  gs2__APIErrorEnum__OK != Response.Result->header->errorCode ) {
        // Error at the BetFair level
        m_lasterrormsg = soap_gs2__APIErrorEnum2s(&m_soap, Response.Result->header->errorCode);
        m_sessiontoken = "";
        m_lasttimestamp = 0;
        if ( gs2__APIErrorEnum__NO_USCORESESSION == Response.Result->header->errorCode ) {
          // BetFair API no session handled specially as relogin will probably cure it
          retval = BFAPI_NOSESSION;
          printf("KeepAlive BetFair Error:%s\n", m_lasterrormsg.c_str());
        } else {
          // Any other BetFair API handled here
          retval = BFAPI_BFERROR;
          printf("KeepAlive BetFair Error:%s\n", m_lasterrormsg.c_str());
        }
      } else {
        // Successful keepalive save the session information
        m_sessiontoken = Response.Result->header->sessionToken;
        m_lasttimestamp = Response.Result->header->timestamp;
        retval = BFAPI_OK;
	  } 
	} else {
      // Error at the SOAP level
      retval = BFAPI_SOAPERROR;
      printf("KeepAlive SOAP Error\n");
	}
	soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
  }
  return ( retval );
}
//
//
//
void BetFairAPIFree_5::sessionTimerCallback(void)
{
  BetFairAPIFree_5 *BFApi = BetFairAPIFree_5::getInstance();
  // If we are supposed to be logged in
  if ( BFApi->getLogInRequested() ) {
    if ( BFAPI_NOSESSION == BFApi->keepAlive() ) {
      BFApi->setLoggedIn(false);
      // If BetFair API no session error then attempt to relogin
      BFApi->login(true);
    }
  }
}
//
//
//
void BetFairAPIFree_5::logout()
{
  bool mysoap = false;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    m_SOAPInUse = true; 
    m_LoggedIn = false;
    m_LogInRequested = false;
    m_SOAPInUse = false; 
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
  }
}
//
//
//
BFAPI_Retvals BetFairAPIFree_5::setUserName( std::string username )
{
  bool mysoap = false;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    m_SOAPInUse = true; 
    m_username = username;
    m_SOAPInUse = false; 
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
    retval = BFAPI_OK;
  }
  return ( retval );
}
//
//
//
BFAPI_Retvals BetFairAPIFree_5::setPassword( std::string password )
{
  bool mysoap = false;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    m_SOAPInUse = true; 
    m_password = password;
    m_SOAPInUse = false; 
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
    retval = BFAPI_OK;
  }
  return ( retval );
}

// TODO: Add extra error handling to display call specifici error code (switch statement)
// Given a reference to a vector of structures to store the event data in, this loads up
// the event items retrieved from BetFair into the vector.
BFAPI_Retvals BetFairAPIFree_5::getActiveEventTypes(vector<EventType>&EventList)
{
  bool mysoap = false;
  EventType EventTmp;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    gs2__APIRequestHeader BFgetActiveEventsheader;     // Header to put session key into   
    gs2__GetEventTypesReq BFgetActiveEventTypesReq;    // get active events request class
    _gs1__getActiveEventTypes BFgetActiveEventTypesI;  // getActiveEvents request carrier class
    _gs1__getActiveEventTypesResponse Response;        // getActiveEvents response carrier class

    // Set up where to respond to
    Response.soap = &m_soap;
    // Set up keepalive parameters
    BFgetActiveEventsheader.sessionToken = (char *)m_sessiontoken.c_str();
    BFgetActiveEventTypesReq.header = &BFgetActiveEventsheader;
    // Set up parameter carrier
    BFgetActiveEventTypesI.request = &BFgetActiveEventTypesReq;
    BFgetActiveEventTypesI.soap = &m_soap;
    // Make the Betfair API call, use defaults for SOAP Endpoint and API command 
    if (SOAP_OK == soap_call___gs1__getActiveEventTypes(&m_soap, NULL, NULL, &BFgetActiveEventTypesI, &Response )) {
      if (  gs2__APIErrorEnum__OK != Response.Result->header->errorCode ) {
        // Error at the BetFair level
        m_lasterrormsg = soap_gs2__APIErrorEnum2s(&m_soap, Response.Result->header->errorCode);
        m_sessiontoken = "";
        m_lasttimestamp = 0;
        if ( gs2__APIErrorEnum__NO_USCORESESSION == Response.Result->header->errorCode ) {
          // BetFair API no session handled specially as relogin will probably cure it
          retval = BFAPI_NOSESSION;
          printf("getActiveEventTypes BetFair Error:%s\n", m_lasterrormsg.c_str());
        } else {
          // Any other BetFair API handled here
          retval = BFAPI_BFERROR;
          printf("getActiveEventTypes BetFair Error:%s\n", m_lasterrormsg.c_str());
        }
      } else {
        // Successful getActiveEventTypes save the session information
        m_sessiontoken = Response.Result->header->sessionToken;
        m_lasttimestamp = Response.Result->header->timestamp;
        retval = BFAPI_OK;
        // Loop through the EventType array and get the name and id tag values
        gs2__EventType *Events;
        for ( int x = 0; x < Response.Result->eventTypeItems->__sizeEventType; x++ ) {
          Events = Response.Result->eventTypeItems->EventType[x];
          EventTmp.id = Events->id;
          EventTmp.name = Events->name;
          EventTmp.exchangeId = Events->exchangeId;
          EventTmp.nextMarketId = Events->nextMarketId;
          EventList.push_back(EventTmp);
        }
	  }
    } else {
      // Error at the SOAP level
      retval = BFAPI_SOAPERROR;
      printf("getActiveEventTypes SOAP Error\n");
    }
	soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
    retval = BFAPI_OK;
  }
  return ( retval );
}
//
//
//
BFAPI_Retvals BetFairAPIFree_5::getEvents(int eventid, vector<Events>&EventList)
{
  bool mysoap = false;
  MarketSummary MarketTmp;
  BFEvents BFEventTmp;
  Events EventTmp;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    gs2__APIRequestHeader BFgetEventsheader;           // Header to put session key into   
    gs2__GetEventsReq BFgetEventTypesReq;              // get events request class
    _gs1__getEvents BFgetEventTypesI;                  // getEvents request carrier class
    _gs1__getEventsResponse Response;                  // getEvents response carrier class

    // Set up where to respond to
    Response.soap = &m_soap;
    // Set up keepalive parameters
    BFgetEventsheader.sessionToken = (char *)m_sessiontoken.c_str();
    BFgetEventTypesReq.header = &BFgetEventsheader;
    BFgetEventTypesReq.eventParentId = eventid;
    // Set up parameter carrier
    BFgetEventTypesI.request = &BFgetEventTypesReq;
    BFgetEventTypesI.soap = &m_soap;
    // Make the Betfair API call, use defaults for SOAP Endpoint and API command 
    if (SOAP_OK == soap_call___gs1__getEvents(&m_soap, NULL, NULL, &BFgetEventTypesI, &Response )) {
      if (  gs2__APIErrorEnum__OK != Response.Result->header->errorCode ) {
        // Error at the BetFair level
        m_lasterrormsg = soap_gs2__APIErrorEnum2s(&m_soap, Response.Result->header->errorCode);
        m_sessiontoken = "";
        m_lasttimestamp = 0;
        if ( gs2__APIErrorEnum__NO_USCORESESSION == Response.Result->header->errorCode ) {
          // BetFair API no session handled specially as relogin will probably cure it
          retval = BFAPI_NOSESSION;
          printf("getEvents BetFair Error:%s\n", m_lasterrormsg.c_str());
        }
        else {
          // Any other BetFair API handled here
          retval = BFAPI_BFERROR;
          printf("getEvents BetFair Error:%s\n", m_lasterrormsg.c_str());
        }
      } else {
        // Successful getActiveEventTypes save the session information
        m_sessiontoken = Response.Result->header->sessionToken;
        m_lasttimestamp = Response.Result->header->timestamp;
        EventTmp.errorCode = Response.Result->errorCode;
		if ( gs2__GetEventsErrorEnum__OK == Response.Result->errorCode ) {
		  retval = BFAPI_OK;
		  EventTmp.eventParentId = Response.Result->eventParentId;
		  EventTmp.minorErrorCode = Response.Result->minorErrorCode;
          // TODO couponLinks

		  // Loop through Eventitems
		  EventTmp.eventItems.clear();
		  if ( Response.Result->eventItems->__sizeBFEvent > 0 ) {
            for ( int x = 0; x < Response.Result->eventItems->__sizeBFEvent; x++ ) {
              BFEventTmp.eventId = Response.Result->eventItems->BFEvent[x]->eventId;
			  BFEventTmp.eventName = Response.Result->eventItems->BFEvent[x]->eventName;
			  BFEventTmp.eventTypeId = Response.Result->eventItems->BFEvent[x]->eventTypeId;
			  BFEventTmp.menuLevel =  Response.Result->eventItems->BFEvent[x]->menuLevel;
			  BFEventTmp.orderIndex = Response.Result->eventItems->BFEvent[x]->orderIndex;
			  BFEventTmp.startTime = Response.Result->eventItems->BFEvent[x]->startTime;
			  BFEventTmp.timezone =  Response.Result->eventItems->BFEvent[x]->timezone;
			  EventTmp.eventItems.push_back(BFEventTmp);
            }
          }
		  // Loop through MarketSummaries
		  EventTmp.marketItems.clear();
		  if ( Response.Result->marketItems->__sizeMarketSummary > 0 ) {
            for ( int x = 0; x < Response.Result->marketItems->__sizeMarketSummary; x++ ) {
			  MarketTmp.eventTypeId = Response.Result->marketItems->MarketSummary[x]->eventTypeId;
			  MarketTmp.exchangeId = Response.Result->marketItems->MarketSummary[x]->exchangeId;
			  MarketTmp.eventParentId = Response.Result->marketItems->MarketSummary[x]->eventParentId;
			  MarketTmp.marketId = Response.Result->marketItems->MarketSummary[x]->marketId;
			  MarketTmp.marketName = Response.Result->marketItems->MarketSummary[x]->marketName;
			  MarketTmp.marketType = Response.Result->marketItems->MarketSummary[x]->marketType;
			  MarketTmp.marketTypeVariant = Response.Result->marketItems->MarketSummary[x]->marketTypeVariant;
			  MarketTmp.menuLevel = Response.Result->marketItems->MarketSummary[x]->menuLevel;
			  MarketTmp.orderIndex = Response.Result->marketItems->MarketSummary[x]->orderIndex;
			  MarketTmp.startTime =  Response.Result->marketItems->MarketSummary[x]->startTime;
			  MarketTmp.timeZone =  Response.Result->marketItems->MarketSummary[x]->timezone;
			  MarketTmp.venue =  Response.Result->marketItems->MarketSummary[x]->venue;
			  MarketTmp.numberOfWinners = Response.Result->marketItems->MarketSummary[x]->numberOfWinners;
			  MarketTmp.betDelay = Response.Result->marketItems->MarketSummary[x]->betDelay;
			  EventTmp.marketItems.push_back(MarketTmp);
            }
          }
		  EventList.push_back(EventTmp);
		} else {
          // Any BetFair error populating items handled here
          retval = BFAPI_BFERROR;
		  switch ( Response.Result->errorCode ) {
            case gs2__GetEventsErrorEnum__INVALID_USCOREEVENT_USCOREID:
              printf("getEvents BetFair Error: Invalid parent id or parent has no children\n" );
			  break;
            case gs2__GetEventsErrorEnum__NO_USCORERESULTS:
                printf("getEvents BetFair Error: No data available to retrun\n" );
			  break;
            case gs2__GetEventsErrorEnum__INVALID_USCORELOCALE_USCOREDEFAULTING_USCORETO_USCOREENGLISH:
              printf("getEvents BetFair Error: Invalid Local passed reverting to English\n" );
			  break;
			case gs2__GetEventsErrorEnum__API_USCOREERROR:
                printf("getEvents BetFair Error: General API Error\n" );
				break;
			default:
  			    printf("getEvents Error: Unknown error occurred code : %d\n", Response.Result->errorCode );
		  }
          printf("getEvents BetFair Error:%s\n", m_lasterrormsg.c_str());
		}
	  }
    } else {
      // Error at the SOAP level
      retval = BFAPI_SOAPERROR;
      printf("getEvents SOAP Error\n");
	}
	soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
    retval = BFAPI_OK;
  }
  return ( retval );
}
//
//
//
BFAPI_Retvals BetFairAPIFree_5::getMarket(int eventid, vector<MarketData>&MarketDataList)
{
  bool mysoap = false;
  MarketData MarketTmp;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    es2__APIRequestHeader BFgetMarketheader;           // Header to put session key into   
	es2__GetMarketReq BFgetMarketReq;                  // getMarket request class
    _es1__getMarket BFgetMarketI;                      // getMarket request carrier class
    _es1__getMarketResponse Response;                  // getMarket response carrier class

    // Set up where to respond to
    Response.soap = &m_soap;
    // Set up keepalive parameters
    BFgetMarketheader.sessionToken = (char *)m_sessiontoken.c_str();
    BFgetMarketReq.header = &BFgetMarketheader;
    BFgetMarketReq.marketId = eventid;
    // Set up parameter carrier
    BFgetMarketI.request = &BFgetMarketReq;
    BFgetMarketI.soap = &m_soap;
    // Make the Betfair API call, use defaults for SOAP Endpoint and API command 
    if (SOAP_OK == soap_call___es1__getMarket(&m_soap, NULL, NULL, &BFgetMarketI, &Response)) {
      if (  gs2__APIErrorEnum__OK != Response.Result->header->errorCode ) {
        // Error at the BetFair level
        m_lasterrormsg = soap_es2__APIErrorEnum2s(&m_soap, Response.Result->header->errorCode);
        m_sessiontoken = "";
        m_lasttimestamp = 0;
        if ( gs2__APIErrorEnum__NO_USCORESESSION == Response.Result->header->errorCode ) {
          // BetFair API no session handled specially as relogin will probably cure it
          retval = BFAPI_NOSESSION;
          printf("getMarket BetFair Error:%s\n", m_lasterrormsg.c_str());
        } else {
          // Any other BetFair API errors handled here
          retval = BFAPI_BFERROR;
          printf("getMarket BetFair Error:%s\n", m_lasterrormsg.c_str());
        }
      } else {
        // Successful getMarket save the session information
        m_sessiontoken = Response.Result->header->sessionToken;
        m_lasttimestamp = Response.Result->header->timestamp;
		MarketTmp.errorCode = Response.Result->errorCode;
        if ( gs2__GetEventsErrorEnum__OK == Response.Result->errorCode ) {
		  retval = BFAPI_OK;
          // Get the required market data from the retrieved values
		  if ( Response.Result->market != NULL ) {
			MarketTmp.countryISO3 = Response.Result->market->countryISO3;
			MarketTmp.discountAllowed = Response.Result->market->discountAllowed;
			MarketTmp.eventTypeId = Response.Result->market->eventTypeId;
			MarketTmp.lastRefresh = Response.Result->market->lastRefresh;
			MarketTmp.licenceId = Response.Result->market->licenceId;
			MarketTmp.marketBaseRate = Response.Result->market->marketBaseRate;
			MarketTmp.marketDescription = Response.Result->market->marketDescription;
			MarketTmp.marketDescriptionHasDate = Response.Result->market->marketDescriptionHasDate;
			MarketTmp.marketDisplayTime = Response.Result->market->marketDisplayTime;
			MarketTmp.marketId = Response.Result->market->marketId;
			MarketTmp.marketStatus = (BFAPI_MktStatus)Response.Result->market->marketStatus;
			MarketTmp.marketSuspendTime = Response.Result->market->marketSuspendTime;
			MarketTmp.marketTime = Response.Result->market->marketTime;
			MarketTmp.marketType = Response.Result->market->marketType;
			MarketTmp.marketTypeVariant = Response.Result->market->marketTypeVariant;
			MarketTmp.menuPath = Response.Result->market->menuPath;
			MarketTmp.name = Response.Result->market->name;
			MarketTmp.numberOfWinners = Response.Result->market->numberOfWinners;
			MarketTmp.parentEventId = Response.Result->market->parentEventId;
			int tmpu = (int)*Response.Result->market->unit;
			MarketTmp.unit = tmpu;
			int tminuv = (int)*Response.Result->market->minUnitValue;
			MarketTmp.minUnitValue = tminuv;
            int tmaxuv = (int)*Response.Result->market->maxUnitValue;
			MarketTmp.maxUnitValue = tmaxuv;
			int tint = (int)*Response.Result->market->interval;
            MarketTmp.interval = tint;
			MarketTmp.runnersMayBeAdded = Response.Result->market->runnersMayBeAdded;
			MarketTmp.timezone = Response.Result->market->timezone;
            int numrunners = Response.Result->market->runners->__sizeRunner;
			if ( numrunners > 0 ) {
		      MarketRunners arunner;
		  	  for (int j=0; j<numrunners; j++) {
			    arunner.asianLineId = Response.Result->market->runners->Runner[j]->asianLineId;
				arunner.handicap = Response.Result->market->runners->Runner[j]->handicap;
				arunner.name = Response.Result->market->runners->Runner[j]->name;
				arunner.selectionId = Response.Result->market->runners->Runner[j]->selectionId;
				MarketTmp.runners.push_back(arunner);
			  }
			}
			MarketDataList.push_back(MarketTmp);
		  }	else {
            retval = BFAPI_BFERROR;
            printf("getMarket Market is NULL Error\n");
		  }
	    } else {
          // Any BetFair error populating items handled here
          retval = BFAPI_BFERROR;
		  switch ( Response.Result->errorCode ) {
            case gs2__GetEventsErrorEnum__INVALID_USCOREEVENT_USCOREID:
              printf("getmarket BetFair Error: Invalid parent id or parent has no children\n" );
			  break;
            case gs2__GetEventsErrorEnum__NO_USCORERESULTS:
                printf("getmarket BetFair Error: No data available to retrun\n" );
			  break;
            case gs2__GetEventsErrorEnum__INVALID_USCORELOCALE_USCOREDEFAULTING_USCORETO_USCOREENGLISH:
              printf("getmarket BetFair Error: Invalid Local passed reverting to English\n" );
			  break;
			case gs2__GetEventsErrorEnum__API_USCOREERROR:
                printf("getMarket BetFair Error: General API Error\n" );
				break;
			default:
  			    printf("getMarket Error: Unknown error occurred code : %d\n", Response.Result->errorCode );
		  }
		}
	  }
	} else {
        // Error at the SOAP level
        retval = BFAPI_SOAPERROR;
        printf("getMarket SOAP Error\n");
	}
	soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
    retval = BFAPI_OK;
  }
  return ( retval );
}
//
//
//
BFAPI_Retvals BetFairAPIFree_5::getMarketPrices(int marketid, vector<MarketPrices>&TheMarketPrices)
{
  bool mysoap = false;
  MarketPrices MarketPricesTmp = {};
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    es2__APIRequestHeader BFgetMarketPricesheader;     // Header to put session key into   
	es2__GetMarketPricesReq BFgetMarketPricesReq;      // getMarketPrices request class
    _es1__getMarketPrices BFgetMarketPricesI;          // getMarketPrices request carrier class
    _es1__getMarketPricesResponse Response;            // getMarketPrices response carrier class
    // Set up where to respond to
    Response.soap = &m_soap;
    // Set up keepalive parameters
    BFgetMarketPricesheader.sessionToken = (char *)m_sessiontoken.c_str();
    BFgetMarketPricesReq.header = &BFgetMarketPricesheader;
    BFgetMarketPricesReq.marketId = marketid;
    // Set up parameter carrier
    BFgetMarketPricesI.request = &BFgetMarketPricesReq;
    BFgetMarketPricesI.soap = &m_soap;
    // Make the Betfair API call, use defaults for SOAP Endpoint and API command 
    if (SOAP_OK == soap_call___es1__getMarketPrices(&m_soap, NULL, NULL, &BFgetMarketPricesI, &Response)) {
      if (  gs2__APIErrorEnum__OK != Response.Result->header->errorCode ) {
        // Error at the BetFair level
        m_lasterrormsg = soap_es2__APIErrorEnum2s(&m_soap, Response.Result->header->errorCode);
        m_sessiontoken = "";
        m_lasttimestamp = 0;
        if ( gs2__APIErrorEnum__NO_USCORESESSION == Response.Result->header->errorCode ) {
          // BetFair API no session handled specially as relogin will probably cure it
          retval = BFAPI_NOSESSION;
          printf("getMarketPrices BetFair Error:%s\n", m_lasterrormsg.c_str());
        } else {
          // Any other BetFair API errors handled here
          retval = BFAPI_BFERROR;
          printf("getMarketPrices BetFair Error:%s\n", m_lasterrormsg.c_str());
        }
      } else {
        // Successful getMarketPrices save the session information
        m_sessiontoken = Response.Result->header->sessionToken;
        m_lasttimestamp = Response.Result->header->timestamp;
		MarketPricesTmp.errorCode = Response.Result->errorCode;
        if ( gs2__GetEventsErrorEnum__OK == Response.Result->errorCode ) {
		  retval = BFAPI_OK;
          // Get the required market prices from the retrieved values
		  if ( Response.Result->marketPrices != NULL ) {
            MarketPricesTmp.currencyCode = Response.Result->marketPrices->currencyCode;
            MarketPricesTmp.delay = Response.Result->marketPrices->delay;
		    MarketPricesTmp.discountAllowed = Response.Result->marketPrices->discountAllowed;
            MarketPricesTmp.lastRefresh = Response.Result->marketPrices->lastRefresh;
		    MarketPricesTmp.marketBaseRate = Response.Result->marketPrices->marketBaseRate;
		    MarketPricesTmp.marketid = Response.Result->marketPrices->marketId;
		    MarketPricesTmp.marketStatus = Response.Result->marketPrices->marketStatus;
		    MarketPricesTmp.numberOfWinners = Response.Result->marketPrices->numberOfWinners;
		    MarketPricesTmp.removedRunners = Response.Result->marketPrices->removedRunners;
		    MarketPricesTmp.marketInfo = Response.Result->marketPrices->marketInfo;
		    int numrunners = Response.Result->marketPrices->runnerPrices->__sizeRunnerPrices;
		    if ( numrunners > 0 ) {
              RPrices arprice = {};
			  for (int j=0; j<numrunners; j++) {
			    arprice.asianLineId = *Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->asianLineId;
                arprice.handicap = *Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->handicap;
                arprice.lastPriceMatched = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->lastPriceMatched;
                arprice.reductionFactor = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->reductionFactor;
                arprice.selectionId = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->selectionId;
                arprice.sortOrder = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->sortOrder;
                arprice.totalAmountMatched = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->totalAmountMatched;
                arprice.vacant = *Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->vacant;
			    Prices lprice = {};
			    int layprices = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->bestPricesToLay->__sizePrice;
				for (int k=0; k<layprices; k++) {
			      lprice.amountAvailable = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->bestPricesToLay->Price[k]->amountAvailable;
				  lprice.betType= Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->bestPricesToLay->Price[k]->betType;
				  lprice.depth = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->bestPricesToLay->Price[k]->depth;
				  lprice.price = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->bestPricesToLay->Price[k]->price;
				  arprice.bestPricesToLay.push_back(lprice);
				}
			    Prices bprice = {};
			    int backprices = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->bestPricesToBack->__sizePrice;
				for (int l=0; l<backprices; l++) {
			      bprice.amountAvailable = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->bestPricesToBack->Price[l]->amountAvailable;
				  bprice.betType= Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->bestPricesToBack->Price[l]->betType;
				  bprice.depth = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->bestPricesToBack->Price[l]->depth;
				  bprice.price = Response.Result->marketPrices->runnerPrices->RunnerPrices[j]->bestPricesToBack->Price[l]->price;
				  arprice.bestPricesToBack.push_back(bprice);
		        }
			    MarketPricesTmp.runnerPrices.push_back(arprice);
			    arprice.bestPricesToBack.clear();
			    arprice.bestPricesToLay.clear();
			  }
		    }
		    TheMarketPrices.push_back(MarketPricesTmp);
		    retval = BFAPI_OK;
		  } else {
            // Any BetFair error populating items handled here
            retval = BFAPI_BFERROR;
		    switch ( Response.Result->errorCode ) {
              case gs2__GetEventsErrorEnum__INVALID_USCOREEVENT_USCOREID:
                printf("getMarketPrices BetFair Error: Invalid parent id or parent has no children\n" );
			    break;
              case gs2__GetEventsErrorEnum__NO_USCORERESULTS:
                printf("getMarketPrices BetFair Error: No data available to retrun\n" );
			  break;
              case gs2__GetEventsErrorEnum__INVALID_USCORELOCALE_USCOREDEFAULTING_USCORETO_USCOREENGLISH:
                printf("getMarketPrices BetFair Error: Invalid Local passed reverting to English\n" );
			    break;
			  case gs2__GetEventsErrorEnum__API_USCOREERROR:
                printf("getMarketPrices BetFair Error: General API Error\n" );
				break;
			  default:
  			    printf("getMarketPrices Error: Unknown error occurred code : %d\n", Response.Result->errorCode );
		    }
		  }
        } else {
          retval = BFAPI_BFERROR;
          printf("getMarketPrices  is NULL Error\n");
		}
	  }
    } else {
      // Error at the SOAP level
      retval = BFAPI_SOAPERROR;
      printf("getMarketPrices SOAP Error\n");
	}
	soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
  }
  return ( retval );
}
//
//
//
BFAPI_Retvals BetFairAPIFree_5::getMarketPricesCompressed(int marketid, vector<MarketPrices>&TheMarketPrices)
{
  bool mysoap = false;
  MarketPrices MarketPricesTmp = {};
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  vector<std::string> compitems;
  vector<std::string> runitems;
  vector<std::string> runnerbits;
  vector<std::string> runnerpricesbits;
  vector<std::string>runnerpricesbacklay;
  std::string rundata;
  if ( mysoap ) {
    es2__APIRequestHeader BFgetMarketPricesCompressedheader;           // Header to put session key into   
	es2__GetMarketPricesCompressedReq BFgetMarketPricesCompressedReq;  // getMarketPricesCompressed request class
    _es1__getMarketPricesCompressed BFgetMarketPricesCompressedI;      // getMarketPricesCompressed request carrier class
    _es1__getMarketPricesCompressedResponse Response;                  // getMarketPricesCompressed response carrier class
    // Set up where to respond to
    Response.soap = &m_soap;
    // Set up keepalive parameters
    BFgetMarketPricesCompressedheader.sessionToken = (char *)m_sessiontoken.c_str();
    BFgetMarketPricesCompressedReq.header = &BFgetMarketPricesCompressedheader;
    BFgetMarketPricesCompressedReq.marketId = marketid;
    // Set up parameter carrier
    BFgetMarketPricesCompressedI.request = &BFgetMarketPricesCompressedReq;
    BFgetMarketPricesCompressedI.soap = &m_soap;
    // Make the Betfair API call, use defaults for SOAP Endpoint and API command 
    if (SOAP_OK == soap_call___es1__getMarketPricesCompressed(&m_soap, NULL, NULL, &BFgetMarketPricesCompressedI, &Response)) {
      if (  gs2__APIErrorEnum__OK != Response.Result->header->errorCode ) {
        // Error at the BetFair level
        m_lasterrormsg = soap_es2__APIErrorEnum2s(&m_soap, Response.Result->header->errorCode);
        m_sessiontoken = "";
        m_lasttimestamp = 0;
        if ( gs2__APIErrorEnum__NO_USCORESESSION == Response.Result->header->errorCode ) {
          // BetFair API no session handled specially as relogin will probably cure it
          retval = BFAPI_NOSESSION;
          printf("getMarketPricesCompressed BetFair Error:%s\n", m_lasterrormsg.c_str());
        } else {
          // Any other BetFair API errors handled here
          retval = BFAPI_BFERROR;
          printf("getMarketPricesCompressed BetFair Error:%s\n", m_lasterrormsg.c_str());
        }
      } else {
        // Successful getMarketPricesCompressed save the session information
        m_sessiontoken = Response.Result->header->sessionToken;
        m_lasttimestamp = Response.Result->header->timestamp;
		MarketPricesTmp.errorCode = Response.Result->errorCode;
        if ( gs2__GetEventsErrorEnum__OK == Response.Result->errorCode ) {
		  retval = BFAPI_OK;
          // Get the required market prices from the retrieved values
		  if ( Response.Result->marketPrices != NULL ) {
            // First load a vector of strings with all the members of the ~ delimited data string
		    compitems.clear();
            std::stringstream ss(Response.Result->marketPrices);
            std::string item;
            while (std::getline(ss, item, '~')) {
              compitems.push_back(item);
            }
		    vector<std::string>::iterator mktitems = compitems.begin();
		    MarketPricesTmp.marketid = atoi(mktitems->c_str()); // 1
		    mktitems++;
		    MarketPricesTmp.currencyCode = mktitems->c_str(); // 2
		    mktitems++;
		    MarketPricesTmp.marketStatus = atoi(mktitems->c_str()); // 3
		    mktitems++;
            MarketPricesTmp.delay = atoi(mktitems->c_str()); // 4
		    mktitems++;
		    MarketPricesTmp.numberOfWinners = atoi(mktitems->c_str()); // 5
  		    mktitems++;
		    MarketPricesTmp.marketInfo = mktitems->c_str(); // 6
		    mktitems++;
		    MarketPricesTmp.discountAllowed = atoi(mktitems->c_str()); // 7
		    mktitems++;
		    MarketPricesTmp.marketBaseRate = atof(mktitems->c_str()); // 8
            mktitems++;
            MarketPricesTmp.lastRefresh = atoi(mktitems->c_str()); // 9
            mktitems++;
		    // Now re-construct a , delimited string from remaining items
		    rundata = "";
		    while ( mktitems != compitems.end() ) {
			  rundata.append(mktitems->c_str());
			  rundata.append(",");
			  mktitems++;
		    }
		    // Clear no longer needed vector of compitems
            compitems.clear();
            // Split string built into chunks of runner information
		    // TODO: Look at escaped characters 
		    runitems.clear();
		    std::stringstream rr(rundata.c_str());
            std::string ritem;
		    int countr = 0;
		    while (std::getline(rr, ritem, ':')) {
              runitems.push_back(ritem);
			  countr++;
            }
		    if ( countr > 1 ) {
		      vector<std::string>::iterator runitemsit;
		      vector<std::string>::iterator runnerpricesit;
			  vector<std::string>::iterator runnerpricesheaderit;
			  vector<std::string>::iterator backlaybitsit;
			  RPrices arprice = {};
			  runitemsit = runitems.begin();;
			  // TODO:: Currently jumping first entry which is non runners
			  runitemsit++;
			  // Work through runner information
			  for (; runitemsit != runitems.end(); runitemsit++) {
                // Create vector of up to 3 items:- runnerprices, bestpricestoback, bestpricestolay
			    // back and lay prices are not always present have to check L & B to see what each is
                runnerbits.clear();
			    std::stringstream rb(runitemsit->c_str());
                std::string rbitem;
		        while (std::getline(rb, rbitem, '|')) {
                  runnerbits.push_back(rbitem);
                }
			    runnerpricesit = runnerbits.begin();
  			    // Get the runnerprices information
			    // Create vector of runnerprices items from , delimited string in first vector member
                runnerpricesbits.clear();
			    std::stringstream rpb(runnerpricesit->c_str());
                std::string rpbitem;
		        while (std::getline(rpb, rpbitem, ',')) {
                  runnerpricesbits.push_back(rpbitem);
                }
			    runnerpricesheaderit = runnerpricesbits.begin();
                // TODO Error checking is the first item an integer
			    arprice.asianLineId = 0;  // Not supplied in Compressed data string
			    arprice.selectionId = atoi(runnerpricesheaderit->c_str());
                runnerpricesheaderit++;
                arprice.sortOrder = atoi(runnerpricesheaderit->c_str());
                runnerpricesheaderit++;
                arprice.totalAmountMatched = atof(runnerpricesheaderit->c_str());
                runnerpricesheaderit++;
                arprice.lastPriceMatched = atof(runnerpricesheaderit->c_str());
                runnerpricesheaderit++;
                arprice.handicap = atof(runnerpricesheaderit->c_str());
                runnerpricesheaderit++;
                arprice.reductionFactor = atof(runnerpricesheaderit->c_str());
                runnerpricesheaderit++;
                arprice.vacant = (atoi(runnerpricesheaderit->c_str()) != 0);
			    // Get the back/lay prices information
			    runnerpricesit++;
			    while ( runnerpricesit != runnerbits.end() ) {
			      // Create vector of back/lay prices from , delimited string in next vector member
                  runnerpricesbacklay.clear();
 			      std::stringstream rpbl(runnerpricesit->c_str());
                  std::string rpblitem;
                  while (std::getline(rpbl, rpblitem, ',')) {
                    runnerpricesbacklay.push_back(rpblitem);
                  }
				  Prices aprice = {};
				  backlaybitsit = runnerpricesbacklay.begin();
				  while ( backlaybitsit != runnerpricesbacklay.end() ) {
				    aprice.price = atof(backlaybitsit->c_str());
				    // Sometimes there is no Lay and or Back prices so check if we got some
				    if ( aprice.price > 0 ) {
                      backlaybitsit++;
				      aprice.amountAvailable = atof(backlaybitsit->c_str());
                      backlaybitsit++;
				      aprice.betType= backlaybitsit->c_str();
                      backlaybitsit++;
				      aprice.depth = atoi(backlaybitsit->c_str());
                      backlaybitsit++;
					  // Now decide whethe it was back or lay and save it
                      vector<std::string>::iterator backlayit;
  				      backlayit = runnerpricesbacklay.begin();
				      backlayit++;
				      backlayit++;
				      switch (backlayit->at(0)) {
				        case 'L':
				          arprice.bestPricesToBack.push_back(aprice);
                          break;
				        case 'B':
				          arprice.bestPricesToLay.push_back(aprice);
                          break;
				      };
				    } else {
  					  if ( backlaybitsit != runnerpricesbacklay.end() ) {
                        backlaybitsit++;
				 	  }
					  if ( backlaybitsit != runnerpricesbacklay.end() ) {
                        backlaybitsit++;
					  }
					  if ( backlaybitsit != runnerpricesbacklay.end() ) {
                        backlaybitsit++;
					  }
					  if ( backlaybitsit != runnerpricesbacklay.end() ) {
                        backlaybitsit++;
				 	  }
				    }
				  }
                  runnerpricesit++;
			    }
			    MarketPricesTmp.runnerPrices.push_back(arprice);
			    arprice.bestPricesToBack.clear();
			    arprice.bestPricesToLay.clear();
			  }
		    }
            TheMarketPrices.push_back(MarketPricesTmp);
		    retval = BFAPI_OK;
		  } else {
            // Error at the SOAP level
            retval = BFAPI_BFERROR;
            printf("getMarketPricesCompressed is NULL Error\n");
		  }
		} else {
          // Any BetFair error populating items handled here
          retval = BFAPI_BFERROR;
		  switch ( Response.Result->errorCode ) {
            case gs2__GetEventsErrorEnum__INVALID_USCOREEVENT_USCOREID:
              printf("getMarketPricesCompressed BetFair Error: Invalid parent id or parent has no children\n" );
		     break;
            case gs2__GetEventsErrorEnum__NO_USCORERESULTS:
              printf("getMarketPricesCompressed BetFair Error: No data available to retrun\n" );
			break;
            case gs2__GetEventsErrorEnum__INVALID_USCORELOCALE_USCOREDEFAULTING_USCORETO_USCOREENGLISH:
              printf("getMarketPricesCompressed BetFair Error: Invalid Locale passed reverting to English\n" );
			  break;
			case gs2__GetEventsErrorEnum__API_USCOREERROR:
              printf("getMarketPricesCompressed BetFair Error: General API Error\n" );
			  break;
			default:
			  printf("getMarketPricesCompressed Error: Unknown error occurred code : %d\n", Response.Result->errorCode );
		  }
		}
	  }
    } else {
      // Error at the SOAP level
      retval = BFAPI_SOAPERROR;
      printf("getMarketPricesCompressed SOAP Error\n");
    }
	soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
  }
  return ( retval );
}

//
// TODO: Add extra error handling to display call specifici error code (switch statement)
// Given a reference to a vector of structures to store the event data in, this loads up
// the Account Funds data retrieved from BetFair into the vector.
BFAPI_Retvals BetFairAPIFree_5::getAccountFunds(vector<AccountFunds>&AccountFList)
{
  bool mysoap = false;
  AccountFunds FundsTmp;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    es2__APIRequestHeader BFgetAccountFundsheader;     // Header to put session key into   
    es2__GetAccountFundsReq BFgetAccountFundsReq;      // get account funds request class
    _es1__getAccountFunds BFgetAccountFundsI;          // getAccountFunds request carrier class
    _es1__getAccountFundsResponse Response;            // getAccountFunds response carrier class

    // Set up where to respond to
    Response.soap = &m_soap;
    // Set up keepalive parameters
    BFgetAccountFundsheader.sessionToken = (char *)m_sessiontoken.c_str();
    BFgetAccountFundsReq.header = &BFgetAccountFundsheader;
    // Set up parameter carrier
    BFgetAccountFundsI.request = &BFgetAccountFundsReq;
    BFgetAccountFundsI.soap = &m_soap;
    // Make the Betfair API call, use defaults for SOAP Endpoint and API command 
    if (SOAP_OK == soap_call___es1__getAccountFunds(&m_soap, NULL, NULL, &BFgetAccountFundsI, &Response )) {
      if (  gs2__APIErrorEnum__OK != Response.Result->header->errorCode ) {
        // Error at the BetFair level
        m_lasterrormsg = soap_es2__APIErrorEnum2s(&m_soap, Response.Result->header->errorCode);
        m_sessiontoken = "";
        m_lasttimestamp = 0;
        if ( es2__APIErrorEnum__NO_USCORESESSION == Response.Result->header->errorCode ) {
          // BetFair API no session handled specially as relogin will probably cure it
          retval = BFAPI_NOSESSION;
          printf("getAccountFunds BetFair Error:%s\n", m_lasterrormsg.c_str());
        } else {
          // Any other BetFair API handled here
          retval = BFAPI_BFERROR;
          printf("getAccountFunds BetFair Error:%s\n", m_lasterrormsg.c_str());
        }
      } else {
        // Successful getAccountFunds save the session information
        m_sessiontoken = Response.Result->header->sessionToken;
        m_lasttimestamp = Response.Result->header->timestamp;
        retval = BFAPI_OK;
		// Retrieve values and load structure, convert money doubles to ints in pence
		double moneytmp = 0.0;
		moneytmp = Response.Result->availBalance;
		FundsTmp.availBalance = (int)(moneytmp*100+0.5);
        moneytmp = Response.Result->balance;
		FundsTmp.balance = (int)(moneytmp*100+0.5);
		moneytmp = Response.Result->commissionRetain;
		FundsTmp.commissionRetain = (int)(moneytmp*100+0.5);
        moneytmp = Response.Result->creditLimit;
		FundsTmp.creditLimit = (int)(moneytmp*100+0.5);
		FundsTmp.currentBetfairPoints = Response.Result->currentBetfairPoints;
		FundsTmp.errorCode = Response.Result->errorCode;
        moneytmp = Response.Result->expoLimit;
		FundsTmp.expoLimit = (int)(moneytmp*100-0.5);
		moneytmp = Response.Result->exposure;
		FundsTmp.exposure = (int)(moneytmp*100-0.5);
		FundsTmp.holidaysAvail = Response.Result->holidaysAvailable;
		FundsTmp.minorErrorCode = Response.Result->minorErrorCode;
		FundsTmp.nextDiscount = Response.Result->nextDiscount;
        moneytmp = Response.Result->withdrawBalance;
		FundsTmp.withdrawBalance = (int)(moneytmp*100+0.5);
        AccountFList.push_back(FundsTmp);
	  }
    } else {
      // Error at the SOAP level
      retval = BFAPI_SOAPERROR;
      printf("getAccountFunds SOAP Error\n");
    }
	soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
    retval = BFAPI_OK;
  }
  return ( retval );
}

//
// Given a reference to a vector of structures containing bets to place, this attempts to place those bets
// and returns an array of results, one for each bet in the request array.
// Currently this function will place 1 bet only
BFAPI_Retvals BetFairAPIFree_5::placeBets(vector<PlaceABet>&BetPlaceRequest, vector<PlaceABetResp>&PlaceBetResponse)
{
  bool mysoap = false;
  AccountFunds FundsTmp;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    // Prepare the array of bet requests, assume only 1 bet in a request for the moment
    es2__ArrayOfPlaceBets theBets;
	es2__PlaceBets aBet[1];
	vector<PlaceABet>::iterator byit = BetPlaceRequest.begin();
	if ( byit == BetPlaceRequest.end() ) {
      return (BFAPI_BADIPDATA);
	}
	// Load bet request parameters into SOAP structure
	// aBet = es2__PlaceBets
	aBet[0].asianLineId = byit->asianLineId;
	aBet[0].betType = (char *)byit->betType.c_str();
	aBet[0].marketId = byit->marketID;
	aBet[0].price = byit->price;
	aBet[0].selectionId = byit->selectionID;
	aBet[0].size = byit->size;                         // Size of bet
	aBet[0].betCategoryType = (char *)byit->betCategoryType.c_str();
	aBet[0].betPersistenceType = (char *)byit->betPersistenceType.c_str();
	aBet[0].bspLiability = byit->bspLiability;
    // theBets = es2__ArrayOfPlaceBets
	theBets.__sizePlaceBets = 1;
	// Get a pointer to the array of bets
	es2__PlaceBets *thebets = &aBet[0];
	// Give the address of that pointer a **
	theBets.PlaceBets = &thebets;
    es2__APIRequestHeader BFplaceABetheader;           // Header to put session key into   
    es2__PlaceBetsReq BFplaceABetReq;                  // place an array of bets
    _es1__placeBets BFplaceABetI;                      // placeBets request carrier class
    _es1__placeBetsResponse Response;                  // placeBets response carrier class
	// Set up where to respond to
    Response.soap = &m_soap;
    // Set up keepalive parameters
    BFplaceABetheader.sessionToken = (char *)m_sessiontoken.c_str();
	BFplaceABetReq.header = &BFplaceABetheader;
    // Set up parameter carrier
	BFplaceABetI.request = &BFplaceABetReq;
	BFplaceABetI.soap = &m_soap;
    // Put bets information into request
	BFplaceABetI.request->bets = &theBets;
	// Make the Betfair API call, use defaults for SOAP Endpoint and API command 
    // _es1__placeBets *es1__placeBets, _es1__placeBetsResponse *es1__placeBetsResponse
    int soapret = 0;
    if (SOAP_OK == (soapret = soap_call___es1__placeBets(&m_soap, NULL, NULL, &BFplaceABetI, &Response ))) {
      if (  es2__APIErrorEnum__OK != Response.Result->header->errorCode ) {
        // Error at the BetFair level
        m_lasterrormsg = soap_es2__APIErrorEnum2s(&m_soap, Response.Result->header->errorCode);
        m_sessiontoken = "";
        m_lasttimestamp = 0;
        if ( es2__APIErrorEnum__NO_USCORESESSION == Response.Result->header->errorCode ) {
          // BetFair API no session handled specially as relogin will probably cure it
          retval = BFAPI_NOSESSION;
          printf("placeBet BetFair Error:%s\n", m_lasterrormsg.c_str());
        } else {
          // Any other BetFair API handled here
          retval = BFAPI_BFERROR;
          printf("placeBet BetFair Error:%s\n", m_lasterrormsg.c_str());
        }
      } else {
	    // Header said OK so save sessionToken and timestamp
        m_sessiontoken = Response.Result->header->sessionToken;
        m_lasttimestamp = Response.Result->header->timestamp;
		// Now check overall bet placement result
		if (es2__PlaceBetsErrorEnum__OK != Response.Result->errorCode ) {
 		  switch ( Response.Result->errorCode ) {
			case es2__PlaceBetsErrorEnum__BETWEEN_USCORE1_USCOREAND_USCORE60_USCOREBETS_USCOREREQUIRED:
              printf("placeBets Error: Between 1 and 60 Bets Required\n" );
			  break;
			case es2__PlaceBetsErrorEnum__EVENT_USCOREINACTIVE:
                printf("placeBets Error: Market is Not Active\n" );
			  break;
			case es2__PlaceBetsErrorEnum__EVENT_USCORECLOSED:
              printf("placeBets Error: Market is Closed\n" );
			  break;
			case es2__PlaceBetsErrorEnum__EVENT_USCORESUSPENDED:
                printf("placeBets Error: Market is Suspended\n" );
				break;
			case es2__PlaceBetsErrorEnum__ACCOUNT_USCORECLOSED:
                printf("placeBets Error: Account Closed\n" );
				break;
			case es2__PlaceBetsErrorEnum__ACCOUNT_USCORESUSPENDED:
                printf("placeBets Error: Account Suspended\n" );
				break;
			case es2__PlaceBetsErrorEnum__AUTHORISATION_USCOREPENDING:
                printf("placeBets Error: Account is pending authorisation\n" );
				break;
			case es2__PlaceBetsErrorEnum__INTERNAL_USCOREERROR:
                printf("placeBets Error: Internal Error\n" );
				break;
			case es2__PlaceBetsErrorEnum__SITE_USCOREUPGRADE:
                printf("placeBets Error: Site is currently being upgraded\n" );
				break;
			case es2__PlaceBetsErrorEnum__BACK_USCORELAY_USCORECOMBINATION:
                printf("placeBets Error: Back and Lay on Same Market and Back Price is Less than or Equal to Lay Price\n" );
				break;
			case es2__PlaceBetsErrorEnum__INVALID_USCOREMARKET:
                printf("placeBets Error: Invalid Market\n" );
				break;
			case es2__PlaceBetsErrorEnum__MARKET_USCORETYPE_USCORENOT_USCORESUPPORTED:
                printf("placeBets Error: Bet Type Not Supported\n" );
				break;
			case es2__PlaceBetsErrorEnum__DIFFERING_USCOREMARKETS:
                printf("placeBets Error: All Bets Not for the Same Market\n" );
				break;
			case es2__PlaceBetsErrorEnum__FROM_USCORECOUNTRY_USCOREFORBIDDEN:
                printf("placeBets Error: Attempt To Place Bet From Forbidden Country\n" );
				break;
			case es2__PlaceBetsErrorEnum__API_USCOREERROR:
                printf("placeBets Error: API Error\n" );
				break;
			default:
				printf("placeBets Error: Unknown error occurred code : %d\n", Response.Result->errorCode );
		  }
          retval = BFAPI_BFERROR;
		} else {
		  // So header said OK and overall placement said OK now get the bet result itself
  		  // Retrieve values and load structure
          PlaceABetResp tmpbetresp;
		  if (1 == Response.Result->betResults->__sizePlaceBetsResult) {
			  // Got one bet placement result as expected
		    tmpbetresp.averagePriceMatched = Response.Result->betResults->PlaceBetsResult[0]->averagePriceMatched;
		    tmpbetresp.betId = Response.Result->betResults->PlaceBetsResult[0]->betId;
		    tmpbetresp.resultCode = Response.Result->betResults->PlaceBetsResult[0]->resultCode;
			tmpbetresp.sizeMatched = Response.Result->betResults->PlaceBetsResult[0]->sizeMatched;
		    tmpbetresp.success = Response.Result->betResults->PlaceBetsResult[0]->success;
            PlaceBetResponse.push_back(tmpbetresp);
	        retval = BFAPI_OK;
		  } else {
			  printf("placeBets Error: Unexpected number of place bets responses, expect got : 1 got : %d\n", Response.Result->betResults->__sizePlaceBetsResult );
            retval = BFAPI_BFERROR;
		  }
		}
	  }
    } else {
      // Error at the SOAP level
      retval = BFAPI_SOAPERROR;
	  printf("placeBet SOAP Error code : %d\n", soapret );
    }
	soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
    retval = BFAPI_OK;
  }
  return ( retval );
}

//
//
//
BFAPI_Retvals BetFairAPIFree_5::getABet(LONG64 betid, vector<GetABetResp>&BetDetails)
{
  bool mysoap = false;
  GetABetResp GetABetRespTmp = {};
  GBetMatches GetABetMatchesTmp = {};
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    es2__APIRequestHeader BFgetBetheader;                              // Header to put session key into   
	es2__GetBetReq BFgetBetReq;                                        // getBet request class
    _es1__getBet BFgetBetI;                                            // getBet request carrier class
    _es1__getBetResponse Response;                                     // getBet response carrier class
    // Set up where to respond to
    Response.soap = &m_soap;
    // Set up keepalive parameters
    BFgetBetheader.sessionToken = (char *)m_sessiontoken.c_str();
    BFgetBetReq.header = &BFgetBetheader;
    BFgetBetReq.betId = betid;
    // Set up parameter carrier
    BFgetBetI.request = &BFgetBetReq;
    BFgetBetI.soap = &m_soap;
    // Make the Betfair API call, use defaults for SOAP Endpoint and API command 
	if (SOAP_OK == soap_call___es1__getBet(&m_soap, NULL, NULL, &BFgetBetI, &Response)) {
      if (  gs2__APIErrorEnum__OK != Response.Result->header->errorCode ) {
        // Error at the BetFair level
        m_lasterrormsg = soap_es2__APIErrorEnum2s(&m_soap, Response.Result->header->errorCode);
        m_sessiontoken = "";
        m_lasttimestamp = 0;
        if ( gs2__APIErrorEnum__NO_USCORESESSION == Response.Result->header->errorCode ) {
          // BetFair API no session handled specially as relogin will probably cure it
          retval = BFAPI_NOSESSION;
          printf("getABet BetFair Error:%s\n", m_lasterrormsg.c_str());
        } else {
          // Any other BetFair API errors handled here
          retval = BFAPI_BFERROR;
          printf("getABet BetFair Error:%s\n", m_lasterrormsg.c_str());
        }
      } else {
        // Successful getBet save the session information
        m_sessiontoken = Response.Result->header->sessionToken;
        m_lasttimestamp = Response.Result->header->timestamp;
        if ( gs2__GetEventsErrorEnum__OK == Response.Result->errorCode ) {
		  retval = BFAPI_OK;
          // Get all the bet information from the retrieved values
          vector<GBetMatches> TheMatches;
		  GetABetRespTmp.asianLineId = Response.Result->bet->asianLineId;
		  GetABetRespTmp.avgPrice = Response.Result->bet->avgPrice;
		  GetABetRespTmp.betId = Response.Result->bet->betId;
		  GetABetRespTmp.betStatus = Response.Result->bet->betStatus;
		  GetABetRespTmp.betType = (char)Response.Result->bet->betType;
		  GetABetRespTmp.cancelledDate = Response.Result->bet->cancelledDate;
		  GetABetRespTmp.executedBy = "";  // Not Used
		  GetABetRespTmp.fullMarketString = Response.Result->bet->fullMarketName;
		  GetABetRespTmp.handicap = Response.Result->bet->handicap;
		  GetABetRespTmp.lapsedDate = Response.Result->bet->lapsedDate;
		  GetABetRespTmp.marketID = Response.Result->bet->marketId;
		  GetABetRespTmp.marketName = Response.Result->bet->marketName;
		  GetABetRespTmp.marketType = (char)Response.Result->bet->marketType;
		  GetABetRespTmp.marketTypeVariant = Response.Result->bet->marketTypeVariant;
		  GetABetRespTmp.matchedDate = Response.Result->bet->matchedDate;
		  GetABetRespTmp.matchedSize = Response.Result->bet->matchedSize;
		  GetABetRespTmp.placedDate = Response.Result->bet->placedDate;
		  GetABetRespTmp.price = Response.Result->bet->price;
		  GetABetRespTmp.profitAndLoss = Response.Result->bet->profitAndLoss;
		  GetABetRespTmp.remainingSize = Response.Result->bet->remainingSize;
		  GetABetRespTmp.requestedSize = Response.Result->bet->requestedSize;
		  GetABetRespTmp.selectionId = Response.Result->bet->selectionId;
		  GetABetRespTmp.selectionName = Response.Result->bet->selectionName;
		  GetABetRespTmp.settledDate = Response.Result->bet->settledDate;
		  GetABetRespTmp.voidedDate = Response.Result->bet->voidedDate;
		  // Get the matching parts if some has been matched
		  if  ( Response.Result->bet->matchedSize > 0.0 ) {
            for ( int h=0; h < Response.Result->bet->matches->__sizeMatch; h++ ) {
	          GetABetMatchesTmp.betStatus = Response.Result->bet->matches->Match[h]->betStatus;
			  GetABetMatchesTmp.matchedDate = Response.Result->bet->matches->Match[h]->matchedDate;
			  GetABetMatchesTmp.priceMatched = Response.Result->bet->matches->Match[h]->priceMatched;
			  GetABetMatchesTmp.profitLoss = Response.Result->bet->matches->Match[h]->profitLoss;
			  GetABetMatchesTmp.settledDate = Response.Result->bet->matches->Match[h]->settledDate;
			  GetABetMatchesTmp.sizeMatched = Response.Result->bet->matches->Match[h]->sizeMatched;
	  		  GetABetRespTmp.matches.push_back(GetABetMatchesTmp);
		    }
		  }
		  BetDetails.push_back(GetABetRespTmp);
		} else {
          // Any BetFair error populating items handled here
 		  switch ( Response.Result->errorCode ) {
			case es2__GetBetErrorEnum__MARKET_USCORETYPE_USCORENOT_USCORESUPPORTED:
              printf("getEvents BetFair Error: Invalid parent id or parent has no children\n" );
		     break;
			case es2__GetBetErrorEnum__BET_USCOREID_USCOREINVALID:
              printf("getEvents BetFair Error: No data available to retrun\n" );
			break;
			case es2__GetBetErrorEnum__NO_USCORERESULTS:
              printf("getEvents BetFair Error: Invalid Local passed reverting to English\n" );
			  break;
			case es2__GetBetErrorEnum__API_USCOREERROR:
              printf("getEvents BetFair Error: General API Error\n" );
			  break;
			case es2__GetBetErrorEnum__INVALID_USCORELOCALE_USCOREDEFAULTING_USCORETO_USCOREENGLISH:
              printf("getEvents BetFair Error: General API Error\n" );
			  break;
			default:
			  printf("getABet Error: Unknown error occurred code : %d\n", Response.Result->errorCode );
		  }
		}
	  }
    } else {
      // Error at the SOAP level
      retval = BFAPI_SOAPERROR;
      printf("getABet SOAP Error\n");
    }
	soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
  }
  return ( retval );
}

//
// Currently this cancels 1 bet only at a time
//
BFAPI_Retvals BetFairAPIFree_5::cancelBets(vector<LONG64>&betid, vector<CancelbetsResp>&CancelBetsResp)
{
  bool mysoap = false;
  BFAPI_Retvals retval = BFAPI_TIMEOUT;
  mysoap = this->getSOAPFlag();
  if ( mysoap ) {
    es2__ArrayOfCancelBets theCancels;
    es2__CancelBets cancelaBet[40];
    vector<LONG64>::iterator cbit = betid.begin();
	if ( cbit == betid.end() ) {
      return (BFAPI_BADIPDATA);
	}
	// Load up the request with the ids of the bets to cancel
	theCancels.__sizeCancelBets = 0;
	for (cbit = betid.begin(); cbit != betid.end(); cbit++) {
	  theCancels.__sizeCancelBets++;
	  cancelaBet->betId = *cbit;
    }
    // Get a pointer to the array of cancel bets
	es2__CancelBets *thecancels = &cancelaBet[0];
	// Give the address of that pointer a **
	theCancels.CancelBets = &thecancels;
	es2__APIRequestHeader BFcancelBetsheader;                          // Header to put session key into   
	es2__CancelBetsReq BFcancelBetReq;                                 // cancelBet request class
    _es1__cancelBets BFcancelBetI;                                     // cancelBet request carrier class
    _es1__cancelBetsResponse Response;                                 // cancelBet response carrier class
    // Set up where to respond to
    Response.soap = &m_soap;
    // Set up keepalive parameters
    BFcancelBetsheader.sessionToken = (char *)m_sessiontoken.c_str();
    BFcancelBetReq.header = &BFcancelBetsheader;
    // Set up parameter carrier
    BFcancelBetI.request = &BFcancelBetReq;
    BFcancelBetI.soap = &m_soap;
    // Put cancels information into request
	BFcancelBetI.request->bets = &theCancels;
    // Make the Betfair API call, use defaults for SOAP Endpoint and API command 
	if (SOAP_OK == soap_call___es1__cancelBets(&m_soap, NULL, NULL, &BFcancelBetI, &Response)) {
      if (  gs2__APIErrorEnum__OK != Response.Result->header->errorCode ) {
        // Error at the BetFair level
        m_lasterrormsg = soap_es2__APIErrorEnum2s(&m_soap, Response.Result->header->errorCode);
        m_sessiontoken = "";
        m_lasttimestamp = 0;
        if ( gs2__APIErrorEnum__NO_USCORESESSION == Response.Result->header->errorCode ) {
          // BetFair API no session handled specially as relogin will probably cure it
          retval = BFAPI_NOSESSION;
          printf("cancelBets BetFair Error:%s\n", m_lasterrormsg.c_str());
        } else {
          // Any other BetFair API errors handled here
          retval = BFAPI_BFERROR;
          printf("cancelBets BetFair Error:%s\n", m_lasterrormsg.c_str());
        }
      } else {
        // Successful getBet save the session information
        m_sessiontoken = Response.Result->header->sessionToken;
        m_lasttimestamp = Response.Result->header->timestamp;
        if ( es2__CancelBetsErrorEnum__OK == Response.Result->errorCode ) {
		  retval = BFAPI_OK;
          // Get all the bet cancellation information from the retrieved values
          CancelbetsResp Cancelbetstmp;
		  for ( int j = 0; j < Response.Result->betResults->__sizeCancelBetsResult; j++ ) {
		    Cancelbetstmp.betId = Response.Result->betResults->CancelBetsResult[j]->betId;
		    Cancelbetstmp.resultCode = Response.Result->betResults->CancelBetsResult[j]->resultCode;
		    Cancelbetstmp.sizeCancelled = Response.Result->betResults->CancelBetsResult[j]->sizeCancelled;
		    Cancelbetstmp.sizeMatched = Response.Result->betResults->CancelBetsResult[j]->sizeMatched;
		    Cancelbetstmp.success = Response.Result->betResults->CancelBetsResult[j]->success;
		    CancelBetsResp.push_back(Cancelbetstmp);
		  }
		} else {
          // Any BetFair error populating items handled here
 		  switch ( Response.Result->errorCode ) {

			case es2__CancelBetsErrorEnum__INVALID_USCORENUMER_USCOREOF_USCORECANCELLATIONS:
              printf("cancelBets BetFair Error: Invalid Number of Cancellations\n" );
		     break;
			case es2__CancelBetsErrorEnum__MARKET_USCORETYPE_USCORENOT_USCORESUPPORTED:
              printf("cancelBets BetFair Error: Market Type Not Supported\n" );
			break;
			case es2__CancelBetsErrorEnum__MARKET_USCORESTATUS_USCOREINVALID:
              printf("cancelBets BetFair Error: Market Status Invalid\n" );
			  break;
			case es2__CancelBetsErrorEnum__MARKET_USCOREIDS_USCOREDONT_USCOREMATCH:
              printf("cancelBets BetFair Error: Market IDs Do Not Match\n" );
			  break;
			case es2__CancelBetsErrorEnum__INVALID_USCOREMARKET_USCOREID:
              printf("cancelBets BetFair Error: Invalid Market Id\n" );
			  break;
			case es2__CancelBetsErrorEnum__API_USCOREERROR:
              printf("cancelBets BetFair Error: General API Error\n" );
			  break;
			default:
			  printf("cancelBets Error: Unknown error occurred code : %d\n", Response.Result->errorCode );
		  }
		}
	  }
    } else {
      // Error at the SOAP level
      retval = BFAPI_SOAPERROR;
      printf("cancelBets SOAP Error\n");
    }
	soap_destroy(&m_soap);   //remove dynamically allocated C++ objects
	soap_end(&m_soap);       //remove temporary data and deserialized data
    // Let go of the m_SOAPInUse flag safely
    EnterCriticalSection( &m_cs );
    m_SOAPInUse = false;
    mysoap = false;
    LeaveCriticalSection( &m_cs );
  }
  return ( retval );
}

// End of file