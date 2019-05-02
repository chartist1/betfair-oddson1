#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
// #include <vld.h>            // Enable for memory leak checking and debugging
#include "all2.nsmap"          // Obtain the SOAP namespace mapping table
#include "sqlite3.h"           // SQLite3 API
#include "BetFairAPIFree_5.h"  // Betfair API access library
#include "Stakingplan.h"       // Staking plan functions
#include "Configuration.h"     // Configuration items functions
#include "Sendemail.h"         // Send Email functions

// TODO: Maybe have a resettable error counter on the loop reading odds, increment on fail
// reset to 0 on OK, if exceeds a value then abort and cause a restart.
//

// Local functions
int watchtherace(sqlite3 *thedb, int *whichone, vector<MarketData>::pointer MarketT, RaceData *TheRace, HANDLE waitobj, char *raceresults);
char *dateord(int dayn);
DWORD WINAPI mutexThread( LPVOID lpParam );
// Race distance/time calculation functions
bool extractdistance(std::string eventName, std::string& distance, std::string& namepart, int *furlongs);
bool calcavgtimes( sqlite3 *thedb);
// Bet placing and retrieving functions
LONG64 placealaybet(sqlite3 *thedb, BetFairAPIFree_5 *BFApi, double theprice, double thesize, int selectionid, int marketid);
int getaraceresult(sqlite3 *thedb, BetFairAPIFree_5 *BFApi, LONG64 betid, HANDLE waitobj);
// Find first and last race for day for emailing purposes
bool findfirstlastrace(sqlite3 *thedb, BetFairAPIFree_5 *BFApi, char *firstr, int firstrsz, char *lastr, int lastrsz, int *moreraces);
// Update the watchdog file, call this at least every minute
void touchwatchdogfile(char * tag, char *dogfile);
/* When using the gsoap tools to generate the soap classes use the -s no stl option with the wsdl2h tool */
#define _CRT_SECURE_NO_DEPRECATE 1

// Some constants used, move to config table later
// Values for time window

// Couple of hours to allow for late races, things like a Jockey falling can cause delays
#define PRESLOT  7200
// To ensure software is monitoring for start of race in plenty of time
#define POSTSLOT 3600

// Mutex name
#define bftime   "betFairBot1Timing"

// Market name
#define MNAME "To Be Placed"

// Buffer sizes
#define CHAR50 50
#define CHAR100 100

// Global flag to keep other threads running
static int nowrunning = 1;

// Not nice but easiest way to handle this
LARGE_INTEGER sfrequency;  // ticks per second

// Full path of the watchdog file
char *watchdogfile = "c:\\tools\\betfairbot1.wd";


int main()
{
  cout << "Betfairbot1 Starting...." << endl;
  // Create the watchdog file
  touchwatchdogfile("0010", watchdogfile);
  extern LARGE_INTEGER sfrequency;
  LARGE_INTEGER gpt1, gpt2, gmt1, gmt2;   // ticks
  // get ticks per second
  QueryPerformanceFrequency(&sfrequency);
  vector<EventType> TypeSought;
  vector<VenueCtry> countryeventid;
  vector<VenueData> eventid;
  // Create thread to hold timing Mutex
  int Data_Of_mutexThread = 1;
  int wontarget = 0;
  // Next 3 used for retrieving first and last races of day
  int moreracestoday = 0;
  char frace[1024] = {0};
  char lrace[1024] = {0};
  HANDLE Thandle = CreateThread( NULL, 0, mutexThread, &Data_Of_mutexThread, 0, NULL);  
  if ( Thandle == NULL) {
    cout << "Unable to create thread for timing Mutex " << endl; 
	return (1);
  }
  BetFairAPIFree_5 *BFApi = NULL;
  BFApi = BetFairAPIFree_5::getInstance();
  // Append to log file of events of note.
  char startupb[CHAR50] = {0};
  time_t startup = BFApi->timeGMTtoUK(time(NULL));
  struct tm startuptiming = {};
  gmtime_s( &startuptiming, (const time_t *)&startup);
  strftime(startupb,CHAR50,"%Y/%m/%d %H:%M:%S",&startuptiming);
  std::ofstream outfile;
  outfile.open("c:\\tools\\betfairbot1.txt", std::ios_base::app);
  outfile << startupb << " UK:Betfairbot1 Started" << endl; 
  outfile.close();
  BFApi->setUserName("valuecall");
  BFApi->setPassword("dil0_U6t1fs");
  char *dbfile = "C:\\Tools\\betfairbot1.sl3";
  char *raceresultsfile = "c:\\tools\\races.csv";
  LPCWSTR mname = (LPCWSTR)bftime;
  int eventtypeid = 0;
  sqlite3 *db = NULL;
  int currentday = 0;
  int racestoday = 0;
  int sqlret = 0;
  int azero = 0;
  // Set value of current day
  // get value of today is based on current date
  time_t tnowis = time(NULL);
  struct tm tnowistiming = {};
  gmtime_s( &tnowistiming, (const time_t *)&tnowis);
  currentday = tnowistiming.tm_mday;
  // Use wait on mutex with timeout for more accurate waiting between calls
  HANDLE ghMutex; 
  ghMutex = CreateMutex( 
    NULL,              // default security attributes
    FALSE,             // initially not owned
    mname);            // unnamed mutex
  if (ghMutex == NULL) {
    cout << "CreateMutex error: " <<  GetLastError() << endl;
	nowrunning = 0;
	Sleep(500);
    return(1);
  }
  // Open or create the SQLite database
  const char *rtdrop1 = "DROP TABLE IF EXISTS Races;";
  const char *rtdrop2 = "DROP TABLE IF EXISTS SMarket;";
  const char *rtdrop3 = "DROP TABLE IF EXISTS Runners;";
  const char *rtdrop4 = "DROP TABLE IF EXISTS AvgRacetimes;";
  sqlret = sqlite3_open(dbfile, &db);
  // Touch the watchdog file
  touchwatchdogfile("0020", watchdogfile);
  if (sqlret == SQLITE_OK)  {
    std::cout << "Opened Database Successfully " << std::endl;
    // Drop the races table
    sqlret = sqlite3_exec(db,rtdrop1,0,0,0);
	if ( sqlret != SQLITE_OK ) {
	  cout << "Failed to drop database table Races error code : " << sqlret << endl;
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
  	  return(1);
	}
    // Create races table, this ensures the table only contains recent data
    const char *rtcreate1 = "CREATE TABLE IF NOT EXISTS Races(primkey TEXT PRIMARY KEY, exchangeId INTEGER, id INTEGER, country TEXT, venue TEXT, name TEXT, starttime INTEGER, completed INTEGER, racestate INTEGER);";
	sqlret = sqlite3_exec(db,rtcreate1,0,0,0);
	if ( sqlret != SQLITE_OK ) {
	  cout << "Failed to create database table Races error code : " << sqlret << endl;
	  sqlite3_close(db);
  	  nowrunning = 0;
	  Sleep(500);
  	  return(1);
	}
	// Add starttime index to races table
    const char *rtindex1 = "CREATE INDEX IF NOT EXISTS idx_Races_starttime ON Races(starttime);";
    sqlret = sqlite3_exec(db,rtindex1,0,0,0);
	if ( sqlret != SQLITE_OK ) {
	  cout << "Failed to create index starttime on table Races error code : " << sqlret << endl;
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
	// Touch the watchdog file
    touchwatchdogfile("0030", watchdogfile);
    // Drop the static markets table
    sqlret = sqlite3_exec(db,rtdrop2,0,0,0);
	if ( sqlret != SQLITE_OK ) {
	  cout << "Failed to drop database table SMarket error code : " << sqlret << endl;
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
    // Create static market info table, this ensures the table only contains recent data
	const char *rtcreate3 = "CREATE TABLE IF NOT EXISTS SMarket(primkey TEXT PRIMARY KEY, countryISO3 TEXT, discountAllowed INTEGER, eventTypeId INTEGER, lastRefresh INTEGER, licenceId INTEGER, marketBaseRate REAL, marketDescription TEXT, marketDescriptionHasDate INTEGER, marketDisplayTime INTEGER, marketId INTEGER, marketStatus INTEGER, marketSuspendTime INTEGER, marketTime INTEGER, marketType INTEGER, marketTypeVariant INTEGER, menuPath TEXT, name TEXT, numberOfWinners INTEGER, parentEventId INTEGER, unit INTEGER, minUnitValue INTEGER, maxUnitValue INTEGER, interval INTEGER, runnersMayBeAdded INTEGER, timezone TEXT);";
    sqlret = sqlite3_exec(db,rtcreate3,0,0,0);
	if ( sqlret != SQLITE_OK ) {
	  cout << "Failed to create database table SMarket error code : " << sqlret << endl;
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
    // Drop the runners table
    sqlret = sqlite3_exec(db,rtdrop3,0,0,0);
	if ( sqlret != SQLITE_OK ) {
	  cout << "Failed to drop database table Runners error code : " << sqlret << endl;
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
	// Create runners table this ensure the table only contains recent data
	const char *rtcreate4 = "CREATE TABLE IF NOT EXISTS Runners(primkey TEXT PRIMARY KEY, exchangeId INTEGER, marketId INTEGER, asianLineId INTEGER, handicap REAL, name TEXT, selectionId INTEGER);";
    sqlret = sqlite3_exec(db,rtcreate4,0,0,0);
	if ( sqlret != SQLITE_OK ) {
	  cout << "Failed to create database table Runners error code : " << sqlret << endl;
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
	// Touch the watchdog file
    touchwatchdogfile("0040", watchdogfile);
    // Drop the average race times table, this is regenerated at the start each time
	// so it uses all the latest race times available including the latest
    sqlret = sqlite3_exec(db,rtdrop4,0,0,0);
	if ( sqlret != SQLITE_OK ) {
	  cout << "Failed to drop database table AvgRacetimes error code : " << sqlret << endl;
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
	// Create AvgRacetimes table if necessary
	const char *rtcreate5 = "CREATE TABLE IF NOT EXISTS AvgRaceTimes(distance TEXT PRIMARY KEY, avgtime INTEGER, nobetafter INTEGER, nobetbefore INTEGER);";
    sqlret = sqlite3_exec(db,rtcreate5,0,0,0);
	if ( sqlret != SQLITE_OK ) {
	  cout << "Failed to create database table AvgRacetimes error code : " << sqlret << endl;
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
	// These next tables are never dropped they are created once
    // Create the configuration table if necessary
	if ( 0 == CFsetupconfiguration( db ) ) {
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
	// Create the staking plan table 
	if ( 0 == SPsetupstakingplan( db ) ) {
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
    // Create all venues found table if necessary
	// This table is not dropped, the information gathered is used for future runs to validate venues
    const char *rtcreate2 = "CREATE TABLE IF NOT EXISTS AllVenues(venue TEXT PRIMARY KEY, confirmed INTEGER);";
    sqlret = sqlite3_exec(db,rtcreate2,0,0,0);
	if ( sqlret != SQLITE_OK ) {
	  cout << "Failed to create database table AllVenues error code : " << sqlret << endl;
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
	// Touch the watchdog file
    touchwatchdogfile("0050", watchdogfile);
    // Create the race times time table if necessary
	// This table is not dropped, the information here is used to calculate the average race times and grows over time, may add
	// a feature to drop data from the table after a certain time period (probably months).
	// The average race times for each distance are calculated from the information stored here at the start of a run and stored
	// in a table that is re-created each run, This keeps the average times upto date with the latest race times
	const char *rtcreate6 = "CREATE TABLE IF NOT EXISTS RaceTimes(primarykey INTEGER PRIMARY KEY, country TEXT, venue TEXT, name TEXT, distance TEXT, racetime INTEGER, racestarted INTEGER, racefinished INTEGER, duration INTEGER, confirmed INTEGER );";
    sqlret = sqlite3_exec(db,rtcreate6,0,0,0);
	if ( sqlret != SQLITE_OK ) {
	  cout << "Failed to create database table RaceTimes error code : " << sqlret << endl;
	  sqlite3_close(db);
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
  }
  else {
    cout << "Failed to open database file : " << dbfile << endl;
	nowrunning = 0;
	Sleep(500);
	return(1);
  }
  // Calculate average race times based on data in RaceTimes table
  calcavgtimes( db );
  // Touch the watchdog file
  touchwatchdogfile("0060", watchdogfile);
  // Login to Betfair
  if ( BFAPI_OK == BFApi->login()) {
    cout << "Login To Betfair Successful " << endl;
  }
  else {
    cout << "Login To Betfair Failure " << endl;
    if ( NULL != db ) {
      sqlite3_close(db);
    }
	nowrunning = 0;
	Sleep(500);
	return(1);
  }
  // Get the bank balance from Betfair
  vector <AccountFunds> TheFunds;
  int openbbali = 0;
  double openbbald1 = 0.0;
  double openbbald2 = 0.0;
  if ( BFAPI_OK == BFApi->getAccountFunds( TheFunds ) ) { 
    vector<AccountFunds>::iterator fuit = TheFunds.begin();
    openbbali = fuit->balance;
	// Convert pennies to pounds and pence
    openbbald1 = ((double)openbbali) / 100;
    openbbald1 = floor(openbbald1 * 10000 + 0.5) / 10000;
  }
  else {
    cout << "Getting Account Funds From Betfair Failure " << endl;
    if ( NULL != db ) {
      sqlite3_close(db);
    }
	nowrunning = 0;
	Sleep(500);
	return(1);
  }
  // Reset win and lose counts if new day
  CFchecktodayis(db);
  // Get active event types to find Horse Racing
  TypeSought.clear();
  // Touch the watchdog file
  touchwatchdogfile("0070", watchdogfile);
  if ( BFAPI_OK == BFApi->getActiveEventTypes(TypeSought)) {
    for (vector<EventType>::iterator tyit = TypeSought.begin(); tyit != TypeSought.end(); tyit++) {
      if ( tyit->name == "Horse Racing" ) {
	    // Found id for Horse Racing events
        eventtypeid = tyit->id;
        cout << "Found " << tyit->name << endl;
      }
    }
  }
  else {
    cout << "Failed to get ActiveEvents from Betfair " << endl;
    if ( NULL != db ) {
      sqlite3_close(db);
    }
	nowrunning = 0;
	Sleep(500);
	return(1);
  }
  TypeSought.clear();
  if ( eventtypeid > 0 ) {
	countryeventid.clear();
    // Now looking for horse racing event id's in required countries
    VenueCtry ctrydat;
    vector<Events> CtrySought;
	if ( BFAPI_OK != BFApi->getEvents(eventtypeid, CtrySought )) {
      cout << "Failed to get Country ID info from Betfair " << endl;
      if ( NULL != db ) {
        sqlite3_close(db);
      }
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	} else {
	  if ( CtrySought.begin() != CtrySought.end() ) {
	    for (vector<BFEvents>::iterator ctit = CtrySought.begin()->eventItems.begin(); ctit != CtrySought.begin()->eventItems.end(); ctit++) {
		  // TODO Make this configurable
          if ( ctit->eventName == "GB" || ctit->eventName == "IRE" ) {
            ctrydat.country = ctit->eventName;
	        ctrydat.id = ctit->eventId;
            ctrydat.eventTypeId = ctit->eventTypeId;
            countryeventid.push_back(ctrydat);
            cout << "Found " << ctit->eventName << endl;
          }
	    }
	  } else {
	    cout << "Zero Country ID info retrieved from Betfair " << endl;
	    if ( NULL != db ) {
          sqlite3_close(db);
        }
	    nowrunning = 0;
	    Sleep(500);
		return(1);
	  }
	}
  }
  // Touch the watchdog file
  touchwatchdogfile("0080", watchdogfile);
  eventid.clear();
  const char * avenuep = NULL;
  sqlite3_stmt *sstmt = NULL;
  const char *selectavenue = "SELECT venue, confirmed FROM AllVenues WHERE (venue = ?1) LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(db, selectavenue, -1, &sstmt, NULL)) ) {
    cout << "Could not prepare sstmt Error code : " << sqlret << endl;
  }
  sqlite3_stmt *istmt = NULL;
  const char *insertavenue = "INSERT OR IGNORE INTO AllVenues (venue, confirmed) VALUES (?, ?);";
  if ( SQLITE_OK != (sqlret = sqlite3_prepare_v2(db, insertavenue, -1, &istmt, NULL))) {
    cout << "Could not prepare istmt Error code : " << sqlret << endl;
  }
  int venrows;
  int allvens = 1;
  vector<std::string> unknownv;
  unknownv.clear();
  for (vector<VenueCtry>::iterator ctryit = countryeventid.begin(); ctryit != countryeventid.end(); ctryit++) {
    vector<std::string> vvenu;
	// Now looking for all venues in the required countries with events
    VenueData venuedat;
    vector<Events> ActiveVenues;
	if ( BFAPI_OK != BFApi->getEvents(ctryit->id, ActiveVenues)) {
      cout << "Failed to get Active Venues info from Betfair " << endl;
      if ( NULL != db ) {
        sqlite3_close(db);
      }
	  nowrunning = 0;
	  Sleep(500);
	  return(1);
	}
	if ( ActiveVenues.begin() != ActiveVenues.end() ) {
      for (vector<BFEvents>::iterator venit = ActiveVenues.begin()->eventItems.begin(); venit != ActiveVenues.begin()->eventItems.end(); venit++) {
	    // Tokenize eventName string
	    vvenu.clear();
	    std::stringstream vve(venit->eventName.c_str());
        std::string vveitem;
	    while (std::getline(vve, vveitem, ' ')) {
          vvenu.push_back(vveitem);
        }
	    avenuep = venit->eventName.c_str();
        // Is there expected number of tokens for a real event
	    if ( vvenu.size() == 3 ) {
          // Is last item a month
		  if ( vvenu.back() == "Jan" || vvenu.back() == "Feb" || vvenu.back() == "Mar" || vvenu.back() == "Apr"
		     || vvenu.back() == "May" || vvenu.back() == "Jun" || vvenu.back() == "Jul" || vvenu.back() == "Aug"
			 || vvenu.back() == "Sep" || vvenu.back() == "Oct" || vvenu.back() == "Nov" || vvenu.back() == "Dec") {
			 avenuep = vvenu.front().c_str();
		  }
	    }
	    // Store the venue name information for checking and indicating which is a valid venue
	    sqlret = sqlite3_bind_text(istmt, 1, avenuep,  strlen(avenuep),  SQLITE_STATIC);
	    if (sqlret != SQLITE_OK) {
	      cout << "Could not bind( " << avenuep << " ) to istmt Error Code: " <<  sqlret << endl;
	      nowrunning = 0;
	      Sleep(500);
		  return(1);
        }
	    sqlret = sqlite3_bind_int(istmt,  2, azero);
	    if (sqlret != SQLITE_OK) {
	      cout << "Could not bind( " << azero << " ) to istmt Error Code: " <<  sqlret << endl;
	      nowrunning = 0;
	      Sleep(500);
		  return(1);
        }
	    sqlret = sqlite3_step(istmt);
	    if (sqlret != SQLITE_DONE) {
	      cout << "Could not step (execute) istmt Error Code: " <<  sqlret << " Primary Key " << avenuep << endl;
	      nowrunning = 0;
	      Sleep(500);
		  return(1);
        }
        sqlret = sqlite3_reset(istmt);
        if (sqlret != SQLITE_OK) {
	      cout << "Could not reset ( istmt ) Error Code: " <<  sqlret << " Primary Key " << avenuep << endl;
	      nowrunning = 0;
	      Sleep(500);
		  return(1);
        }
        // IDEA: Keep information for venues we know are valid, trying to filter out the unwanted by listing the unwanted
	    // was not safe as the unwanted items seems to change from time to time, whereas the valid venues should not
	    // and at worst we won't look at something we should, instead of looking at something we should not which would
	    // not be good when the bot is placing the bets automatically. Better not to place a wrong bet than miss placing
	    // a right bet.
	    venrows = 0;
        sqlret = sqlite3_bind_text(sstmt, 1, avenuep, -1, SQLITE_STATIC);
	    if (sqlret != SQLITE_OK) {
          cout << "Could not bind ( " <<  avenuep << " ) to sstmt Error Code: " << sqlret << endl;
	      nowrunning = 0;
	      Sleep(500);
		  return(1);
        }
	    sqlret = sqlite3_step(sstmt);
	    if ( sqlret == SQLITE_ROW) {
          int tcomp = sqlite3_column_int(sstmt, 1);
		  if ( 1 == tcomp ) {
 		    venrows = 1;
		  }
		  if ( 0 == tcomp  ) {
			cout << "Warning : Venue Status Has Not Been Configured In Database, Update Its Status In Database: " << ctryit->country << " " << venit->eventName << endl;
			// Save venue country and name to send by email later
			unknownv.push_back(ctryit->country + ":" + venit->eventName);
			allvens = 0;
		  }
        }
        else {
	      if ( sqlret != SQLITE_DONE) {
            cout << "Could not step (execute) sstmt Error Code: " << sqlret << " Primary Key " << avenuep << endl;
	        nowrunning = 0;
			Sleep(500);
		    return(1);

		  } else {
		    cout << "WARN Venue Not Found In AllVenues Table" << ctryit->country << " " << venit->eventName << endl;
		  }
	    }
	    sqlret = sqlite3_reset(sstmt);
	    if (sqlret != SQLITE_OK) {
          cout << "Could not reset( sstmt ) 1 Error Code: " << sqlret << endl;
	      nowrunning = 0;
	      Sleep(500);
		  return(1);
        }
	    if (1 == venrows) {
	      venuedat.id = venit->eventId;
	      venuedat.country.assign(ctryit->country);
	      venuedat.name = venit->eventName;
          eventid.push_back(venuedat);
	    }
      } // End for (vector<BFEvents>
	} else {
      cout << "Betfair Returned Zero Meets For Country" << endl;
      if ( NULL != db ) {
        sqlite3_close(db);
      }
      nowrunning = 0;
	  Sleep(500);
      return(1);
	}
  }
   // Send Email to monitoring email account listing unknown venues found this run.
  if ( 0 == allvens ) {
    time_t emailr = BFApi->timeGMTtoUK(time(NULL));
	  std::string msgbody = "Betfairbot1 unknown venues found.";
	  cout << "====================" << endl;
	  cout << msgbody << endl;
	  msgbody += "\r";
	  for (vector<std::string>::iterator unv = unknownv.begin(); unv != unknownv.end(); unv++) {
	    msgbody += *unv;
	    msgbody += "\r";
      cout << *unv << endl;
	  }
	  cout << "====================" << endl;
    // Build the string for the SMS
    std::string smsbody = "BFB1:Unknown Venues:";
    for (vector<std::string>::iterator unv = unknownv.begin(); unv != unknownv.end(); unv++) {
	    smsbody += *unv;
      smsbody += ":";
	  }
    if (!SendEmail(smtpdom, authpass, fromnm, fromdom, tonam, todom, "Betfairbot1 Unknown Venues Found", msgbody.c_str(), emailr, smsbody.c_str())) {
      cout << "Failed to send unknown venue(s) email" << endl;
    }
  }
  // Touch the watchdog file
  touchwatchdogfile("0090", watchdogfile);
  unknownv.clear();
  sqlret = sqlite3_finalize(istmt);
  if (sqlret != SQLITE_OK) {
    cout << "Could not finalize( istmt ) Error Code: " << sqlret << endl;
    nowrunning = 0;
    Sleep(500);
    return(1);
  }
  sqlret = sqlite3_finalize(sstmt);
  if (sqlret != SQLITE_OK) {
    cout << "Could not finalize( sstmt ) Error Code: " << sqlret << endl;
    nowrunning = 0;
    Sleep(500);
    return(1);
  }
  sqlite3_stmt *stmt = NULL;
  const char * ctryp = NULL;
  const char * namep = NULL;
  const char * venuep = NULL;
  char keybuff[CHAR100] = {0};
  char timebuff[CHAR50] = {0};
  char *insertrace = "INSERT OR REPLACE INTO Races (primkey, exchangeId, id, country, venue, name, starttime, completed, racestate) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlret = sqlite3_prepare_v2(db, insertrace, -1, &stmt, NULL);
  for (vector<VenueData>::iterator evntit = eventid.begin(); evntit != eventid.end(); evntit++) {
    // Touch the watchdog file
    touchwatchdogfile("0100", watchdogfile);
	  RaceData thisRace;
    // Now looking for races at the venues found
    vector<Events> ActiveEvents;
	  if ( BFAPI_OK != BFApi->getEvents(evntit->id, ActiveEvents )) {
      if ( NULL != db ) {
        sqlite3_close(db);
      }
	    cout << "Failed to get Races at Meet info from Betfair " << endl;
      nowrunning = 0;
	    Sleep(500);
	    return(1);
	  }
    if ( ActiveEvents.begin() != ActiveEvents.end() ) {
      for (vector<MarketSummary>::iterator itev = ActiveEvents.begin()->marketItems.begin(); itev != ActiveEvents.begin()->marketItems.end(); itev++) {
	      // If not To Be Placed
        if ( itev->marketName != MNAME ) {
          int hour;
	        int minute;
		      struct tm racestart = {};	      
	        thisRace.id = itev->marketId;
		      thisRace.country = evntit->country;
		      ctryp = thisRace.country.c_str();
		      thisRace.name = itev->marketName;
		      namep = thisRace.name.c_str();
		      thisRace.venue = itev->venue;
  		    venuep = thisRace.venue.c_str();
		      thisRace.starttime = itev->startTime;
		      thisRace.exchangeId = itev->exchangeId;
		      time_t ttime = BFApi->timeGMTtoUK(thisRace.starttime);
		      gmtime_s( &racestart, (const time_t *)&ttime);  // This is giving correct time
          hour = racestart.tm_hour;
          minute = racestart.tm_min;
		      if ( currentday == racestart.tm_mday ) {
            racestoday++;
		      }
		  sprintf_s(keybuff, CHAR100, "%d-%d", thisRace.exchangeId, thisRace.id);
		  sqlite3_bind_text(stmt,  1, keybuff,  strlen(keybuff),  SQLITE_STATIC);
		  sqlite3_bind_int(stmt,   2, thisRace.exchangeId);
		  sqlite3_bind_int(stmt,   3, thisRace.id);
		  sqlite3_bind_text(stmt,  4, ctryp,  strlen(ctryp),  SQLITE_STATIC);
		  sqlite3_bind_text(stmt,  5, venuep,  strlen(venuep),  SQLITE_STATIC);
		  sqlite3_bind_text(stmt,  6, namep, strlen(namep), SQLITE_STATIC);
		  sqlite3_bind_int64(stmt, 7, thisRace.starttime);
		  sqlite3_bind_int(stmt,   8, azero);
  		  sqlite3_bind_int(stmt,   9, azero);  // 0 racestatus means unknown state
		  sqlret = sqlite3_step(stmt);
		  if (sqlret != SQLITE_DONE) {
		    cout << "Could not step (execute) stmt Error Code: " <<  sqlret << "Primary Key " << keybuff << endl;
	        nowrunning = 0;
	        Sleep(500);
			return 1;
          }
          sqlret = sqlite3_reset(stmt);
  		  if (sqlret != SQLITE_OK) {
		    cout << "Could not reset( stmt ) Error Code: " <<  sqlret << endl;
	        nowrunning = 0;
	        Sleep(500);
			return 1;
          }
		  sprintf_s(timebuff, sizeof(timebuff), "%02d:%02d", hour, minute);
		  cout << evntit->country << " " << evntit->name << " " << timebuff << " " << itev->venue << endl;
	    }
	  }
	} else {
	  cout << "Betfair returned 0 races at the meet" << endl;
      if ( NULL != db ) {
        sqlite3_close(db);
      }
      nowrunning = 0;
      Sleep(500);
	  return(1);
	}
  }
  sqlite3_finalize(stmt);
  // Now we have a list of horse racing 'to win' markets to look at
  CFsetracestoday(db, racestoday);
  // Set day last updated
  CFsettodayis(db);
  // Send an Email informing software run started to monitoring email account
  // Wait until this stage so bot has gathered all the information required for the startup email
  int tsteps = 0;
  int tcumreward = 0;
  int tctarget = 0;
  double tctarget2 = 0.0;
  char emailmsg[4096] = {0};
  char tmpemailmsg[1024] = {0};
  time_t emailt = BFApi->timeGMTtoUK(time(NULL));
  struct tm pemailt = {};
  char emailtbuff[50] = {0};
  gmtime_s( &pemailt, (const time_t *)&emailt);
  double laststakes = 0.0;
  strftime(emailtbuff,sizeof(emailtbuff),"%a %d %b %Y at %H:%M:%S",&pemailt);
  sprintf_s( emailmsg, sizeof(emailmsg), "Run Started\rUK Date and Time : %s\r", emailtbuff);
  if ( findfirstlastrace(db, BFApi, &frace[0], sizeof(frace), &lrace[0], sizeof(lrace), &moreracestoday)) {
    sprintf_s( tmpemailmsg, sizeof(tmpemailmsg), "First race : %s\rLast race : %s\rRaces today : %02d\r", frace, lrace, racestoday);
    strcat_s(emailmsg, sizeof(emailmsg), tmpemailmsg);
  }
  sprintf_s( tmpemailmsg, sizeof(tmpemailmsg), "Opening balance : GBP%4.2f\r", openbbald1);
  strcat_s(emailmsg, sizeof(emailmsg), tmpemailmsg);
  SPinstakingset( db, BFApi, ghMutex, &tsteps, &tcumreward, &tctarget);
  // Convert pennies to pounds and pence
  tctarget2 = ((double)tctarget) / 100;
  tctarget2 = floor(tctarget2 * 10000 + 0.5) / 10000;
  if ( tsteps > 0 ) {
    laststakes = SPreadlaststake( db );
    sprintf_s( tmpemailmsg, sizeof(tmpemailmsg), "Staking plan Step : %02d\r Last Stake : GBP%4.2f\r Target : GBP%4.2f\r", tsteps, laststakes, tctarget2);
  } else {
	  sprintf_s( tmpemailmsg, sizeof(tmpemailmsg), "Staking plan Step : 00\rNext Target : GBP%4.2f\r", tctarget2);
  }
  strcat_s(emailmsg, sizeof(emailmsg), tmpemailmsg);
  std::string msgmail;
  msgmail = emailmsg;
  char smsbody1[160] = {0};
  if ( tsteps > 0 ) {
    sprintf_s(smsbody1, sizeof(smsbody1), "BFB1:Started:FR:%s:LR:%s:NR:%d:OB:GBP%4.2f:SS:%d:LS:GBP%4.2f:TR:GBP%4.2f", frace, lrace, racestoday, openbbald1,tsteps,laststakes,tctarget2);
  } else {
    sprintf_s(smsbody1, sizeof(smsbody1), "BFB1:Started:FR:%s:LR:%s:NR:%d:OB:GBP%4.2f:SP:%d:TR:GBP%4.2f", frace, lrace, racestoday, openbbald1,tsteps,tctarget2);
  }
  if (!SendEmail(smtpdom, authpass, fromnm, fromdom, tonam, todom, "Betfairbot1 Run Started", msgmail, emailt, smsbody1)) {
    cout << "Failed to send start email" << endl;
  }
  int mktcnt = 0;
  // Touch the watchdog file
  touchwatchdogfile("0110", watchdogfile);
  // Check to see if any unsettled bets in staking plan table, possible if bot crashed and restarted
  if ( SPanystepsinset( db) ) {                            // If any rows in staking plan table
    int lsettle = SPreadlastsettled( db );
    
    if ( lsettle < 1 ) {                                   // If last bet was not settled
      cout << "Found unsettled bet in staking plan at startup" << endl;
      LONG64 lbid = SPreadlastbetid( db );                 // Get ID of last bet
      cout << "Retrieving result for betid : " << lbid << " : " << endl;
      int lres = getaraceresult(db, BFApi, lbid, ghMutex); // Get result of that race from exchange
      // Append to csv file
      std::ofstream outfile;
      outfile.open(raceresultsfile, std::ios_base::app);
      if ( lres > 0 ) { // Win
        outfile << "," << lbid << ",,,,,,,,,,,,,,,,,,Win From Betfair After Bot Restart,,,,,,," << endl;
      } else { // Lose
        outfile << "," << lbid << ",,,,,,,,,,,,,,,,,,Lose From Betfair After Bot Restart,,,,,,," << endl;
      }
      outfile.close();
     cout << "Result for betid : " << lbid << " : retrieved, continuing startup" << endl;
    }
  }
  // Touch the watchdog file
  touchwatchdogfile("0111", watchdogfile);
  // Retrieve Race Info sorted by starttime and only where timestamp is recent
  sqlite3_stmt *srstmt = NULL;
  const char *selracesbytime = "SELECT id, starttime, country, venue, name, exchangeId, completed, racestate FROM Races WHERE (starttime > ?1) AND (starttime < ?2) AND (completed = 0) ORDER BY starttime ASC";
  // Insert static market data into database
  sqlite3_stmt *imarkets = NULL;
  const char *insertmarkets = "INSERT OR REPLACE INTO SMarket (primkey,countryISO3,discountAllowed,eventTypeId,lastRefresh,licenceId,marketBaseRate,marketDescription,marketDescriptionHasDate,marketDisplayTime,marketId,marketStatus,marketSuspendTime,marketTime,marketType,marketTypeVariant,menuPath,name,numberOfWinners,parentEventId,unit,minUnitValue,maxUnitValue,interval,runnersMayBeAdded,timezone) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  // Retrieve static market data from database
  sqlite3_stmt *smarkets = NULL;
  const char *selmarkets = "SELECT countryISO3,discountAllowed,eventTypeId,lastRefresh,licenceId,marketBaseRate,marketDescription,marketDescriptionHasDate,marketDisplayTime,marketId,marketStatus,marketSuspendTime,marketTime,marketType,marketTypeVariant,menuPath,name,numberOfWinners,parentEventId,unit,minUnitValue,maxUnitValue,interval,runnersMayBeAdded,timezone FROM SMarket WHERE (primkey=?)";
  // Insert static runners market data into database
  sqlite3_stmt *insrunners = NULL;
  const char *irunners = "INSERT OR REPLACE INTO Runners (primkey,exchangeId,marketId,asianLineId,handicap,name,selectionId) VALUES (?, ?, ?, ?, ?, ?, ?);";
  // Retrieve static runners market data from database
  sqlite3_stmt *selrunners = NULL;
  const char *srunners = "SELECT exchangeId,marketId,asianLineId,handicap,name,selectionId FROM Runners WHERE (exchangeId == ?1) AND (marketId == ?2)";
  // Loop reading markets and prices, tries to read static market data from from database first then if not
  // available from Betfair. This means that after first access of each static market data record it is available
  // in the database and can be accessed immediately. This works around the Betfair Free API limit of 5 per minute,
  // for static market data and allows quick startup by loading them only as required. There is throttling on
  // this reading of static market data from Betfair to stop the API throttling and disconnecting.
  // Use getpricescompressed and getprices to allow up to 70 price reads a minute.
  // Also use a sliding time window to reduce the number of events being checked, to events an hour or so
  // either side of the current time, this allows finding race starts quicker.
  vector<Prices>::iterator btlit;
  vector<MarketData>::iterator getmarkets;
  vector<RPrices>::iterator tprit;
  vector<MarketData>::iterator marketsid;
  vector<MarketPrices>::iterator chkinplay;
  vector<MarketRunners>::iterator insrunnerid;
  vector<MarketRunners>::iterator runnerid;
  std::map<int, struct RPrices, intclasscomp>::iterator SelPrice;
  char tbuff[40] = {0};
  char dbuff1[40] = {0};
  char dbuff2[40] = {0};
  int racecompleted = 0;
  int racerun = 0;
  time_t endtime = time(NULL);
  time_t starttime = time(NULL) - PRESLOT;
  time_t lateststart = time(NULL) + POSTSLOT;
  // Loop for maximum 12 hours
  endtime += 60*60*12;
  int whichone = 0;

  QueryPerformanceCounter(&gpt1);
  QueryPerformanceCounter(&gmt1);
  // Set api timer counters back to ensure reads immediately first time around
  // Could check values are valid, but probably not necessary as counters start when system starts
  gpt1.QuadPart -= sfrequency.QuadPart;
  gmt1.QuadPart -= (12 * sfrequency.QuadPart);
  //
  // Main loop runs for 12 hours maxium or until reached number of wins required
  // loop is also exited by resetting endtime if no more races in day
  // TODO Currenly number of wins (wonarget) is not being used, will never cause an exit
  while ( (time(NULL) < endtime) && (0 == wontarget)) {
    // Touch the watchdog file
    touchwatchdogfile("00120", watchdogfile);
	sqlret = sqlite3_prepare_v2(db, selracesbytime, -1, &srstmt, NULL);
	if ( sqlret != SQLITE_OK ) {
      cout << "Failed to prepare " << selracesbytime << " error code : " << sqlret << endl;
	  sqlite3_close(db);
      nowrunning = 0;
      Sleep(500);
	  return(1);
	}
    sqlret = sqlite3_bind_int64(srstmt, 1, starttime);
	if ( sqlret != SQLITE_OK ) {
      cout << "Failed to bind " << starttime << " to " << selracesbytime << " error code : " << sqlret << endl;
	  sqlite3_close(db);
      nowrunning = 0;
      Sleep(500);
	  return(1);
	}
    sqlret = sqlite3_bind_int64(srstmt, 2, lateststart);
	if ( sqlret != SQLITE_OK ) {
      cout << "Failed to bind " << lateststart << " to " << selracesbytime << " error code : " << sqlret << endl;
	  sqlite3_close(db);
      nowrunning = 0;
      Sleep(500);
	  return(1);
	}
	sqlret = sqlite3_step(srstmt);
    if ( sqlret != SQLITE_ROW && sqlret != SQLITE_DONE ) {
      cout << "Failed to retrieve Races info from database error code : " << sqlret << endl;
	  sqlite3_close(db);
      nowrunning = 0;
      Sleep(500);
	  return(1);
    }
    RaceData TheRace = {};
	vector<RaceData> RacesByTime;
	RacesByTime.clear();
	if ( SQLITE_DONE == sqlret ) {
	  // No races found within sliding time frame so sleep a while (30 mins)
      // Also ensures a sleep does not take it beyond endtime
	  cout << "No races in time frame sleeping for a while" << endl;
	  DWORD sleeptime = 30 * 60 * 1000;
	  if ( (time(NULL) + (30*60)) > endtime ) {
	    sleeptime = (DWORD)(endtime - time(NULL)) * 1000;
		if ( sleeptime < 0 ) {
          // Should never happen
          sleeptime = 1;
		}
	  }
	  if ( sleeptime > 15000 ) {
        DWORD sleepsteps = sleeptime / 15000;
		for ( DWORD sx = 0; sx < sleepsteps; sx++ ) {
          Sleep(15000);
	      // Touch the watchdog file
          touchwatchdogfile("0130", watchdogfile);
		}
		Sleep(sleeptime % 15000);
	  } else {
        Sleep(sleeptime);
	  }
      // Touch the watchdog file
      touchwatchdogfile("0140", watchdogfile);
	}
	else {
      // Create vector of race info so can close open sqlite statement
      while ( sqlret == SQLITE_ROW ) {
	    TheRace.id = sqlite3_column_int(srstmt, 0);
	    TheRace.starttime = sqlite3_column_int(srstmt, 1);
	    TheRace.country.assign((const char *)sqlite3_column_text(srstmt, 2));
	    TheRace.venue.assign((const char *)sqlite3_column_text(srstmt, 3));
	    TheRace.name.assign((const char *)sqlite3_column_text(srstmt, 4));
	    TheRace.exchangeId = sqlite3_column_int(srstmt, 5);
	    TheRace.completed = sqlite3_column_int(srstmt, 6);
		TheRace.racestate = sqlite3_column_int(srstmt, 7);
		RacesByTime.push_back(TheRace);
	    sqlret = sqlite3_step(srstmt);
		if ( sqlret != SQLITE_DONE && sqlret != SQLITE_ROW ) {
          cout << "Could not step ( srstmt ) to next row of race info error code " << sqlret << endl;
		  sqlite3_close(db);
          nowrunning = 0;
          Sleep(500);
	      return(1);
  	    } 
	  }
	}
    sqlret = sqlite3_reset(srstmt);
    if ( sqlret != SQLITE_OK ) {
      cout << "Could not reset ( srstmt ) end loop error code " << sqlret << endl;
  	} 
    sqlret = sqlite3_finalize(srstmt);
    if ( sqlret != SQLITE_OK ) {
      cout << "Could not finalize ( srstmt ) end loop error code " << sqlret << endl;
  	} 
	whichone = 0;
	// Loop reading races from vector
	for ( vector<RaceData>::iterator racesnext = RacesByTime.begin(); racesnext != RacesByTime.end(); racesnext++ ) {
      // Touch the watchdog file
      touchwatchdogfile("0150", watchdogfile);
      TheRace.id = racesnext->id;
	  TheRace.starttime = racesnext->starttime;
	  TheRace.country = racesnext->country;
	  TheRace.venue = racesnext->venue;
	  TheRace.name = racesnext->name;
	  TheRace.exchangeId = racesnext->exchangeId;
	  TheRace.completed = racesnext->completed;
	  TheRace.racestate = racesnext->racestate;
	  racecompleted = TheRace.completed;
      racerun = 0;
      char *ord = NULL;
	  struct tm thestart = {};
 	  time_t stime = BFApi->timeGMTtoUK(TheRace.starttime);
	  gmtime_s( &thestart, (const time_t *)&stime);
	  strftime(dbuff1,sizeof(dbuff1),"%d",&thestart);
	  ord = dateord(thestart.tm_mday);
	  strftime(dbuff2,sizeof(dbuff2),"%b",&thestart);
	  strftime(tbuff,sizeof(tbuff),"%H:%M",&thestart);
      // Now looking for runners in the races
      // Get market static data for the event
	  { // Bracket to create scope to stop memory leakage
        vector<MarketData> ActiveRunners;
	    // This is done to speed up the cycle as there is a limit of 5 per minute on the Free API
	    sqlret = sqlite3_prepare_v2(db, selmarkets, -1, &smarkets, NULL);
        if (sqlret != SQLITE_OK) {
	      cout << "Could not prepare " << selmarkets << " Error Code: " <<  sqlret << endl;
        }
        sprintf_s(keybuff, CHAR100, "%d-%d", TheRace.exchangeId, TheRace.id);
		sqlret = sqlite3_bind_text(smarkets, 1, keybuff,  strlen(keybuff),  SQLITE_STATIC);
        if (sqlret != SQLITE_OK) {
	      cout << "Could not bind " << keybuff << " to smarkets Error Code: " <<  sqlret << endl;
        }
        sqlret = sqlite3_step(smarkets);
        if ( sqlret != SQLITE_ROW && sqlret != SQLITE_DONE ) {
          cout << "Could not step( smarkets ) error code " << sqlret << endl;
		}
        
        if ( sqlret != SQLITE_ROW ) {
		  sqlret = sqlite3_reset(smarkets);
		  if (sqlret != SQLITE_OK) {
	        cout << "Could not reset( smarkets ) Error Code: " <<  sqlret << endl;
          }
          sqlret = sqlite3_finalize(smarkets);
		  if (sqlret != SQLITE_OK) {
	        cout << "Could not finalize( smarkets ) Error Code: " <<  sqlret << endl;
          }
          vector<MarketData> TmpActiveRunners;
		  // No race information found in database so get it from Betfair this time
          cout << "No race information found in database for " << TheRace.country << " " << TheRace.venue << " " << dbuff1 <<  ord << " " << dbuff2 << " " << tbuff << " " << TheRace.name << " Retrieving from Betfair" << endl;
	      // start timer
          QueryPerformanceCounter(&gmt2);
          // Only allowed 5 a minute of this next call so 1 every 12 seconds, just over for safety
	      LONGLONG ElapsedMSCount = gmt2.QuadPart - gmt1.QuadPart;
	      LONGLONG MilliMSSeconds = (ElapsedMSCount * 1000) / sfrequency.QuadPart;
	      long pa2 = 0;
	      if ( MilliMSSeconds < PERMIN5 ) {
            pa2 = (long)(PERMIN5 - MilliMSSeconds);
		    DWORD ret2 = 0;
            ret2 = WaitForSingleObject( ghMutex, pa2 );
			if ( WAIT_TIMEOUT != ret2 ) {
		      cout << "Object wait timeout error : " << ret2 << endl;
			}
		  }
  	      // Touch the watchdog file
          touchwatchdogfile("0160", watchdogfile);
		  BFAPI_Retvals gmval = BFApi->getMarket(TheRace.id, TmpActiveRunners);
		  if ( BFAPI_OK != gmval ) {
		    if ( BFAPI_SOAPERROR == gmval ) {
              cout << "SOAP Error retrieving Market Info from Betfair. Exit program and restart it." << endl;
	          if ( NULL != db ) {
                sqlite3_close(db);
              }
              nowrunning = 0;
              Sleep(500);
			  // Return value 2 equals restart program
	          return(2);
			}
			cout << "Error retrieving static race data from Betfair code : " << gmval << endl;
		  }
          // Reset the timer
          QueryPerformanceCounter(&gmt1);
	      getmarkets = TmpActiveRunners.begin();
		  // If we did not get any data
		  if ( getmarkets == TmpActiveRunners.end() ) {
	        // This is not an error, it will happen when a race has been run and is no
		    // longer available on betfair but is still in our list of races to check
		    cout << "No market information available for " << TheRace.country << " " << TheRace.venue << " " << TheRace.name << endl;
		  } else {
  	        sqlret = sqlite3_prepare_v2(db, insertmarkets, -1, &imarkets, NULL);
			if ( sqlret != SQLITE_OK ) {
              cout << "Failed to prepare " << insertmarkets << " error code : " << sqlret << endl;
	          sqlite3_close(db);
	  	      nowrunning = 0;
  	          Sleep(500);
  	          return(1);
	        }
            // Load it into database for more frequent retrieval later
	        sprintf_s(keybuff, CHAR100, "%d-%d", getmarkets->licenceId, getmarkets->marketId);
	        sqlite3_bind_text(imarkets,    1, keybuff,  strlen(keybuff),  SQLITE_STATIC);
	        sqlite3_bind_text(imarkets,    2, getmarkets->countryISO3.c_str(), strlen(getmarkets->countryISO3.c_str()), SQLITE_STATIC);
	        sqlite3_bind_int(imarkets,     3, getmarkets->discountAllowed);
	        sqlite3_bind_int(imarkets,     4, getmarkets->eventTypeId);
	        sqlite3_bind_int64(imarkets,   5, getmarkets->lastRefresh);
	        sqlite3_bind_int(imarkets,     6, getmarkets->licenceId);
	        sqlite3_bind_double(imarkets,  7, getmarkets->marketBaseRate);
	        sqlite3_bind_text(imarkets,    8, getmarkets->marketDescription.c_str(), strlen(getmarkets->marketDescription.c_str()), SQLITE_STATIC);
	        sqlite3_bind_int(imarkets,     9, getmarkets->marketDescriptionHasDate);
	        sqlite3_bind_int64(imarkets,  10, getmarkets->marketDisplayTime);
	        sqlite3_bind_int(imarkets,    11, getmarkets->marketId);
	        sqlite3_bind_int(imarkets,    12, getmarkets->marketStatus);
	        sqlite3_bind_int64(imarkets,  13, getmarkets->marketSuspendTime);
	        sqlite3_bind_int64(imarkets,  14, getmarkets->marketTime);
	        sqlite3_bind_int(imarkets,    15, getmarkets->marketType);
	        sqlite3_bind_int(imarkets,    16, getmarkets->marketTypeVariant);
	        sqlite3_bind_text(imarkets,   17, getmarkets->menuPath.c_str(), strlen(getmarkets->menuPath.c_str()), SQLITE_STATIC);
	        sqlite3_bind_text(imarkets,   18, getmarkets->name.c_str(), strlen(getmarkets->name.c_str()), SQLITE_STATIC);
	        sqlite3_bind_int(imarkets,    19, getmarkets->numberOfWinners);
	        sqlite3_bind_int(imarkets,    20, getmarkets->parentEventId);
	        sqlite3_bind_int(imarkets,    21, getmarkets->unit);
	        sqlite3_bind_int(imarkets,    22, getmarkets->minUnitValue);
	        sqlite3_bind_int(imarkets,    23, getmarkets->maxUnitValue);
	        sqlite3_bind_int(imarkets,    24, getmarkets->interval);
	        sqlite3_bind_int(imarkets,    25, getmarkets->runnersMayBeAdded);
	        sqlite3_bind_text(imarkets,   26, getmarkets->timezone.c_str(), strlen(getmarkets->timezone.c_str()), SQLITE_STATIC);
            // Next two vectors currently not used by this program
	        // .eventHierachy
	        // .couponLinks
            sqlret = sqlite3_step(imarkets);
            if (sqlret != SQLITE_DONE) {
	          cout << "Could not step (execute) imarkets Error Code: " <<  sqlret << "Primary Key " << keybuff << endl;
            }
            sqlret = sqlite3_reset(imarkets);
			if (sqlret != SQLITE_OK) {
	          cout << "Could not reset( imarkets ) Error Code: " <<  sqlret << endl;
            }
            sqlret = sqlite3_finalize(imarkets);
            if (sqlret != SQLITE_OK) {
	          cout << "Could not finalize( imarkets ) Error Code: " <<  sqlret << endl;
            }
            sqlret = sqlite3_prepare_v2(db, irunners, -1, &insrunners, NULL);   
            if (sqlret != SQLITE_OK) {
	          cout << "Could not prepare " << irunners << " Error Code: " <<  sqlret << endl;
            }
            // Now save the vector of runners for this market
            for (insrunnerid = getmarkets->runners.begin(); insrunnerid != getmarkets->runners.end(); insrunnerid++) {        
              // Load it into database for more frequent retrieval later
	          sprintf_s(keybuff, CHAR100, "%d-%d-%d", getmarkets->licenceId, getmarkets->marketId, insrunnerid->selectionId);
			  sqlite3_bind_text(insrunners,    1, keybuff,  strlen(keybuff),  SQLITE_STATIC);
	  	      sqlite3_bind_int(insrunners,     2, getmarkets->licenceId);
		      sqlite3_bind_int(insrunners,     3, getmarkets->marketId);
		      sqlite3_bind_int(insrunners,     4, insrunnerid->asianLineId);
		      sqlite3_bind_double(insrunners,  5, insrunnerid->handicap);
		      sqlite3_bind_text(insrunners,    6, insrunnerid->name.c_str(), strlen(insrunnerid->name.c_str()), SQLITE_STATIC);
		      sqlite3_bind_int(insrunners,     7, insrunnerid->selectionId);
		      sqlret = sqlite3_step(insrunners);
              if (sqlret != SQLITE_DONE) {
	            cout << "Could not step (execute) insrunners Error Code: " <<  sqlret << "Primary Key " << keybuff << endl;
              }
              sqlret = sqlite3_reset(insrunners);
			  if (sqlret != SQLITE_OK) {
	            cout << "Could not reset( insrunners ) in loop Error Code: " <<  sqlret << endl;
              }
		    }
            sqlret = sqlite3_reset(insrunners);
		    if (sqlret != SQLITE_OK) {
	          cout << "Could not reset( insrunners ) Error Code: " <<  sqlret << endl;
            }
            sqlret = sqlite3_finalize(insrunners);
		    if (sqlret != SQLITE_OK) {
	          cout << "Could not finalize( insrunners ) Error Code: " <<  sqlret << endl;
            }
          }
		  // Data now in database so now retrieve it again
          sqlret = sqlite3_prepare_v2(db, selmarkets, -1, &smarkets, NULL);
          if (sqlret != SQLITE_OK) {
	        cout << "Could not prepare " << selmarkets << " Error Code: " <<  sqlret << endl;
          }
          sprintf_s(keybuff, CHAR100, "%d-%d", TheRace.exchangeId, TheRace.id);
          sqlret = sqlite3_bind_text(smarkets, 1, keybuff,  strlen(keybuff),  SQLITE_STATIC);
          if (sqlret != SQLITE_OK) {
	        cout << "Could not bind " << keybuff << " to smarkets Error Code: " <<  sqlret << endl;
          }
          sqlret = sqlite3_step(smarkets);
          if ( sqlret != SQLITE_ROW && sqlret != SQLITE_DONE ) {
            cout << "Could not step( smarkets ) error code " << sqlret << endl;
		  }
		  if ( sqlret != SQLITE_ROW ) {
            cout << "No market information found in database for " << TheRace.country << " " << TheRace.venue << " retrieving from Betfair appears to have inserted nothing" << endl;
		  }
	    } 
		// Data now in currently selected database record so load up internal structures from there
		MarketData MarketTmp = {};
	    MarketTmp.countryISO3.assign((const char *)sqlite3_column_text(smarkets, 0));
	    MarketTmp.discountAllowed = (sqlite3_column_int(smarkets, 1) != 0);
	    MarketTmp.eventTypeId = sqlite3_column_int(smarkets, 2);
	    MarketTmp.lastRefresh = sqlite3_column_int64(smarkets, 3);
	    MarketTmp.licenceId = sqlite3_column_int(smarkets, 4);
	    MarketTmp.marketBaseRate = sqlite3_column_int(smarkets, 5);
	    MarketTmp.marketDescription.assign((const char *)sqlite3_column_text(smarkets, 6));
	    MarketTmp.marketDescriptionHasDate = (sqlite3_column_int(smarkets, 7) != 0);
	    MarketTmp.marketDisplayTime = sqlite3_column_int(smarkets, 8);
	    MarketTmp.marketId = sqlite3_column_int(smarkets, 9);
	    MarketTmp.marketStatus = (BFAPI_MktStatus)sqlite3_column_int(smarkets, 10);
	    MarketTmp.marketSuspendTime = sqlite3_column_int(smarkets, 11);
	    MarketTmp.marketTime = sqlite3_column_int(smarkets, 12);
	    MarketTmp.marketType = sqlite3_column_int(smarkets, 13);
	    MarketTmp.marketTypeVariant = sqlite3_column_int(smarkets, 14);
	    MarketTmp.menuPath.assign((const char *)sqlite3_column_text(smarkets, 15));
	    MarketTmp.name.assign((const char *)sqlite3_column_text(smarkets, 16));
	    MarketTmp.numberOfWinners = sqlite3_column_int(smarkets, 17);
	    MarketTmp.parentEventId = sqlite3_column_int(smarkets, 18);
        MarketTmp.unit = sqlite3_column_int(smarkets, 19);
        MarketTmp.minUnitValue = sqlite3_column_int(smarkets, 20);
        MarketTmp.maxUnitValue = sqlite3_column_int(smarkets, 21);
        MarketTmp.interval = sqlite3_column_int(smarkets, 22);
	    MarketTmp.runnersMayBeAdded = (sqlite3_column_int(smarkets, 23) != 0);
	    MarketTmp.timezone.assign((const char *)sqlite3_column_text(smarkets, 24));
	    // Next two vectors currently not used by this program
	    // .eventHierachy
	    // .couponLinks
        int lsqlret = sqlite3_reset(smarkets);
		if (lsqlret != SQLITE_OK) {
	      cout << "Could not reset( smarkets ) Error Code: " <<  lsqlret << endl;
        }
        lsqlret = sqlite3_finalize(smarkets);
		if (lsqlret != SQLITE_OK) {
	      cout << "Could not finalize( smarkets ) Error Code: " <<  lsqlret << endl;
        }
        // Get runners vector
        sqlret = sqlite3_prepare_v2(db, srunners, -1, &selrunners, NULL);
        if ( sqlret != SQLITE_OK ) {
          cout << "Could not prepare( srunners ) error code " << sqlret << endl;
	  	} 
	    sqlret = sqlite3_bind_int(selrunners, 1, MarketTmp.licenceId);
        if ( sqlret != SQLITE_OK ) {
		  cout << "Could not bind " << MarketTmp.licenceId << " to " << selrunners << " error code " << sqlret << endl;
		} 
	    sqlret = sqlite3_bind_int(selrunners, 2, MarketTmp.marketId);
        if ( sqlret != SQLITE_OK ) {
		  cout << "Could not bind " << MarketTmp.marketId << " to " << selrunners << " error code " << sqlret << endl;
	  	} 
        sqlret = sqlret = sqlite3_step(selrunners);
        if ( sqlret != SQLITE_ROW && sqlret != SQLITE_DONE ) {
          cout << "Could not step( selrunners ) error code : " << sqlret << endl;
        }
        else if ( sqlret != SQLITE_ROW ) {
          cout << "No runners information found in database for " << TheRace.country << " " << TheRace.venue << " " << dbuff1 <<  ord << " " << dbuff2 << " " << tbuff << " " << TheRace.name << " error code : " << sqlret << endl;
        }
        // Touch the watchdog file
        touchwatchdogfile("0170", watchdogfile);
	    while (sqlret == SQLITE_ROW ) {
	      MarketRunners TmpRunners = {};
	      TmpRunners.asianLineId = sqlite3_column_int(selrunners, 2);
	      TmpRunners.handicap = sqlite3_column_double(selrunners, 3);
	      TmpRunners.name.assign((const char *)sqlite3_column_text(selrunners, 4));
	      TmpRunners.selectionId = sqlite3_column_int(selrunners, 5); 
		  MarketTmp.runners.push_back(TmpRunners);
          sqlret = sqlite3_step(selrunners);
          if ( sqlret != SQLITE_ROW && sqlret != SQLITE_DONE ) {
            cout << "Could not step( selrunners ) in loop error code : " << sqlret << endl;
          }
	    }
  	    ActiveRunners.push_back(MarketTmp);
		sqlret = sqlite3_reset(selrunners);
        if ( sqlret != SQLITE_OK ) {
          cout << "Could not reset( selrunners ) error code " << sqlret << endl;
	  	} 
        sqlret = sqlite3_finalize(selrunners);
        if ( sqlret != SQLITE_OK ) {
          cout << "Could not finalize( selrunners ) error code " << sqlret << endl;
	  	} 
		// Get market prices for event
		if ( 0 == racecompleted ) { // Means when 0 race not completed
	      vector<MarketPrices> MktPrices;
          // start timer
          QueryPerformanceCounter(&gpt2);
          // Allow 70 per minute (marketprices + marketpricescompressed, 858 milliseconds, add a little extra for safety.
	      LONGLONG ElapsedCount = gpt2.QuadPart - gpt1.QuadPart;
	      LONGLONG MilliSeconds = (ElapsedCount * 1000) / sfrequency.QuadPart;
	      long pa1 = 0;
	      if ( MilliSeconds < PERMIN70 ) {
            pa1 = (long)(PERMIN70 - MilliSeconds);
		    DWORD ret1 = 0;
            ret1 = WaitForSingleObject( ghMutex, pa1 );
			if ( WAIT_TIMEOUT != ret1 ) {
		      cout << "Object wait timeout error : " << ret1 << endl;
			}
		  }
		  // Using MarketPrices and MarketPricesCompressed to get most speed out of free API
		  BFAPI_Retvals gotpr = BFAPI_OK;
		  if ( whichone < 7 ) {
            gotpr = BFApi->getMarketPricesCompressed(TheRace.id, MktPrices);
	        whichone++;
          } else {
            gotpr = BFApi->getMarketPrices(TheRace.id, MktPrices);
	        whichone = 0;
          }
          // Reset the timer
          QueryPerformanceCounter(&gpt1);
	      if ( BFAPI_OK == gotpr ) {
	        // Map for Prices to look up along with other runner information by selectionId quickly
            std::map<int, struct RPrices, intclasscomp> PricesbyselectionId;
	        // Load the prices map for event
            if (MktPrices.begin() != MktPrices.end()) {
				for ( tprit = MktPrices.begin()->runnerPrices.begin(); tprit != MktPrices.begin()->runnerPrices.end(); tprit++){
	            PricesbyselectionId[tprit->selectionId] = *tprit;
	          }
	        }
	        // Touch the watchdog file
            touchwatchdogfile("0180", watchdogfile);
	        for (marketsid = ActiveRunners.begin(); marketsid != ActiveRunners.end(); marketsid++) {
			  // If market is still active
		      if ( BFAPI_MKTSTAT_ACTIVE == marketsid->marketStatus ) {
		        cout << "========================================" << endl;
				cout << TheRace.country << " " << TheRace.venue << " " << dbuff1 <<  ord << " " << dbuff2 << " " << tbuff << " " << TheRace.name << endl;
		        cout << "========================================" << endl;
			    if ( marketsid->runners.begin() == marketsid->runners.end() ) {
                  cout << "No runners found at event" << endl;
				}
		        // For each runner at the event
			    int rstart = 0;
	            for (runnerid = marketsid->runners.begin(); runnerid != marketsid->runners.end(); runnerid++) {
				  if ( 0 == rstart ) {
                    // Looking up the prices for the runner from prices map
	                SelPrice = PricesbyselectionId.find(int(runnerid->selectionId));
				    if ( SelPrice != PricesbyselectionId.end() ) {
		  		      // Do not look for start of races while still loading database with static race info records
                      // Determine if race has started
  					  if ( marketsid->runners.size() > 1 ) {        // More than 1 runner
		                chkinplay = MktPrices.begin();
					    if ( chkinplay != MktPrices.end() ) {       // Some prices
                          if ( chkinplay->delay > 0 ) {             // Delay exceeds 0
						    if ( 1 == TheRace.racestate ) {         // Can only go in play from waiting to run
                              // If prices available for the runner
		                      if ( SelPrice != PricesbyselectionId.end() ) {
	                            btlit = SelPrice->second.bestPricesToLay.begin();
					            if ( btlit != SelPrice->second.bestPricesToLay.end() ) {
						          // Touch the watchdog file
                                  touchwatchdogfile("0190", watchdogfile);
                                  cout << " ***GOING INPLAY***" << endl;
					              rstart = 1;
								}
					          }
					        } else if ( (0 == TheRace.racestate) || (2 == TheRace.racestate) ) {
                              // Race in wrong race state for start of race so missed it, set to race completed
                              // Increment the missed races today counter in Configuration database
			                  int dmisses = CFreadracemissed(db);
			                  dmisses++;
			                  CFsetracemissed(db, dmisses);
			                  // Set day last updated
                              CFsettodayis(db);
							  TheRace.racestate = 3;
							  TheRace.completed = 1;
					          sqlite3_stmt *rtup10 = NULL;
							  const char *rtupdate10 = "UPDATE Races SET  racestate = 3, completed = 1 WHERE (primkey == ?);";
                              sqlret = sqlite3_prepare_v2(db, rtupdate10, -1, &rtup10, NULL);
                              if ( sqlret != SQLITE_OK ) {
                                cout << "Failed to prepare rtupdate10 database table error code : " << sqlret << endl;
                                sqlite3_close(db);
                                nowrunning = 0;
                                Sleep(500);
                                return(1);
                              }
							  char keybuff[CHAR100] = {0};
	                          sprintf_s(keybuff, CHAR100, "%d-%d", TheRace.exchangeId, TheRace.id);
	                          sqlite3_bind_text(rtup10,  1, keybuff,  strlen(keybuff),  SQLITE_STATIC);
	                          sqlret = sqlite3_step(rtup10);
	                          if (sqlret != SQLITE_DONE) {
	                            cout << "Could not step (execute) rtup10 Error Code: " <<  sqlret << "Primary Key " << keybuff << endl;
                                nowrunning = 0;
	                            Sleep(500);
		                        return(1);
                              }
                              sqlret = sqlite3_reset(rtup10);
  	                          if (sqlret != SQLITE_OK) {
	                            cout << "Could not reset( rtup10 ) Error Code: " <<  sqlret << endl;
	                            nowrunning = 0;
	                            Sleep(500);
		                        return(1);
                              }
                              sqlret = sqlite3_finalize(rtup10);
  	                          if (sqlret != SQLITE_OK) {
	                            cout << "Could not finalize( rtup10 ) Error Code: " <<  sqlret << endl;
	                            nowrunning = 0;
	                            Sleep(500);
		                        return(1);
                              }                       
						    }
						  } else {
                            if ( 0 == TheRace.racestate ) {
                              TheRace.racestate = 1;
							  TheRace.completed = 0;
					          sqlite3_stmt *rtup11 = NULL;
							  const char *rtupdate11 = "UPDATE Races SET  racestate = 1, completed = 0 WHERE (primkey == ?);";
                              sqlret = sqlite3_prepare_v2(db, rtupdate11, -1, &rtup11, NULL);
                              if ( sqlret != SQLITE_OK ) {
                                cout << "Failed to prepare rtupdate11 database table error code : " << sqlret << endl;
                                sqlite3_close(db);
                                nowrunning = 0;
                                Sleep(500);
                                return(1);
                              }
							  char keybuff[CHAR100] = {0};
	                          sprintf_s(keybuff, CHAR100, "%d-%d", TheRace.exchangeId, TheRace.id);
	                          sqlite3_bind_text(rtup11,  1, keybuff,  strlen(keybuff),  SQLITE_STATIC);
	                          sqlret = sqlite3_step(rtup11);
	                          if (sqlret != SQLITE_DONE) {
	                            cout << "Could not step (execute) rtup11 Error Code: " <<  sqlret << "Primary Key " << keybuff << endl;
                                nowrunning = 0;
	                            Sleep(500);
		                        return(1);
                              }
                              sqlret = sqlite3_reset(rtup11);
  	                          if (sqlret != SQLITE_OK) {
	                            cout << "Could not reset( rtup11 ) Error Code: " <<  sqlret << endl;
	                            nowrunning = 0;
	                            Sleep(500);
		                        return(1);
                              }
                              sqlret = sqlite3_finalize(rtup11);
  	                          if (sqlret != SQLITE_OK) {
	                            cout << "Could not finalize( rtup11 ) Error Code: " <<  sqlret << endl;
	                            nowrunning = 0;
	                            Sleep(500);
		                        return(1);
                              }
							}
						  }
				        }
					  }
					  if ( rstart > 0 ) {
                        // Monitor the race until finished
						vector<MarketData>::pointer tptr = &ActiveRunners[0];
				        // Touch the watchdog file
                        touchwatchdogfile("0200", watchdogfile);
					    watchtherace(db, &whichone, tptr, &TheRace, ghMutex, raceresultsfile);
                        racerun = 1;
						// Time stamp output here to help debug races being missed
						struct tm timing = {};
						char sbuff[50] = {};
                        time_t stime = BFApi->timeGMTtoUK(time(NULL));
                        gmtime_s( &timing, (const time_t *)&stime);
                        strftime(sbuff,sizeof(sbuff),"%H:%M:%S",&timing);
                        cout << "Ended and Settled : " << sbuff << endl;
					  }
					  // Display prices for runners, commented out to tidy screen
					  //else {
                      //  int dtitle = 1;
		              //  for ( btlit = SelPrice->second.bestPricesToLay.begin(); btlit != SelPrice->second.bestPricesToLay.end(); btlit++) {
                      //    // Get all the lay prices for the runner
					  //    if ( btlit->price > 0 ) {
	 		          //      if ( dtitle > 0 ) {
		              //        cout << runnerid->name << " ";
			          //        dtitle = 0;
			 	      //      }
				      //      cout << "[" << btlit->depth << ":" << btlit->price << ":" << btlit->amountAvailable << "] ";
		 	          //    }
		              //  }
		              //  if ( dtitle < 1 ) {
			          //    cout << "{" << SelPrice->second.totalAmountMatched << "}" << endl;
			          //  }
					  //}
				    } else {
				      cout << " No prices found for : " << runnerid->name << endl;
				    }
				  } // End if 0 == rstart
			    } // End getting prices for each runner at event
	          } // End if market is active
            } // End For market data
		  } else {
		    if ( BFAPI_SOAPERROR == gotpr ) {
              cout << "SOAP Error retrieving race odds from Betfair. Exit program and restart it." << endl;
	          if ( NULL != db ) {
                sqlite3_close(db);
              }
              nowrunning = 0;
              Sleep(500);
			  // Return value 2 equals restart program
	          return(2);
			} else {
			  cout << "Error retrieving race odds from Betfair code : " << gotpr << endl;
			}
		  }
		} // 0 == racecompleted
		// Seems to count towards throttle even if no runners returned
        mktcnt++;
	  } // End scope for memory leak fixing
      // Looping around so check if last race looked at was just run
      // Touch the watchdog file
      touchwatchdogfile("0210", watchdogfile);
	  if ( 1 == racerun ) {
		// Race was run so exit loop to update database and start loop again
        break;
	  }
    } // End Loop reading list of races in time order from vector
	if ( 1 == racerun ) {
	  // First update the race just run in the races table
      TheRace.racestate = 3;
	  TheRace.completed = 1;
      sqlite3_stmt *rtup12 = NULL;
	  const char *rtupdate12 = "UPDATE Races SET  racestate = 3, completed = 1 WHERE (primkey == ?);";
      sqlret = sqlite3_prepare_v2(db, rtupdate12, -1, &rtup12, NULL);
      if ( sqlret != SQLITE_OK ) {
        cout << "Failed to prepare rtupdate12 database table error code : " << sqlret << endl;
        sqlite3_close(db);
        nowrunning = 0;
        Sleep(500);
        return(1);
      }
	  char keybuff[CHAR100] = {0};
	  sprintf_s(keybuff, CHAR100, "%d-%d", TheRace.exchangeId, TheRace.id);
	  sqlite3_bind_text(rtup12,  1, keybuff,  strlen(keybuff),  SQLITE_STATIC);
	  sqlret = sqlite3_step(rtup12);
	  if (sqlret != SQLITE_DONE) {
	    cout << "Could not step (execute) rtup12 Error Code: " <<  sqlret << "Primary Key " << keybuff << endl;
        nowrunning = 0;
	    Sleep(500);
		return(1);
      }
      sqlret = sqlite3_reset(rtup12);
  	  if (sqlret != SQLITE_OK) {
	    cout << "Could not reset( rtup12 ) Error Code: " <<  sqlret << endl;
	    nowrunning = 0;
	    Sleep(500);
		return(1);
      }
      sqlret = sqlite3_finalize(rtup12);
  	  if (sqlret != SQLITE_OK) {
	    cout << "Could not finalize( rtup12 ) Error Code: " <<  sqlret << endl;
	    nowrunning = 0;
	    Sleep(500);
		return(1);
      }
      // Now reset all the others that are not race status completed to race status unknown
	  // This prevents an allready started race from being selected, as it must see it go from
	  // unknown to not in running to in running, cannot go from unkown to inrunning.
	  const char *rtupdate1 = "UPDATE Races SET  racestate = 0  WHERE (racestate != 3);";
      sqlret = sqlite3_exec(db,rtupdate1,0,0,0);
	  if ( sqlret != SQLITE_OK ) {
	    cout << "Failed to update Races database table error code : " << sqlret << endl;
	    sqlite3_close(db);
	    nowrunning = 0;
	    Sleep(500);
	    return(1);
	  }
	}
  // Outside of test for race run, as races can also be marked complete if missed.
  // Determine if any more races today
    frace[0] = 0;
    lrace[0] = 0;
	moreracestoday = 0;
    if ( findfirstlastrace(db, BFApi, &frace[0], sizeof(frace), &lrace[0], sizeof(lrace), &moreracestoday)) {
      if ( 0 == moreracestoday ) {
        cout << "No more races for day detected, setting loop to exit program" << sqlret << endl;
        endtime = time(NULL);
      }
	}
    sqlret = sqlite3_close(db);
    if ( sqlret != SQLITE_OK ) {
      cout << "Could not close at end sqlret loop error code " << sqlret << endl;
  	}
	// Allow time to close
	Sleep(50);
	// Reset the SOAP interface
	BFApi->reSetSOAP();
	// Re-open database connection
    sqlret = sqlite3_open(dbfile, &db);
    if (sqlret != SQLITE_OK)  {
      cout << "Could not reopen database" << endl;
	  db = NULL;
      break;
	}
	// After first loop set period of interest to window around current time
    starttime = time(NULL) - PRESLOT;
	lateststart = time(NULL) + POSTSLOT;
    // Touch the watchdog file
    touchwatchdogfile("0220", watchdogfile);
  }  // End while time() < endtime
  cout << "Exiting program" << endl;
  sqlret = sqlite3_exec(db,rtdrop1,0,0,0);
  if ( sqlret != SQLITE_OK ) {
    cout << "Failed to drop database table Races error code : " << sqlret << endl;
  }
  sqlret = sqlite3_exec(db,rtdrop2,0,0,0);
  if ( sqlret != SQLITE_OK ) {
    cout << "Failed to drop database table SMarket error code : " << sqlret << endl;
  }
  sqlret = sqlite3_exec(db,rtdrop3,0,0,0);
  if ( sqlret != SQLITE_OK ) {
    cout << "Failed to drop database table Runners error code : " << sqlret << endl;
  }
  sqlret = sqlite3_exec(db,rtdrop4,0,0,0);
  if ( sqlret != SQLITE_OK ) {
   cout << "Failed to drop database table AvgRacetimes error code : " << sqlret << endl;
  }
  int numbfgavetoday = CFreadbfgaveresult(db);
  int numbotsawtoday = CFreadbotsawresult(db);
  int numracestoday = CFreadracestoday(db);
  int winnerstoday = CFreadwinstoday(db);
  int stopsdonetoday = CFreadstopstoday(db);
  int loserstoday = CFreadlosestoday(db);
  int drawstoday = CFreadracedraw(db);
  SPinstakingset( db, BFApi, ghMutex, &tsteps, &tcumreward, &tctarget);
  double lastcstakes = 0.0;
  if ( tsteps > 0 ) {
    lastcstakes = SPreadlaststake( db );
  }
  int nomatchedbetstoday = CFreadnomatchedbets(db);
  int oddsnotfoundtoday = CFreadoddsnotfound(db);
  int racesmissedtoday = CFreadracemissed(db);
  if ( NULL != db ) {
    sqlite3_close(db);
  }
  // Append to log file of events of note.
  char shutdownb[30] = {0};
  time_t shutdown = BFApi->timeGMTtoUK(time(NULL));
  struct tm shutdowntiming = {};
  gmtime_s( &shutdowntiming, (const time_t *)&shutdown);
  strftime(shutdownb,sizeof(shutdownb),"%Y/%m/%d %H:%M:%S",&shutdowntiming);
  outfile.open("c:\\tools\\betfairbot1.txt", std::ios_base::app);
  outfile << shutdownb << " UK:Betfairbot1 Ended" << endl; 
  outfile.close();
  // Email software end event to monitoring email account
  time_t emailq = BFApi->timeGMTtoUK(time(NULL));
  struct tm pemailq = {};
  char emailqbuff[50] = {0};
  gmtime_s( &pemailq, (const time_t *)&emailq);
  strftime(emailqbuff,sizeof(emailqbuff),"%a %d %b %Y at %H:%M:%S",&pemailq);
  // Get the bank balance from Betfair
  openbbali = 0;
  openbbald2 = 0.0;
  TheFunds.clear();

  if ( BFAPI_OK == BFApi->getAccountFunds( TheFunds ) ) { 
    vector<AccountFunds>::iterator fuit = TheFunds.begin();
    openbbali = fuit->balance;
	  // Convert pennies to pounds and pence
    openbbald2 = ((double)openbbali) / 100;
    openbbald2 = floor(openbbald2 * 10000 + 0.5) / 10000;
  }
  // Convert pennies to pounds and pence
  tctarget2 = 0.0;
  tctarget2 = ((double)tctarget) / 100;
  tctarget2 = floor(tctarget2 * 10000 + 0.5) / 10000;
  BFApi->destroySOAP();
  delete(BFApi);
  sprintf_s( emailmsg, sizeof(emailmsg), "Run Ended\rUK Date and Time : %s\rProfit today : GBP%4.2f\rOpening balance : GBP%4.2f\rClosing balance : GBP%4.2f\r", emailqbuff, openbbald2 - openbbald1, openbbald1, openbbald2);
  sprintf_s( tmpemailmsg, sizeof(tmpemailmsg), "Races today : %d\rWinners today : %d\rLosers today : %d\rDraws today : %d\rBets not matched today : %d\rOdds not found today : %d\rRaces missed today : %d\rResults from Betfair : %d\rResults Bot detected : %d\rStaking plan resets today : %d\r", numracestoday, winnerstoday, loserstoday, drawstoday, nomatchedbetstoday, oddsnotfoundtoday, racesmissedtoday, numbfgavetoday, numbotsawtoday, stopsdonetoday);
  strcat_s(emailmsg, sizeof(emailmsg), tmpemailmsg);
  if ( tsteps > 0 ) {
    sprintf_s( tmpemailmsg, sizeof(tmpemailmsg), "Staking plan Step : %02d\r Last Stake : GBP%4.2f\r Target : GBP%4.2f\r", tsteps, lastcstakes, tctarget2);
  } else {
	  sprintf_s( tmpemailmsg, sizeof(tmpemailmsg), "Staking plan Step : 00\r Next Target : GBP%4.2f\r", tctarget2);
  }
  strcat_s(emailmsg, sizeof(emailmsg), tmpemailmsg);
  msgmail = emailmsg;
  char smsbody2[160] = {0};
  if ( tsteps > 0 ) {
    sprintf_s(smsbody2, sizeof(smsbody2), "BFB1:Ended:PR:GBP%4.2f:OB:GBP%4.2f:CB:GBP%4.2f:WIN:%d:LOSE:%d:DRAW:%d:RES:%d:SS:%d:LS:GBP%4.2f:TR:GBP%4.2f", openbbald2 - openbbald1, openbbald1, openbbald2, winnerstoday, loserstoday, drawstoday,stopsdonetoday,tsteps,lastcstakes,tctarget2);
  } else {
    sprintf_s(smsbody2, sizeof(smsbody2), "BFB1:Ended:PR:GBP%4.2f:OB:GBP%4.2f:CB:GBP%4.2f:WIN:%d:LOSE:%d:DRAW:%d:RES:%d:TR:GBP%4.2f", openbbald2 - openbbald1, openbbald1, openbbald2, winnerstoday, loserstoday, drawstoday,stopsdonetoday,tctarget2);
  }
  if (!SendEmail(smtpdom, authpass, fromnm, fromdom, tonam, todom, "Betfairbot1 Run Ended", msgmail, emailq, smsbody2)) {
    cout << "Failed to send run ended email" << endl;
  }
  // Indicate to threads we are ending
  nowrunning = 0;
  // Allow time for threads to stop and cleanup
  Sleep(500);
  // Delete the watchdog file
  remove( watchdogfile );
  cout << "Betfairbot1 Exited Normally" << endl;
  return 0;
}  // End main()
//------------------------------------------------------------------------------------------
// Given appropriate parameters watches an inplay race looking for first odds on favourite
//   whichone : Passed to keep get marketscompressed and getmarkets usage in sync with main
//              loop to prevent possible apparently random API throttling occurring.
//   
//------------------------------------------------------------------------------------------ 
int watchtherace(sqlite3 *thedb, int *whichone, vector<MarketData>::pointer MarketT, RaceData *TheRace, HANDLE waitobj, char *raceresults)
{
  // This function is called with a running in play markets race.
  // This watches the prices on just the live race and decides if a bet could be placed.
  // End of race when there is only one runner with lay prices, and it has only lay
  // prices and no back prices.
  // Has a timeout as well to prevent it getting stuck here, currrently hard coded at 10 minutes
  // If two races start the same time go for the one with the lowest lay odds on
  // any runner, the idea being this is most likely race to have an odds on favourite
  // to back.
  extern LARGE_INTEGER sfrequency; // ticks per second
  BetFairAPIFree_5 *BFApi = NULL;
  BFApi = BetFairAPIFree_5::getInstance();
  int sqlret = 0;
  int bfresult = -1;
  int testmode = CFreadtestmode( thedb );
  char startrunbuff[50] = {0};     // Buffer for start run time in readable format 
  char szbuff[50] = {0};           // Buffer for current UK time in readable format 
  char timetorunbuff[50] = {0};    // Buffer for current UK time in readable format 
  char timetofewbuff[50] = {0};    // Buffer for current UK time in readable format 
  char timetofallenbuff[50] = {0}; // Buffer for current UK time in readable format 
  char timetomanybuff[50] = {0};   // Buffer for current UK time in readable format 
  char racetooshortbuff[50] = {0}; // Buffer for current UK time in readable format 
  char timetoruns[50] = {0};       // Buffer for current UK time in readable format 
  char tbuff[50] = {0};            // Race start time according to card
  char sbuff[50] = {0};            // Start time
  char nbuff[50] = {0};            // End time
  char gbuff[50] = {0};            // Race duration
  char qbuff[50] = {0};            // Bet time
  char kbuff[50] = {0};            // Duration until bet
  char pbuff[50] = {0};            // Date stamp for .csv file entries
  double bprice = 0.0;             // Lay odds bet on
  int gotstartfav = 0;             // Flag to indicate if noted starting favourite
  std::string startingfav = "";    // Favourite at start of race
  std::string beton = "";          // Horse bet on
  std::string winner = "";         // Horse that won race
  std::string distance = "";       // Distance part of name
  std::string namep = "";          // Rest of name without the distance part
  time_t etime = 0;
  int betstopped = 0;              // Indicates bet placement was stopped by being out of betting placement time window
  int stoppedset = 0;              // Indicates outside of betting placement time window
  int numberofrunners = 0;         // Indicates bet placement stopped because too few runners left or too many when bet could be placed
  int numberoffurlongs = 0;        // Indicates bet placement stopped because race is too short to bet on
  int numberoffallers = 0;         // Indicates bet placement stopped because percentage of fallers exceeds limit
  int losestopped = 0;             // Indicates staking plan stopped betting placement
  int betplaced = 0;               // indicates a bet has been placed
  int betsallowedmsg = 0;          // Displayed betting period reached message
  int minimumrunners = 0;          // Store the minimum runners value
  int maximumrunners = 0;          // Store the maximum runners value
  int runnerswhenbet = -1;         // Number of runners still in race (with prices) when bet placed
  int startingrunners = 0;         // Number of runners at start of race
  int fallenlimit = 0;             // Percentage of starting runners fallen, range 0 to 100, >= to this and will not bet
  int minfurlongs = 0;             // Minimum furlongs of race below which will not bet on race
  LONG64 thebetid = 0;
  double someprice = 10000.0;
  double startingprice = 10000.0;
  int furlongs = 0;

  minfurlongs = CFreadminfurlongs( thedb );
  fallenlimit = CFreadmaxfallen( thedb );
  minimumrunners = CFreadminrunners( thedb );
  maximumrunners = CFreadmaxrunners( thedb );
  // Insert race timing data into database
  sqlite3_stmt *irtimes = NULL;
  const char *insertracetimes = "INSERT OR REPLACE INTO RaceTimes (country,venue,name,distance,racetime,racestarted,racefinished,duration,confirmed) VALUES (?, ?, ?, ?, ?, ?, ?, ?,?);";
  time_t racestarted = 0;
  time_t racefinished = 0;
  LARGE_INTEGER t1, t2;    // ticks
  vector<RPrices>::iterator IRtprit;
  vector<MarketRunners>::iterator IRrunnerid;
  vector<Prices>::iterator IRbtlit;
  std::map<int, struct RPrices, intclasscomp>::iterator IRSelPrice;
  struct tm timing = {};
  time_t stime = BFApi->timeGMTtoUK(time(NULL));
  gmtime_s( &timing, (const time_t *)&stime);
  racestarted = stime;
  strftime(sbuff,sizeof(sbuff),"%H:%M:%S",&timing);
  cout << "Started : " << sbuff << endl;
  struct tm thestart = {};
  struct tm thefew = {};
  struct tm thefallen = {};
  struct tm themany = {};
  struct tm thedist = {};
  struct tm theruns = {};
  time_t ztime = BFApi->timeGMTtoUK(TheRace->starttime);
  gmtime_s( &thestart, (const time_t *)&ztime);
  strftime(tbuff,sizeof(tbuff),"%H:%M",&thestart);
  
  int notover = 1;
  extractdistance(TheRace->name, distance, namep, &furlongs);
  sqlite3_stmt *srtimes1 = NULL;
  const char *selectracetimes1 = "SELECT nobetafter, nobetbefore, avgtime FROM AvgRaceTimes WHERE (distance = ?1)LIMIT 1;";
  // Prepare average race times read command
  sqlret = sqlite3_prepare_v2(thedb, selectracetimes1, -1, &srtimes1, NULL);
  if (sqlret != SQLITE_OK) {
    cout << "Could not prepare " << selectracetimes1 << " Error Code: " <<  sqlret << endl;
    return 0;
  }
  sqlite3_bind_text(srtimes1, 1, distance.c_str(),  strlen(distance.c_str()),  SQLITE_STATIC);
  sqlret = sqlite3_step(srtimes1);
  time_t timetorun = 0;
  time_t starttorun = 0;
  time_t avgtime = 0;
  if ( sqlret != SQLITE_ROW ) {
    cout << "Error retrieving times for distance from AvgRaceTimes Looking For " << distance.c_str() << " error code " << sqlret << endl;
  } else {
    timetorun = sqlite3_column_int64(srtimes1, 0);
    starttorun = sqlite3_column_int64(srtimes1, 1);
    avgtime = sqlite3_column_int64(srtimes1, 2);
    cout << " Retrieved avgtime : " << avgtime << " seconds nobetbefore : " << starttorun << " seconds nobetafter : " << timetorun << " seconds for distance : " << distance.c_str() << endl;
    timetorun += racestarted;
    starttorun += racestarted;
  }
  sqlret = sqlite3_reset(srtimes1);
  if (sqlret != SQLITE_OK) {
    cout << "Could not reset( AvgRaceTimes ) Error Code: " <<  sqlret << endl;
    return 0;
  }
  sqlret = sqlite3_finalize(srtimes1);
  if (sqlret != SQLITE_OK) {
    cout << "Could not finalize( AvgRaceTimes ) Error Code: " <<  sqlret << endl;
    return 0;
  }
  // Loop getting market prices for event
  QueryPerformanceCounter(&t1);
  while ( notover > 0 ) {
    // Touch the watchdog file
    touchwatchdogfile("0230", watchdogfile);
    // start timer
    QueryPerformanceCounter(&t2);
    // Allow 70 per minute (marketprices + marketpricescompressed, 858 milliseconds, add a little extra for safety.
    LONGLONG ElapsedCount = t2.QuadPart - t1.QuadPart;
    LONGLONG MilliSeconds = (ElapsedCount * 1000) / sfrequency.QuadPart;
    long pa1 = 0;
    if ( MilliSeconds < PERMIN70 ) {
      pa1 = (long)(PERMIN70 - MilliSeconds);
      DWORD ret1 = 0;
      if ( WAIT_TIMEOUT != ( ret1 = WaitForSingleObject( waitobj, pa1 )) ) {
        cout << "Object wait timeout error : " << ret1 << endl;
      }
    }
    { // Bracket to create scope to stop memory leakage
      vector<MarketPrices> IRMktPrices;
      IRMktPrices.clear();
      // Using MarketPrices and MarketPricesCompressed to get most speed out of free API
      BFAPI_Retvals gotpr = BFAPI_OK;
      if ( *whichone < 7 ) {
		    gotpr = BFApi->getMarketPricesCompressed(TheRace->id, IRMktPrices);
	      (*whichone)++;
      } else {
		    gotpr = BFApi->getMarketPrices(TheRace->id, IRMktPrices);
	      *whichone = 0;
      }
      // Reset the timer
      QueryPerformanceCounter(&t1);
	    if ( BFAPI_OK == gotpr ) {
	      // Map for Prices to look up along with other runner information by selectionId quickly
        std::map<int, struct RPrices, intclasscomp> IRPricesbyselectionId;
		    IRPricesbyselectionId.clear();
	      // Load the prices map for event
        if ( IRMktPrices.begin() != IRMktPrices.end()) {
 		      for ( IRtprit = IRMktPrices.begin()->runnerPrices.begin(); IRtprit != IRMktPrices.begin()->runnerPrices.end(); IRtprit++){
	        IRPricesbyselectionId[IRtprit->selectionId] = *IRtprit;
	      }
	    }
		  // If market is still active
		  if ( BFAPI_MKTSTAT_ACTIVE == MarketT->marketStatus ) {
		    if ( MarketT->runners.begin() == MarketT->runners.end() ) {
          cout << "watchtherace() Error No runners found at event" << endl;
			    return 0;
		    }
		    // For each runner at the event
		    int numruns = 0;
		    int winnerid = 0;
		    int betid = 0;
		    std::string nambuff;
		    std::string betbuff;
		    someprice = 10000.0;
	      for (IRrunnerid = MarketT->runners.begin(); IRrunnerid != MarketT->runners.end(); IRrunnerid++) {
          // Looking up the lay prices for the runner from prices map
	        IRSelPrice = IRPricesbyselectionId.find(int(IRrunnerid->selectionId));
		      if ( IRSelPrice != IRPricesbyselectionId.end()) {
			      // Go through prices, gather selection ID and name, lowest first position lay bet and number of runners with prices
            for ( IRbtlit = IRSelPrice->second.bestPricesToLay.begin(); IRbtlit != IRSelPrice->second.bestPricesToLay.end(); IRbtlit++ ) {
			        if ( IRbtlit == IRSelPrice->second.bestPricesToLay.begin() ) {
			          numruns++;
                // Get name and selectionId of runner, when only one runner found then this will be for that runner
			          winnerid = IRrunnerid->selectionId;
				        nambuff = IRrunnerid->name;
                // Collect details of runner with lowest lay odds in the first position, check this later to see if meets criteria
				        if ( IRbtlit->price < someprice ) {
                  someprice = IRbtlit->price;
  				        betbuff = IRrunnerid->name;
			            betid = IRrunnerid->selectionId;
				        }
				      }
			      }
			    }
		    }
		    // Note the starting favourite
		    if ( gotstartfav < 1 ) {
          startingfav = betbuff;
			    startingprice = someprice;
			    gotstartfav = 1;
			    cout << "Starting favourite is : " << startingfav << " : at odds of : " << someprice << endl;
		    }
		    if ( numruns > startingrunners ) {
          time_t runstime = BFApi->timeGMTtoUK(time(NULL));
          gmtime_s( &theruns, (const time_t *)&runstime);
          strftime(timetoruns,sizeof(timetoruns),"%H:%M:%S",&theruns);
		      // Get number of runners with prices at start of race
			    // Done like this so if not all prices retrieved in first loop will catch when they are present
			    startingrunners = numruns;
			    cout << "Maximum runners with prices : " << startingrunners << " : found at : " << timetoruns << endl;
		    }
		    // Now have count of runners which have lay prices
		    if ( 1 == numruns ) {
          // Only one runner with lay prices, does it have back prices
          // Looking up the back prices for the runner from prices map
	        IRSelPrice = IRPricesbyselectionId.find(winnerid);
		      if ( IRSelPrice != IRPricesbyselectionId.end()) {
            IRbtlit = IRSelPrice->second.bestPricesToBack.begin();
			      // Is there no back price
		        if ( IRbtlit == IRSelPrice->second.bestPricesToBack.end() ) {
              // Race is over we have a single runner (winner) with a lay price and no back price
              notover = 0;
				      // Log the winner
		          ztime = BFApi->timeGMTtoUK(time(NULL));
				      racefinished = ztime;
		          gmtime_s( &thestart, (const time_t *)&ztime);
		          strftime(nbuff,sizeof(nbuff),"%H:%M:%S",&thestart);
			        etime = BFApi->timeGMTtoUK(time(NULL));
				      int durt = (int)(etime - stime);
				      sprintf_s(gbuff, sizeof(gbuff), "%d", durt);
				      winner = nambuff;
				      cout << " Winner of : " << TheRace->country << " " << TheRace->venue << " " << tbuff << " " << TheRace->name << " Is : " << nambuff << " : Time : " << nbuff << " : Duration : " << gbuff << endl;
              // Increment the bot saw result counter in Configuration database
			        int dbotsaw = CFreadbotsawresult(thedb);
			        dbotsaw++;
			        CFsetbotsawresult(thedb, dbotsaw);
			        // Set day last updated
              CFsettodayis(thedb);
		        }
			    }
		    }
		    else {
			    if ( numruns > 0 ) {
			      // Check if there are enough runners left to place a bet on the race
			      if ( numruns < minimumrunners ) {
			        if ( numberofrunners < 1 ) {
				        // If bet not already placed
				        if ( betplaced < 1 ) {
		              time_t zztime = BFApi->timeGMTtoUK(time(NULL));
		              gmtime_s( &thefew, (const time_t *)&zztime);
		              strftime(timetofewbuff,sizeof(timetofewbuff),"%H:%M:%S",&thefew);
                  // Before bet placed set flag to indicate betting stopped because of too few runners
				          cout << "Only : " << numruns << " runners left that is below the minimum runners value of : " << minimumrunners << " a bet cannot be placed current time : " << timetofewbuff << endl;
				          numberofrunners = 1;
				        }
				      }
			      }
 			      // Check if there are too many fallers as a percentage of the starters
			      if ( startingrunners > 0 ) {  // Just to be safe and avoid divide by zero
			        if ( startingrunners >= numruns ) {  // Just to be sure it is sane
			          if ( (((startingrunners - numruns)*100) / startingrunners ) >= fallenlimit ) {
			            if ( numberoffallers < 1 ) {
				            // If bet not already placed
				              if ( betplaced < 1 ) {
		                    time_t zzqtime = BFApi->timeGMTtoUK(time(NULL));
		                    gmtime_s( &thefallen, (const time_t *)&zzqtime);
		                    strftime(timetofallenbuff,sizeof(timetofallenbuff),"%H:%M:%S",&thefew);
                        // Before bet placed set flag to indicate betting stopped because too many fallers
					              int fallers = (((startingrunners - numruns)*100) / (startingrunners) );
					              cout << fallers << "% of the starting : " << startingrunners << " runners have fallen Only : " << numruns << " : runners left. That exceeds the threshold of : " << fallenlimit << "% : a bet cannot be placed current time : " << timetofallenbuff << endl;
				                numberoffallers = 1;
					            }
					          }
				          }
				        }
			        }
              // Check if the race is long enough
              if ( furlongs < minfurlongs ) {
			          if ( numberoffurlongs < 1 ) {
				          // If bet not already placed
				          if ( betplaced < 1 ) {
		                time_t zzytime = BFApi->timeGMTtoUK(time(NULL));
		                gmtime_s( &thedist, (const time_t *)&zzytime);
		                strftime(racetooshortbuff,sizeof(racetooshortbuff),"%H:%M:%S",&thedist);
                    // Before bet placed set flag to indicate race is too short to bet on
					          cout << "Race is only : " << furlongs << " furlongs that is below the minimum distance of : " << minfurlongs << " furlongs a bet cannot be placed current time : " << racetooshortbuff << endl;
				            numberoffurlongs = 1;
				          }
				        }
			        }
			        // More than 1 runner still, check if bet period set
              if ((timetorun > 0) && (starttorun > 0)) {
                // Get current time
			          ztime = BFApi->timeGMTtoUK(time(NULL));
				        // Format times for display purposes
                time_t sruntime = starttorun;
				        struct tm sruntiming = {};
				        gmtime_s( &sruntiming, (const time_t *)&sruntime);
                strftime(startrunbuff,sizeof(startrunbuff),"%H:%M:%S",&sruntiming);
                time_t sztime = ztime;
				        struct tm sztiming = {};
		            gmtime_s( &sztiming, (const time_t *)&sztime);
                strftime(szbuff,sizeof(szbuff),"%H:%M:%S",&sztiming);
                time_t stimetorun = timetorun;
				        struct tm stimetoruntiming = {};
				        gmtime_s( &stimetoruntiming, (const time_t *)&stimetorun);
                strftime(timetorunbuff,sizeof(timetorunbuff),"%H:%M:%S",&stimetoruntiming);
		 		        // Betting period values set
			          if ( 0 == betstopped ) {
                  // Betting not stopped due to time period so check if it needs to be stopped now
				          if ( ( ztime < starttorun ) && ( 0 == stoppedset ) ) {
                    // Before betting period set flag to indicate betting stopped
					          cout << " Not reached bets allowed point. Allowed point : " << startrunbuff << " current time : " << szbuff << endl;
					          stoppedset = 1;
					          if ( 0 == betplaced ) {
                      // Set flag to indicate a bet would be stopped if placed now
                      betstopped = 1;
					          }
				          }
				          if ( ( ztime > timetorun ) && ( 0 == stoppedset ) ) {
                    // Reached end of betting period set flag to indicate betting stopped
				            cout << " Reached no more bets allowed point. Cutoff time : " << timetorunbuff << " current time : " << szbuff << endl;
					          stoppedset = 1;
					          if ( 0 == betplaced ) {
                      // Set flag to indicate a bet would be stopped if placed now
                      betstopped = 1;
					          }
				          }
				        } else {
                  // Betting stopped due to time periods, should that be released now
				          if ( (ztime >= starttorun) && (ztime <= timetorun) ) {
  					        if ( 0 == betsallowedmsg ) {
                      // Only display the message once
				              cout << " In betting allowed period. Allowed from : " << startrunbuff  << " Allowed until : " << timetorunbuff << " Current time : " << szbuff << endl;
  					          betsallowedmsg = 1;
					          }
				            stoppedset = 0;
					          // If no bet attempted to be placed before betting window open then clear betstopped
					          if ( 0 == betplaced ) {
					            betstopped = 0;
					          }
				          }
				        }
			        }
              // Check if bet been placed yet
              if ( 0 == betplaced ) { 
				        // Bet not placed yet so check the lowest lay price found
  			        if ( someprice <= FIXEDLAYODDS ) {
		              gmtime_s( &thestart, (const time_t *)&ztime);
		              strftime(qbuff,sizeof(qbuff),"%H:%M:%S",&thestart);
			            time_t btime = BFApi->timeGMTtoUK(time(NULL));
				          int burt = (int)(btime - stime);
				          sprintf_s(kbuff, sizeof(kbuff), "%d", burt);
				          cout << "Race Duration at bet : " << kbuff << endl;
				          bprice = someprice;
		              beton = betbuff;
				          // Flag a bet placed even if stopped or not got on as is first better than evens
				          // lay favourit opportunity for bet and that is what we want. So will not attempt
				          // to bet again on this race.
                  betplaced = 1;
				          runnerswhenbet = numruns;
				          // Place a bet if not stopped due to time window or insufficent runners left or distance
				          if ( ( 0 == stoppedset ) && ( 0 == numberofrunners ) && (0 == numberoffurlongs && (0 == numberoffallers)) ) {
					          // Only check if less than maximum number of runners as about to place a bet
					          // as number of runners will only decrease if checked to soon and is high
					          // it may stop a bet being placed when ready by which time the number of
					          // runners may have decreased to or below the maximum limit.
					          if ( numruns <= maximumrunners ) {
                      // Get a stake
                      int thestake = SPgetnewstake(thedb, BFApi, waitobj, someprice);
				              if ( thestake > 0 ) {
						            cout << "Stake for bet : " << thestake << " Pence" << endl;
					              double dthestake = ((double)thestake) / 100;
                        dthestake = floor(dthestake * 10000 + 0.5) / 10000;
					              // Although the price is passed it is not used by placealaybet
				                thebetid = placealaybet(thedb, BFApi, someprice, dthestake, betid, TheRace->id);
					              if ( thebetid > 0 ) {
				                  // Will update bets placed table when bet settled
				                  cout << "Bet Placed ID : " << thebetid << endl;
					              } else {
                          // Bet not placed remove unused stake from staking plan table
                          SPremovelaststake(thedb);
				                  cout << "Could Not Place A Bet On Betfair" << endl;
					              }
				              } else {
					              if ( -2 == thestake ) {
						              cout << "SPgetnewstake() returned : test mode" << endl;
					              } else {
                          cout << "SPgetnewstake() returned error code :  " << thestake << endl;
					              }
				              }
                    } else {
                      time_t zzztime = BFApi->timeGMTtoUK(time(NULL));
		                  gmtime_s( &themany, (const time_t *)&zzztime);
		                  strftime(timetomanybuff,sizeof(timetomanybuff),"%H:%M:%S",&themany);
					            cout << "There are : " << numruns << " runners left this is above maximum runners value of : " << maximumrunners << " a bet cannot be placed current time : " << timetomanybuff << endl;
				              numberofrunners = 1;
					          }
				          }
				          // Log the potential lay
				          cout  << " " << TheRace->country << " " << TheRace->venue << " " << tbuff << " " << TheRace->name << " Runner : " << betbuff << " : reached lay odds of : " << someprice << " : at : " << qbuff << " : Currently : " << numruns << " runners" << endl;
				        }
			        }
			      } else {
              // 0 or less runners left ?, not sure what happened here, seems to be the reason why it finishes without a winner mostly though,
              // is this due to uncertain result maybe due to stewards enquiry called ? so have to wait for result from that.
		          notover = 0;
	            cout  << " " << TheRace->country << " " << TheRace->venue << " " << tbuff << " " << TheRace->name << " 0 Or Less Runners Left Detected" << endl;
              // Log winner unknown
		          ztime = BFApi->timeGMTtoUK(time(NULL));
		          gmtime_s( &thestart, (const time_t *)&ztime);
		          strftime(nbuff,sizeof(nbuff),"%H:%M:%S",&thestart);
			        etime = BFApi->timeGMTtoUK(time(NULL));
			        int nurt = (int)(etime - stime);
			        sprintf_s(gbuff, sizeof(gbuff), "%d", nurt);
			        winner = "UNKNOWN 0 RUNNERS";
			      }
		      }
		    } else {
          cout << "Market is no longer active" << endl;
		    }
	    } else {
        cout << "Error retrieving race odds from Betfair" << endl;
	    }
	  } // End of scope to prevent memory leakage
	  if ( (BFApi->timeGMTtoUK(time(NULL)) - stime) > 600 ) {
	    // Timed out after 10 minutes
	    // TODO make this configurable based on race length
      notover = 0;
	    cout  << " " << TheRace->country << " " << TheRace->venue << " " << tbuff << " " << TheRace->name << " Timed Out Winner Not Detected" << endl;
      // Log winner unknown
      ztime = BFApi->timeGMTtoUK(time(NULL));
      gmtime_s( &thestart, (const time_t *)&ztime);
      strftime(nbuff,sizeof(nbuff),"%H:%M:%S",&thestart);
      etime = BFApi->timeGMTtoUK(time(NULL));
	    int gurt = (int)(etime - stime);
	    sprintf_s(gbuff, sizeof(gbuff), "%d", gurt);
	    winner = "UNKNOWN TIMEOUT";
	  }
  } // End of loop reading race prices
  stime = BFApi->timeGMTtoUK(time(NULL));
  gmtime_s( &timing, (const time_t *)&stime);
  strftime(nbuff,sizeof(nbuff),"%H:%M:%S",&timing);
  cout << "Race Ended " << nbuff << endl;
  // Now use the ID of the bet to determine results and update staking plan etc
  // Touch the watchdog file
  touchwatchdogfile("0240", watchdogfile);
  if ( thebetid > 0 ) {
    bfresult = getaraceresult(thedb, BFApi, thebetid, waitobj);
    touchwatchdogfile("0241", watchdogfile);
	  if ( bfresult  >= 0 ) {  // 0 - Bet Lose, 1 - Bet Win
      // getaraceresult() takes care of tidying up staking plan table
      switch ( bfresult ) {
		    case 0:
          cout << "Retrieved results of : " << thebetid << " : LOST" << endl;
		      break;
		    case 1:
		      cout << "Retrieved results of : " << thebetid << " : WON" << endl;
		      break;
		    case 2:
	        cout << "Retrieved results of : " << thebetid << " : NOT Matched/Taken" << endl;
		      break;
	  	  case 3:
	        cout << "Retrieved results of : " << thebetid << " : WON Not All Stake Was Matched" << endl;
		      break;
 	  	  default:
	        cout << "Retrieved results of : " << thebetid << " : Unknown Response" << endl;
	    }
    } else {
      cout << "Error retrieving results of race." << endl;
    }
  } else {
    cout << "No betid value found so no results to check." << endl;
  }
  // Only want results for races with bets placed in .csv file
  std::string wol = "Win";
  if (betstopped > 0) {
    wol = "Win Time Stopped";
  }
  if (losestopped > 0) {
    wol = "Win Loss Stopped";
  }
  if (numberofrunners > 0) {
    wol = "Win Runner Stopped";
  }
  if (numberofrunners > 0) {
    wol = "Win Distance Stopped";
  }
  if (numberoffallers > 0) {
    wol = "Win Fallers Stopped";
  }
  if (betplaced > 0) {
    // Assumption with next test is that these are the know error conditions and that anything
    // else returned would be a runner name.
    // TODO: gather list of runner names and check that what is returned in the winner string
	  // is a runner name as that would then handle any other error strings not seen yet as well.
	  if ( winner != "UNKNOWN TIMEOUT" && winner != "UNKNOWN 0 RUNNERS" ) {
      if (beton == winner) {
		if (betstopped > 0) {
          wol = "Lose Time Stopped";
		} else if ( losestopped > 0 ) {
          wol = "Lose Loss Stopped";
		} else if ( numberofrunners > 0 ) {
          wol = "Lose Runner Stopped";
		} else if ( numberoffurlongs > 0 ) {
          wol = "Lose Distance Stopped";
		} else if ( numberoffallers > 0 ) {
          wol = "Lose Fallers Stopped";
		} else if ( 0 == thebetid ) {
		  if ( testmode > 0 ) {
            wol = "Lose Test Mode only";
		  } else {
		    wol = "Lose But No Bet Placed";
		  }
		} else {
		  if ( testmode > 0 ) {
            wol = "Lose Test Mode only";
		  } else {
            wol = "Lose";
		  }
		}
	  } else {
		if (betstopped > 0) {
          wol = "Win Time Stopped";
		} else if ( losestopped > 0 ) {
          wol = "Win Loss Stopped";
    	  // Whether it was bet on or not it is
		  // now past losing streak.
          SPresetstakes( thedb );
		  // Also clear the stopped flag
		  CFsetlosestopped(thedb, 0);
		} else if ( numberofrunners > 0 ) {
          wol = "Win Runner Stopped";
		} else if ( numberoffurlongs > 0 ) {
          wol = "Win Distance Stopped";
  		} else if ( numberoffallers > 0 ) {
          wol = "Win Fallers Stopped";
		} else if ( 0 == thebetid ) {
		  if ( testmode > 0 ) {
            wol = "Win Test Mode only";
	      } else {
            wol = "Win But No Bet Placed";
	 	  }
		} else {
		  if ( testmode > 0 ) {
            wol = "Win Test Mode only";
		  } else {
            wol = "Win";
            SPresetstakes( thedb );
	 	  }
		    }
	    }
	    if ( 2 == bfresult) {
        wol += " BF Not Matched";
	    }
	  } else {
	    if (betstopped > 0 ) {
        wol = "Unknown Time Stopped";
	    } else if (losestopped > 0 ) {
        wol = "Unknown Loss Stopped";
	    } else if (numberofrunners > 0 ) {
        wol = "Unknown Runner Stopped";
  	  } else if (numberoffurlongs > 0 ) {
        wol = "Unknown Distance Stopped";
  	  } else if (numberoffallers > 0 ) {
        wol = "Unknown Fallers Stopped";
	    } else if ( 0 == thebetid ) {
        wol = "Unknown But No Bet Placed";
	    } else {
        wol = "Unknown";
	    }
      switch ( bfresult ) {
		    case 0:
          wol += " BF LOSE";
		      break;
		    case 1:
          wol += " BF WIN";
		      break;
		    case 2:
		      wol += " BF Not Matched";
		      break;
		    case 3:
          wol += " BF Partial WIN";
		      break;
		    default:
 		      wol += " BF Unknown Response";
	    }
      // Increment the betfair gave result counter in Configuration database
	    int dbfgave = CFreadbfgaveresult(thedb);
      dbfgave++;
	    CFsetbfgaveresult(thedb, dbfgave);
	    // Set day last updated
      CFsettodayis(thedb);
	  }
  }
  time_t ptime = BFApi->timeGMTtoUK(time(NULL));
  gmtime_s( &timing, (const time_t *)&ptime);
  strftime(pbuff,sizeof(pbuff),"%Y/%m/%d",&timing);
  // Get the target for current set or next set if not in a set from Database
  int tsteps = 0;
  int tcumreward = 0;
  int tctarget = 0;
  SPinstakingset( thedb, BFApi, waitobj, &tsteps, &tcumreward, &tctarget);
  // Get the bank balance from Betfair
  vector <AccountFunds> TheFunds;
  int openbbali = 0;
  double openbbald1 = 0.0;
  if ( BFAPI_OK == BFApi->getAccountFunds( TheFunds ) ) { 
    vector<AccountFunds>::iterator fuit = TheFunds.begin();
    openbbali = fuit->balance;
	  // Convert pennies to pounds and pence
    openbbald1 = ((double)openbbali) / 100;
    openbbald1 = floor(openbbald1 * 10000 + 0.5) / 10000;
  }
  // Append to csv file
  std::ofstream outfile;
  outfile.open(raceresults, std::ios_base::app);
  // Date,Country,Venue,Racetime,Racename,furlongs,startedtime,endedtime,duration,avgtime,bettime,durtillbet,peratbet,aperatbet,betodds,bethorse,winhorse,winlose,target,bank,runsatbet
  // peratbet is percentage at bet of actual race time, aperatbet is percentage at bet of average time, runsatbet is number of runners when bet placed, startingrunners number of runners at start of race
  if ( betplaced > 0 ) {
    outfile << pbuff << "," << thebetid << "," << TheRace->country << "," << TheRace->venue << "," << tbuff << "," << TheRace->name << "," << furlongs << "," << sbuff << "," << nbuff << "," << gbuff << "," << avgtime << "," << qbuff << "," << kbuff << ",,," << bprice << "," << beton << "," << winner << ",," << wol << "," << tctarget << "," <<  openbbald1 << "," << runnerswhenbet << "," << startingrunners << "," << startingfav << "," << startingprice << "," << endl;
  } else {
    outfile << pbuff << "," << thebetid << "," << TheRace->country << "," << TheRace->venue << "," << tbuff << "," << TheRace->name << "," << furlongs << "," << sbuff << "," << nbuff << "," << gbuff << "," << avgtime << ",00:00:00,0,,,0.0,NO BET," << winner << ",,NOBET," << tctarget << "," << openbbald1 << "," <<  "," << startingrunners << "," << startingfav << "," << startingprice << "," << endl; 
  }
  outfile.close();
  // Check if a bet was not placed or not attempted to be placed
  if ( 0 == betplaced ) {
    // Increment the odds not found counter in Configuration database
    int doddsnf = CFreadoddsnotfound(thedb);
	  doddsnf++;
	  CFsetoddsnotfound(thedb, doddsnf);
    // Set day last updated
    CFsettodayis(thedb);
  }
  // Prepare race timing database write command
  sqlret = sqlite3_prepare_v2(thedb, insertracetimes, -1, &irtimes, NULL);
  if (sqlret != SQLITE_OK) {
    cout << "Could not prepare " << insertracetimes << " Error Code: " <<  sqlret << endl;
  }
  int aone = 1;
  int minusone = -1;
  time_t durat = 0;
  if ( racefinished > racestarted ) {
    durat = racefinished - racestarted;
  }
  sqlret = sqlite3_bind_text(irtimes,  1, TheRace->country.c_str(), strlen(TheRace->country.c_str()),  SQLITE_STATIC);
  sqlret = sqlite3_bind_text(irtimes,  2, TheRace->venue.c_str(), strlen(TheRace->venue.c_str()),  SQLITE_STATIC);
  sqlret = sqlite3_bind_text(irtimes,  3, namep.c_str(), strlen(namep.c_str()),  SQLITE_STATIC);
  sqlret = sqlite3_bind_text(irtimes,  4, distance.c_str(), strlen(distance.c_str()),  SQLITE_STATIC);
  sqlret = sqlite3_bind_int64(irtimes, 5, TheRace->starttime);
  sqlret = sqlite3_bind_int64(irtimes, 6, racestarted);
  sqlret = sqlite3_bind_int64(irtimes, 7, racefinished);
  sqlret = sqlite3_bind_int64(irtimes, 8, durat);
  if ( racefinished > 0 ) {
    sqlret = sqlite3_bind_int(irtimes, 9, aone);
  } else {
    sqlret = sqlite3_bind_int(irtimes, 9, minusone);
  }
  sqlret = sqlite3_step(irtimes);
  if ( sqlret != SQLITE_DONE ) {
    cout << "Could not step( racetimes ) error code " << sqlret << endl;
  }
  sqlret = sqlite3_reset(irtimes);
  if (sqlret != SQLITE_OK) {
    cout << "Could not reset( racetimes ) Error Code: " <<  sqlret << endl;
	  return 0;
  }
  sqlret = sqlite3_finalize(irtimes);
  if (sqlret != SQLITE_OK) {
    cout << "Could not finalize( racetimes ) Error Code: " <<  sqlret << endl;
	  return 0;
  }
  return 1;
}
//------------------------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------------------------
char *dateord(int dayn)
{
  char *retv = "";
  if ( dayn < 0 || dayn > 31 ) {
    return retv;
  }
  if ( dayn >= 11 && dayn <= 13) {
    return "th";
  }
  switch ( dayn %10 ) {
	case 1: 
  	  retv = "st";
	  break;
	case 2:
  	  retv = "nd";
	  break;
	case 3:
  	  retv = "rd";
	  break;
	default:
	  retv = "th";
  }
  return retv;
}
//------------------------------------------------------------------------------------------
//
// Run as a Thread to hold the timing mutex
//
//------------------------------------------------------------------------------------------
DWORD WINAPI mutexThread( LPVOID lpParam ) 
{
  (void)lpParam; // Silence compiler warning
  extern int nowrunning;
  HANDLE  timMutex = NULL;
  LPCWSTR mname = (LPCWSTR)bftime;
  timMutex = CreateMutex( 
  NULL,              // default security attributes
  TRUE,              // initially set to owned by this thread
  mname);            // named mutex
  if (timMutex == NULL) {
    cout << "CreateMutex error: " <<  GetLastError() << endl;
    return(1);
  }
  // Thread goes to sleep
  while ( nowrunning > 0 ) {
    Sleep(100);
  }
  // Remove the Mutex
  CloseHandle(timMutex);
  return 0; 
} 
//------------------------------------------------------------------------------------------
//
// Take event name and extract first part and check it is a valid distance.
// Give back distance and rest of name.
// Will put the distance into the distance variable and rest into the namepart and
// return true if a distance was found or false if not, if distance not found then
// distance string will be empty and name part contain all the input name given
//
//------------------------------------------------------------------------------------------
bool extractdistance(std::string eventName, std::string& distance, std::string& namepart, int *furlongs)
{ 
  bool retval = false;
  vector<std::string> vdist;
  *furlongs = 0;
  // Tokenise the name string splitting it on the spaces
  vdist.clear();
  std::stringstream dis(eventName.c_str());
  std::string distitem;
  while (std::getline(dis, distitem, ' ')) {
    vdist.push_back(distitem);
  }
  // Ensure got some members in vector
  if ( vdist.begin() != vdist.end() ) {
    // Algorithm for valid distance, this is for GB and IRE races
    // 2 or 4 characters long
    // if 2 long 2nd char is m or f
    // if 4 long 2nd char should m be and 4th char should be f
	if ( 2 == vdist.front().length() ) {
	  if ( 'f' == vdist.front().c_str()[1] || 'm' == vdist.front().c_str()[1] || 
		   'F' == vdist.front().c_str()[1] || 'M' == vdist.front().c_str()[1] ) {
        if ( isdigit(vdist.front().c_str()[0]) ) {
          // Looks like a distance
          retval = true;
		  // Calculate Furlongs
          *furlongs = atoi(&(vdist.front().c_str()[0]));
		  if ( 'm' == vdist.front().c_str()[1] || 'M' == vdist.front().c_str()[1] ) {
              *furlongs = (*furlongs) * 8; 
		  }
        }
      }
	} else if ( 4 == vdist.front().length() ) {
	  if ( ('m' == vdist.front().c_str()[1] || 'M' == vdist.front().c_str()[1]) && 
		   ('f' == vdist.front().c_str()[3] || 'F' == vdist.front().c_str()[3]) ) {
        if ( isdigit(vdist.front().c_str()[0]) && isdigit(vdist.front().c_str()[2]) ) {
          // Looks like a distance
          retval = true;
		  // Calculate Furlongs
          int miles = atoi(&(vdist.front().c_str()[0]));
		  miles = miles * 8;
          *furlongs = atoi(&(vdist.front().c_str()[2]));
          *furlongs += miles;
        }
	  }
    }
  }
  if ( true == retval ) {
    distance = vdist.front();
	vector<std::string>::iterator therest;
	therest = vdist.begin();
	therest++;
	while ( therest != vdist.end() ) {
 	  namepart += therest->c_str();
	  therest++;
	  if ( therest != vdist.end() ) {
	    namepart += " ";
	  }
	}
  }
  if ( false == retval ) {
    distance = eventName;
    namepart = eventName;
  }
  return retval;
}
//------------------------------------------------------------------------------------------
//
// The idea of this is to take the data in the RaceTimes table and produce a table of
// average race times for each distance, so the watchtherace function can stop looking to
// place a bet on a race before a certain percentage of the race has completed and after
// a certain percentage of the race has completed.
//
// Take all the events for a particular distance with a valid time.
// Get the shortest and the longest times.
// Divide the difference between longest and shortest into equal sized blocks (5)
// Set up a number (5) of counters
// Go through the list of race times and increment the counters for the range they fall into.
// See which has the highest count, this is then the range that most races of that distance end in
// If not one clear winner then take bucket for longest race time
// Use the top of bucket range selected as the average time for a race of the distance.
//
//------------------------------------------------------------------------------------------
// Number of buckets to split range into
#define NUMAVGS 5

bool calcavgtimes( sqlite3 *thedb)
{
  time_t shortestrace = 0;
  time_t longestrace = 0;
  time_t averageracetime = 0;
  struct AvgTimes {
	  time_t bottom;
	  time_t top;
	  int count;
  };
  struct AvgTimes averagetimes[NUMAVGS] = {0,0,0};
  struct GetPeakCounts {
	  int bucketnos;
	  int count;
  };
  struct GetPeakCounts getthecounts = {0,0};
  sqlite3_stmt *srtimes1 = NULL;
  const char *selectracetimes1 = "SELECT duration FROM RaceTimes WHERE (distance = ?1) AND (confirmed > 0) ORDER BY duration DESC LIMIT 1;";
  sqlite3_stmt *srtimes2 = NULL;
  const char *selectracetimes2 = "SELECT duration FROM RaceTimes WHERE (distance = ?1) AND (confirmed > 0) ORDER BY duration ASC LIMIT 1;";
  sqlite3_stmt *srtimes3 = NULL;
  const char *selectracetimes3 = "SELECT duration FROM RaceTimes WHERE (distance = ?1) AND (confirmed > 0);";
  sqlite3_stmt *gdistance1 = NULL;
  const char *getdistances1 = "SELECT DISTINCT distance FROM RaceTimes WHERE (confirmed > 0 );";
  sqlite3_stmt *insavgtime1 = NULL;
  const char *insertavgtimes1 = "INSERT INTO AvgRaceTimes (distance, avgtime, nobetafter, nobetbefore) VALUES (?, ?, ?, ?);";
  vector<std::string> alldist;
  std::string thedist;
  // Read unique list of race distances from RaceTimes table
  int sqlret = sqlite3_prepare_v2(thedb, getdistances1, -1, &gdistance1, NULL);
  if (sqlret != SQLITE_OK) {
    cout << "Could not prepare " << getdistances1 << " Error Code: " <<  sqlret << endl;
  }
  sqlret = sqlite3_step(gdistance1);
  // Putting list of distances into vector thedist
  if ( sqlret == SQLITE_ROW ) {
    while ( sqlret == SQLITE_ROW ) {
  	  thedist.assign((const char *)sqlite3_column_text(gdistance1, 0));
	  alldist.push_back(thedist);
      sqlret = sqlite3_step(gdistance1);
      if (sqlret != SQLITE_ROW && sqlret != SQLITE_DONE) {
        cout << "Could not step  ( getdistances1 ) Error Code: " <<  sqlret << endl;
	    return false;
      }
    }
  } else {
    cout << "No race distances found in ( RaceTimes ) Error Code: " <<  sqlret << endl;
  }
  sqlret = sqlite3_reset(gdistance1);
  if (sqlret != SQLITE_OK) {
    cout << "Could not reset( RaceTimes ) Error Code: " <<  sqlret << endl;
	return false;
  }
  sqlret = sqlite3_finalize(gdistance1);
  if (sqlret != SQLITE_OK) {
    cout << "Could not reset( RaceTimes ) Error Code: " <<  sqlret << endl;
	return false;
  }
  // Get the percentage of race run or absolute value in seconds that bets cannot be placed after
  int maxraceper = CFreadnomorebets(thedb);
  // Get the percentage of the race run or absolute value in seconds that bets cannot be placed before
  int dontbetbefore = CFreadnobetbefore(thedb);

  // Prepare selects for shortest and longest distance and all races of distance
  sqlret = sqlite3_prepare_v2(thedb, selectracetimes1, -1, &srtimes1, NULL);
  if (sqlret != SQLITE_OK) {
    cout << "Could not prepare " << selectracetimes1 << " Error Code: " <<  sqlret << endl;
  }
  sqlret = sqlite3_prepare_v2(thedb, selectracetimes2, -1, &srtimes2, NULL);
  if (sqlret != SQLITE_OK) {
    cout << "Could not prepare " << selectracetimes2 << " Error Code: " <<  sqlret << endl;
  }
  sqlret = sqlite3_prepare_v2(thedb, selectracetimes3, -1, &srtimes3, NULL);
  if (sqlret != SQLITE_OK) {
    cout << "Could not prepare " << selectracetimes3 << " Error Code: " <<  sqlret << endl;
  }
  // Prepare insert to store average race distances
  sqlret = sqlite3_prepare_v2(thedb, insertavgtimes1, -1, &insavgtime1, NULL);
  if (sqlret != SQLITE_OK) {
    cout << "Could not prepare " << insertavgtimes1 << " Error Code: " <<  sqlret << endl;
  }
  // For each distance
  for (vector<std::string>::iterator rtit = alldist.begin(); rtit != alldist.end(); rtit++) {
    // Getting shortest distance
	sqlret = sqlite3_bind_text(srtimes1, 1, rtit->c_str(), -1, SQLITE_STATIC);
    if (sqlret != SQLITE_OK) {
	  cout << "Could not bind ( " <<  rtit->c_str() << " ) to srtimes1 Error Code: " << sqlret << endl;
	  return false;
    }
    sqlret = sqlite3_step(srtimes1);
    if ( sqlret == SQLITE_ROW ) {
  	  longestrace = sqlite3_column_int64(srtimes1, 0);
    } else {
      cout << "No longest race distance found in ( RaceTimes ) for srtimes1 Error Code: " <<  sqlret << endl;
    }
    sqlret = sqlite3_reset(srtimes1);
    if (sqlret != SQLITE_OK) {
      cout << "Could not reset( srtimes1 ) Error Code: " <<  sqlret << endl;
	  return false;
    }
    // Getting longest distance
	sqlret = sqlite3_bind_text(srtimes2, 1, rtit->c_str(), -1, SQLITE_STATIC);
    if (sqlret != SQLITE_OK) {
	  cout << "Could not bind ( " <<  rtit->c_str() << " ) to srtimes2 Error Code: " << sqlret << endl;
	  return false;
    }
    sqlret = sqlite3_step(srtimes2);
    if ( sqlret == SQLITE_ROW ) {
  	  shortestrace = sqlite3_column_int64(srtimes2, 0);
    } else {
      cout << "No shortest race distances found in ( RaceTimes ) for srtimes2 Error Code: " <<  sqlret << endl;
    }
    sqlret = sqlite3_reset(srtimes2);
    if (sqlret != SQLITE_OK) {
      cout << "Could not reset( RaceTimes ) Error Code: " <<  sqlret << endl;
	  return false;
    }
    // Rest the buckets and bucket counters
	for ( int o = 0; o < NUMAVGS; o++ ) {
      averagetimes[o].bottom = 0;
	  averagetimes[o].top = 0;
	  averagetimes[o].count = 0;
	}
	getthecounts.bucketnos = 0;
	getthecounts.count = 0;
	// Divide difference into ranges
    time_t lenrng = longestrace - shortestrace;
    int brange = (int)(lenrng / NUMAVGS);
	if (0 != brange) {
	  // Initialise the buckets with there ranges and count 0
	  for ( int x = 0; x < NUMAVGS; x++ ) {
        averagetimes[x].bottom = shortestrace + (x * brange);
	    averagetimes[x].top = (shortestrace + ((x+1) *brange)) -1;
	    averagetimes[x].count = 0;
	  }
	  averagetimes[NUMAVGS - 1].top = longestrace;
      // Now go through the race times and add to the appropriate buckets		
	  sqlret = sqlite3_bind_text(srtimes3, 1, rtit->c_str(), -1, SQLITE_STATIC);
      if (sqlret != SQLITE_OK) {
	    cout << "Could not bind ( " <<  rtit->c_str() << " ) to srtimes3 Error Code: " << sqlret << endl;
	    return false;
      }
      sqlret = sqlite3_step(srtimes3);
	  time_t tgot = 0;
	  // Retrieve times and load buckets
      while ( sqlret == SQLITE_ROW ) {
  	    tgot = sqlite3_column_int64(srtimes3, 0);
	    for ( int j = 0; j < NUMAVGS; j++ ) {
	      if ( (tgot >= averagetimes[j].bottom) && ( tgot <= averagetimes[j].top)) {
            averagetimes[j].count++;
		    break;
		  }
	    }
	    sqlret = sqlite3_step(srtimes3);
      }
      sqlret = sqlite3_reset(srtimes3);
      if (sqlret != SQLITE_OK) {
        cout << "Could not reset( srtimes3 ) Error Code: " <<  sqlret << endl;
	    return false;
      }
      // Find the bucket(s) with the highest count(s)
      // As want longest time if multiple have same start from longest time bucket
      for ( int q = NUMAVGS -1; q > -1; q-- ) {
        if ( averagetimes[q].count > getthecounts.count ) {
          getthecounts.count = averagetimes[q].count;
          getthecounts.bucketnos = q;
	    }
	  }
	  // Get the average time from the bucket for the range with the highest count
	  if ( getthecounts.count > 0 ) {
        // Return the bottom of the bucket range
//		averageracetime = averagetimes[getthecounts.bucketnos].bottom;
        // Return the middle of the bucket range
//	    time_t bucketrange = averagetimes[getthecounts.bucketnos].top - averagetimes[getthecounts.bucketnos].bottom;
//		averageracetime = averagetimes[getthecounts.bucketnos].bottom + (bucketrange / 2);
	    // Return the top of the bucket range, seems to give best results
		averageracetime = averagetimes[getthecounts.bucketnos].top;
	  } else {
	    cout << "No count found in buckets for distance : " << rtit->c_str() << endl;
	  }
	}
	else {
      averageracetime = longestrace;
	}
    // Write average time to database
	sqlret = sqlite3_bind_text(insavgtime1, 1, rtit->c_str(),  strlen(rtit->c_str()),  SQLITE_STATIC);
	if (sqlret != SQLITE_OK) {
	  cout << "Could not bind( " << rtit->c_str() << " ) to insavgtime Error Code: " <<  sqlret << endl;
    }
	sqlret = sqlite3_bind_int64(insavgtime1, 2, averageracetime);
	if (sqlret != SQLITE_OK) {
	  cout << "Could not bind( " << averageracetime << " ) to insavgtime Error Code: " <<  sqlret << endl;
    }
	// Calculate the betting start and stop times
	// Default values
    time_t lastbettime  = 10000;
    time_t firstbettime = 2;
	if ( 0 == CFreadnomorebetsabsolute( thedb ) ) {
	  // Calculate as percentages of average race time for distance
      lastbettime  = (time_t)(averageracetime * maxraceper) / 100;
	  firstbettime = (time_t)(averageracetime * dontbetbefore) / 100;
	} else if ( CFreadnomorebetsabsolute( thedb ) > 0 ) {
      // Take given values as seconds
	  lastbettime  = (time_t)maxraceper;
	  firstbettime = (time_t)dontbetbefore;
	}
	sqlret = sqlite3_bind_int64(insavgtime1, 3, lastbettime);
	if (sqlret != SQLITE_OK) {
	  cout << "Could not bind lastbettime( " << lastbettime << " ) to insavgtime Error Code: " <<  sqlret << endl;
  }
	sqlret = sqlite3_bind_int64(insavgtime1, 4, firstbettime);
	if (sqlret != SQLITE_OK) {
	  cout << "Could not bind firstbettime( " << firstbettime << " ) to insavgtime Error Code: " <<  sqlret << endl;
  }
  sqlret = sqlite3_step(insavgtime1);
  if ( sqlret != SQLITE_DONE ) {
    cout << "Could not insert averagetime for " << rtit->c_str() << " length races Error Code: " <<  sqlret << endl;
  }
    sqlret = sqlite3_reset(insavgtime1);
    if (sqlret != SQLITE_OK) {
      cout << "Could not reset( AvgRaceTimes ) Error Code: " <<  sqlret << endl;
	    return false;
    }
  } // End for each distance
  sqlret = sqlite3_finalize(insavgtime1);
  if (sqlret != SQLITE_OK) {
    cout << "Could not finalize ( AvgRaceTimes ) Error Code: " <<  sqlret << endl;
    return false;
  }
  sqlret = sqlite3_finalize(srtimes1);
  if (sqlret != SQLITE_OK) {
    cout << "Could not finalize srtimes1 for ( RaceTimes ) Error Code: " <<  sqlret << endl;
    return false;
  }
  sqlret = sqlite3_finalize(srtimes2);
  if (sqlret != SQLITE_OK) {
    cout << "Could not finalize srtimes2 for ( RaceTimes ) Error Code: " <<  sqlret << endl;
    return false;
  }
  sqlret = sqlite3_finalize(srtimes3);
  if (sqlret != SQLITE_OK) {
    cout << "Could not finalize srtimes3 for ( RaceTimes ) Error Code: " <<  sqlret << endl;
    return false;
  }
  return true;
}
//
// Place bet and check it was placed OK, do other checking such as matched etc at end.
// Strategy is first runner to go evens or lower, so if does not get matched OK,
// but should not cancel or tinker. By using a fixed price of FIXEDLAYODDS should ensure it
// will get on at slightly better than evens or lower as it has been seen in that
// range before this function was called, and if it cannot then forget it. 
//
// The watchtheraces function will mark the race as betted on so it will not attempt
// to place a bet on the next runner to go evens or better.
//
LONG64 placealaybet(sqlite3 *thedb, BetFairAPIFree_5 *BFApi, double theprice, double thesize, int selectionid, int marketid)
{
  (void)theprice;       // Suppress compiler warning maybe remove parameter later
  LONG64 retval = 0;
  vector<PlaceABet> PlaceTheseBets;
  vector<PlaceABetResp> GetBetReqs;
  vector<GetABetResp> BetDetails;
  // Create the bet
  PlaceABet tmpBet;
  tmpBet.asianLineId = 0;
  tmpBet.betCategoryType = "E";        // Exchange bet
  tmpBet.betPersistenceType = "NONE";  // Bet persistence NONE
  tmpBet.bspLiability = 0.0;           // Need to work around bug in Betfair API, if not present error returned
  tmpBet.betType = "L";                // Lay bet
  tmpBet.marketID = marketid;
  tmpBet.price = FIXEDLAYODDS;         // Place the bet, using a fixed request of FIXEDLAYODDS which is evens or lower
  tmpBet.selectionID = selectionid;
  tmpBet.size = thesize;
  PlaceTheseBets.push_back(tmpBet); 
  // Store results from bet placement
  LONG64 thebetplaced = 0;             // Id of bet placed
  int success = -1;                    // success value for placebet
  int resultcode = -1;                 // result code for placebet
  // Place the bet 
  // Touch the watchdog file
  touchwatchdogfile("0250", watchdogfile);
  if ( BFAPI_OK == BFApi->placeBets(PlaceTheseBets, GetBetReqs) ) { 
    vector<PlaceABetResp>::iterator gbrs = GetBetReqs.begin();
	  if ( gbrs != GetBetReqs.end() ) { // Got a place bet response
	    thebetplaced = gbrs->betId;
	    success = gbrs->success;
	    resultcode = gbrs->resultCode;
	    // Does bet appear to have been placed
	    if ( ( 1 == success ) && ( thebetplaced > 0 ) ) {  // Success and we have a betId
        retval = thebetplaced;
        // Update staking plan record with betid
        SPsetlastbetid(thedb, retval);
	    }
	  }
  }
  return(retval);
}
// 
// Given a betid this will wait to retrieve the result for that betid
// This returns one of 5 values
// -1 - An error occurred
//  0 - Bet lost
//  1 - Bet won
//  2 - Bet not taken
//  3 - Bet won only partially taken
int getaraceresult(sqlite3 *thedb, BetFairAPIFree_5 *BFApi, LONG64 betid, HANDLE waitobj)
{
  static LARGE_INTEGER gpt1;
  static LARGE_INTEGER gpt2;
  static int firstthisrun = 1;
  if ( firstthisrun > 0 ) {
    QueryPerformanceCounter(&gpt1);
    QueryPerformanceCounter(&gpt2);
    gpt1.QuadPart -= (7 * sfrequency.QuadPart);
	  firstthisrun = 0;
  }
  int retval = -1;
  int betsettled = 0;
  vector<GetABetResp> BetDetails;
  vector<CancelbetsResp> CancelBetsResp;
  vector<LONG64> tcancel;
  // Had a scenario where after the race finished this function received a bet matched result from 
  // getABet then a bet lapsed, so it returned no match and placed the same stake on the next race
  // even though this race had actually matched and won. The firstm should hopefully prevent that
  // happening again. Seems to be something to do with partial matched bets and the fact the betFair
  // API getbet call returns 1 status for the bet, according to the documentation that status is U.
  // I saw a Match then a Lapsed so now should only see the Match as any subsequent lapses will be
  // ignored and it will be looking for Settled or Voided at that point. Also removed the cancel for
  // part matched bets, added some extra print output, and refactored the code a bit to make it more
  // readable.
  int firstm = 0;
  // Looping reading the getbet information and checking to see when bet settled and
  // the outcome of the settlement.
  // TODO Some timing
  while ( 0 == betsettled ) {
     // Touch the watchdog file
    touchwatchdogfile("0260", watchdogfile);
  	BetDetails.clear();  // Don't forget to clear it
    // start timer
    QueryPerformanceCounter(&gpt2);
    // Only allowed 12 a minute of this next call
    LONGLONG ElapsedMSCount = gpt2.QuadPart - gpt1.QuadPart;
    LONGLONG MilliMSSeconds = (ElapsedMSCount * 1000) / sfrequency.QuadPart;
    long pa2 = 0;
    if ( MilliMSSeconds < PERMIN60 ) {
      pa2 = (long)(PERMIN60 - MilliMSSeconds);
      DWORD ret2 = 0;
      ret2 = WaitForSingleObject( waitobj, pa2 );
      if ( WAIT_TIMEOUT != ret2 ) {
        cout << "Object wait timeout error : " << ret2 << endl;
      }
    }
    // Touch the watchdog file
    touchwatchdogfile("0270", watchdogfile);
    // Get the bet details 
    if ( BFAPI_OK == BFApi->getABet(betid, BetDetails) ) {
      // Reset the timer
      QueryPerformanceCounter(&gpt1);
      vector<GetABetResp>::iterator gbit = BetDetails.begin();
	    if ( gbit !=  BetDetails.end() ) {
		    switch ( gbit->betStatus ) {
		      case 0: // bet status U (unmatched)
	          // If not already declared a match
			      if ( firstm < 1 ) {
		          // Cancel this bet, may not be necessary
		          tcancel.clear();
              tcancel.push_back(betid);
		          if ( BFAPI_OK != BFApi->cancelBets(tcancel, CancelBetsResp) ) {
			          cout << "getaraceresult() Error cancelling bet : " << betid << endl; 
		          }
   		        // Remove unused stake from plan
              SPremovelaststake(thedb);
   		        // Return bet not used
		          retval = 2;
		          betsettled = 1;
			        // Increment the nomatchedbets counter in Configuration database
			        int dnomatched = CFreadnomatchedbets(thedb);
			        dnomatched++;
			        CFsetnomatchedbets(thedb, dnomatched);
		          // Set day last updated
              CFsettodayis(thedb);
              cout << "getaraceresult() says bet was unmatched" << endl;
			      }
			      break;
		      case 1: // bet status M (matched)
	          // Continue waiting until settled
		        if ( firstm < 1 ) {
		          cout << "getaraceresult() Bet : " << betid << " was Matched, now waiting for settlement" << endl;
			        firstm = 1;
		        }
			      break;
          case 2: // bet status S (settled)
            // Handle settlement
		        if ( gbit->profitAndLoss > 0.0 ) { // Made a profit
		          betsettled = 1;
			        // Get last stake requested and check against stake placed
              double staketoplace = SPreadlaststake( thedb );
			        if ( (staketoplace - gbit->matchedSize) > 0.01 ) {
			          // Win but Stake not completely taken
			          retval = 3;
                // Different ways to handle this hopefully rare situation
				        // 1. Adjust latest entry in staking plan table with values read and continue in set
				        //     until win completely or reset.
				        // 2. Reset staking table and add an entry for this unmatched balance, problem then
				        //     is no longer  n wins and reset but n wins -1 and reset, and target is potentially
				        //     much higher than bank supports.
				        // Going for what seems the lesser of the evils that is number 1.
			          // Adjust the staking plan according to the odds and stake actually settled
			          // Set odds received
			          SPsetoddsreceived( thedb, gbit->avgPrice );
			          // Set the real stake
			          int realstake = (int)(gbit->matchedSize*100+0.5);
                SPsetbetstake( thedb, realstake );
			          double laststake = SPreadlaststake( thedb );
                int expectprofit = (int)(laststake*100+0.5);
                // Set the profit received adjusting cumulative risk at the same time
			          int realprofit = (int)(gbit->profitAndLoss*100+0.5);
                int profitdelta = 0;
				        if ( realprofit >= expectprofit ) {
                  profitdelta = realprofit - expectprofit;
				        } else {
                  profitdelta = expectprofit - realprofit;
				        }
                if ( profitdelta < 5 ) {
                  // Partial stake taken and full profit for that returned
                  SPsetrealprofit( thedb, realprofit );
                  // Increment the wins today counter in Configuration database
		  	          int dwins = CFreadwinstoday(thedb);
			            dwins++;
			            CFsetwinstoday(thedb, dwins);
			            // Set day last updated
                  CFsettodayis(thedb);
                } else {
                  // Partial stake taken and reduced profit for that returned, possibly a dead heat or void
                  // As was a winning bet remove the latest stake from the table
                  SPremovelaststake(thedb);
                  // If that was not the only bet in the series
                  if ( SPanystepsinset(thedb) ) {
                    // TODO

                    // Calculate the odds required for the amount that was won

                    // Adjust the odds and risk, stake etc of the previous bet
                  
                  } else {
                    // Staking plan table empty so reset it
                    SPresetstakes(thedb);
                  }
                  // Increment the draw today counter in Configuration database
			            int ddraw = CFreadracedraw(thedb);
			            ddraw++;
			            CFsetracedraw(thedb, ddraw);
			            // Set day last updated
                  CFsettodayis(thedb);
                }
		   	      } else { // A win for a complete bet match
                // Another scenario that has arisen is, the full bet is taken but then
                // the reward is not what was expected, this can happen on Dead Heats
                // and possibly other occasions.
                // Currently logging real against expected reward to work out the
                // correct expected reward value to use.
 				        // If got approximately expected winnings
                double laststake = SPreadlaststake( thedb );
                int expectreward = (int)(laststake*100+0.5);
			          int realreward = (int)(gbit->profitAndLoss*100+0.5);
				        int rewarddelta = 0;
				        if ( realreward >= expectreward ) {
                  rewarddelta = realreward - expectreward;
				        } else {
                  rewarddelta = expectreward - realreward;
				        }
                if ( rewarddelta < 5 ) {
                  // Full stake taken and full profit returned, the normal case
		              // Return a win
  		            retval = 1;
		              // A win so reset staking plan
                  SPresetstakes( thedb );
			            // Ensure stopped flag is also cleared
			            CFsetlosestopped(thedb, 0);
			            // Increment the wins today counter in Configuration database
			            int dwins = CFreadwinstoday(thedb);
			            dwins++;
			            CFsetwinstoday(thedb, dwins);
			            // Set day last updated
                  CFsettodayis(thedb);
                } else {
                  // Full stake taken and reduced profit returned, possibly a dead heat
                  // As was a winning bet remove the latest stake from the table
                  SPremovelaststake(thedb);
                  // If that was not the only bet in the series
                  if ( SPanystepsinset(thedb) ) {
                    // TODO

                    // Calculate the odds required for the amount that was won

                    // Adjust the odds and risk, stake etc of the previous bet
                  
                  } else {
                    // Staking plan table empty so reset it
                    SPresetstakes(thedb);
                  }
                  // Increment the draw today counter in Configuration database
			            int ddraw = CFreadracedraw(thedb);
			            ddraw++;
			            CFsetracedraw(thedb, ddraw);
			            // Set day last updated
                  CFsettodayis(thedb);
                }
			        }
              cout << "getaraceresult() Bet : " << betid << " made a PROFIT of : " << gbit->profitAndLoss << endl;		          
            } else { // Made a loss
		          betsettled = 1;
			        // Return a lose
  		        retval = 0;
  		        if ( gbit->profitAndLoss >= 0.00 ) { // Made zero loss
			          // As no loss made just remove the last stake from the table
                SPremovelaststake(thedb);
   			        // Increment the draw today counter in Configuration database
			          int ddraw = CFreadracedraw(thedb);
			          ddraw++;
			          CFsetracedraw(thedb, ddraw);
                // Was that the only step in the staking plan
                if ( !SPanystepsinset(thedb) ) {
                  // Staking plan table empty so reset it
                  SPresetstakes(thedb);
                }
			        } else {
			          // Adjust the staking plan according to the odds and stake actually settled
		            SPsetoddsreceived( thedb, gbit->avgPrice );
			          // Convert to positive value for risk calculations and convert to integer
			          int realrisk = (int)((gbit->profitAndLoss *-1)*100+0.5);
			          SPsetbetrisk( thedb, realrisk );
   			        // Increment the loses today counter in Configuration database
			          int dloses = CFreadlosestoday(thedb);
			          dloses++;
			          CFsetlosestoday(thedb, dloses);
                // Mark the bet in the stakes table as settled
                SPsetlastsettled( thedb, 1 );
			        }
			        // Set day last updated
              CFsettodayis(thedb);
              cout << "getaraceresult() Bet : " << betid << " made a LOSS of : " << gbit->profitAndLoss << endl;
		        }
			      break;
		      case 3:  // bet status C (cancelled)
 	          // If not already declared a match
			      if ( firstm < 1 ) {
	            // Remove unused stake from plan
              SPremovelaststake(thedb);
  	          // Return bet not used
		          retval = 2;
		          betsettled = 1;
		          cout << "getaraceresult() says bet : " << betid << " was cancelled" << endl;
			      }
			      break;
		      case 4: // bet status V (voided)
            // Cancel this bet, may not be necessary
            tcancel.clear();
            tcancel.push_back(betid);
		        if ( BFAPI_OK != BFApi->cancelBets(tcancel, CancelBetsResp) ) {
			        cout << "getaraceresult() Error cancelling bet : " << betid << endl; 
		        }
		        // Remove unused stake from plan
            SPremovelaststake(thedb);
  		      // Return bet not used
		        retval = 2;
		        betsettled = 1;
		        cout << "getaraceresult() says bet was voided" << endl;
			      break;
		      case 5: // bet status L (lapsed)
  	        // If not already declared a match
			      if ( firstm < 1 ) {
		          // Cancel this bet, may not be necessary
		          tcancel.clear();
              tcancel.push_back(betid);
		          if ( BFAPI_OK != BFApi->cancelBets(tcancel, CancelBetsResp) ) {
			          cout << "getaraceresult() Error cancelling bet : " << betid << endl; 
		          }
  		        // Remove unused stake from plan
              SPremovelaststake(thedb);
  		        // Return bet not used
  		        retval = 2;
		          betsettled = 1;
              cout << "getaraceresult() says bet lapsed" << endl;
			      }
			      break;
		      case 6: // bet status MU (matched and unmatched)
 	          // If not already declared a match
			      if ( firstm < 1 ) {
              // Cancel this bet, will cancel unmatched portion
  			      // not sure this is necessary
//		        tcancel.clear();
//            tcancel.push_back(betid);
//		        if ( BFAPI_OK != BFApi->cancelBets(tcancel, CancelBetsResp) ) {
//		          cout << "getaraceresult() Error cancelling bet : " << betid << endl; 
//	          }
  		        cout << "getaraceresult() says bet : " << betid << " was not totally Matched" << endl;
		        } 
			      break;
		      default:
		        cout << "getABet returned Unknown bet status : " << gbit->betStatus << endl;
		    } // End of switch ( gbit->betStatus )
	    } else {
        cout << "getABet returned nothing" << endl;
	    }
	  } else {
      // Reset the timer
      QueryPerformanceCounter(&gpt1);
      cout << "BFApi->getABet(betid, BetDetails) returned Error" << endl;
	  }
  } // End while waiting for bet settled
  return(retval);
}
//
// Find the first and last race for the day from the Database Races Table.
//
// Look for non completed races with day equal to today sorted by starttime
// Return true if found OK and copies strings to buffers passed, false if not found
// and does not touch buffers.
//
bool findfirstlastrace(sqlite3 *thedb, BetFairAPIFree_5 *BFApi, char *firstr, int firstrsz, char *lastr, int lastrsz, int *moreraces)
{
  bool retval = false;
  int sqlret = 0;
  int rloop = 1;
  // Retrieve Race Info sorted by starttime and only where race not completed
  sqlite3_stmt *srstmt = NULL;
  time_t stime = 0;
  char stbuff[1024] = {0};
  char ltbuff[1024] = {0};
  char startrunbuff[20] = {0};
  *moreraces = 0;
  const char *selracesbytime = "SELECT id, starttime, country, venue, name, exchangeId, completed, racestate FROM Races WHERE completed = 0 ORDER BY starttime ASC";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, selracesbytime, -1, &srstmt, NULL)) ) {
    cout << "findfirstlastrace() Could not prepare srstmt Error code : " << sqlret << endl;
	  return(false);
  }
  // Set value of current day
  // get value of today is based on current date
  time_t tnowis = time(NULL);
  struct tm tnowistiming = {};
  gmtime_s( &tnowistiming, (const time_t *)&tnowis);
  int todayis = tnowistiming.tm_mday;
  while ( 1 == rloop ) {
    sqlret = sqlite3_step(srstmt);
    if (sqlret == SQLITE_ROW) {
      // Retrieve starttime
      time_t tracestart = sqlite3_column_int(srstmt, 1);
	    time_t ttime = BFApi->timeGMTtoUK(tracestart);
	    struct tm sruntiming = {};
	    gmtime_s( &sruntiming, (const time_t *)&ttime);
	    // Only for current day
	    if ( sruntiming.tm_mday == todayis ) {
        strftime(startrunbuff,sizeof(startrunbuff),"%H:%M",&sruntiming);
	      sprintf_s(ltbuff, sizeof(ltbuff), "%s:%s %s", sqlite3_column_text(srstmt, 2),sqlite3_column_text(srstmt, 3),startrunbuff);
	      if ( 0 == stime ) {
	        stime = tracestart;
		      strcpy_s( stbuff, sizeof(stbuff), ltbuff);
	      }
        *moreraces = 1;
	    }
  	  retval = true;
    }
    else if (sqlret == SQLITE_DONE) {
      rloop = 0;
	    retval = true;
    }
	  else {
      cout << "findfirstlastrace() Could not read from Races table Error Code: " << sqlret << endl;
      rloop = 0;
	    retval = false;
	  }
  }
  sqlret = sqlite3_reset(srstmt);
  if (sqlret != SQLITE_OK) {
    cout << "findfirstlastrace() Could not reset( srstmt ) Error Code: " << sqlret << endl;
  }
  sqlret = sqlite3_finalize(srstmt);
  if (sqlret != SQLITE_OK) {
    cout << "findfirstlastrace() Could not finalize ( srstmt ) Error Code: " << sqlret << endl;
  }
  if ( (true == retval) && (0 != stime) ) {
    strcpy_s( firstr, firstrsz, stbuff);
	  strcpy_s( lastr,  lastrsz,  ltbuff);
  }
  return(retval);
}
//
// Filter out races that will not qualify due to distance and are to close before a race that does qualify
// Called after races loaded from Site
//
#if 0
bool filterclashingnonqualifingraces(sqlite3 *thedb)
{
  bool retval = false;
  int sqlret = 0;
  int rloop = 1;
  // Retrieve Race Info sorted by starttime and only where race not completed
  sqlite3_stmt *srstmt = NULL;
  time_t stime = 0;
  // List of races by starttime, last race first
  const char *selracesbytime = "SELECT id, starttime, country, venue, name, exchangeId, completed, racestate FROM Races ORDER BY starttime DESC";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, selracesbytime, -1, &srstmt, NULL)) ) {
    cout << "findfirstlastrace() Could not prepare srstmt Error code : " << sqlret << endl;
	  return(false);
  }
  // Store value of race
  time_t tnowis = time(NULL);
  struct tm tnowistiming = {};
  gmtime_s( &tnowistiming, (const time_t *)&tnowis);
  int todayis = tnowistiming.tm_mday;
  while ( 1 == rloop ) {
    sqlret = sqlite3_step(srstmt);
    if (sqlret == SQLITE_ROW) {
      // Retrieve starttime
      time_t tracestart = sqlite3_column_int(srstmt, 1);
	    time_t ttime = BFApi->timeGMTtoUK(tracestart);
	    struct tm sruntiming = {};
	    gmtime_s( &sruntiming, (const time_t *)&ttime);
	    // Only for current day
	    if ( sruntiming.tm_mday == todayis ) {
        strftime(startrunbuff,sizeof(startrunbuff),"%H:%M:%S",&sruntiming);
	      sprintf_s(ltbuff, sizeof(ltbuff), "%s:%s %s", sqlite3_column_text(srstmt, 2),sqlite3_column_text(srstmt, 3),startrunbuff);
	      if ( 0 == stime ) {
	        stime = tracestart;
		      strcpy_s( stbuff, sizeof(stbuff), ltbuff);
	      }
        *moreraces = 1;
	    }
  	  retval = true;
    }
    else if (sqlret == SQLITE_DONE) {
      rloop = 0;
	    retval = true;
    }
	  else {
      cout << "findfirstlastrace() Could not read from Races table Error Code: " << sqlret << endl;
      rloop = 0;
	    retval = false;
	  }
  }
  sqlret = sqlite3_reset(srstmt);
  if (sqlret != SQLITE_OK) {
    cout << "findfirstlastrace() Could not reset( srstmt ) Error Code: " << sqlret << endl;
  }
  sqlret = sqlite3_finalize(srstmt);
  if (sqlret != SQLITE_OK) {
    cout << "findfirstlastrace() Could not finalize ( srstmt ) Error Code: " << sqlret << endl;
  }
  if ( (true == retval) && (0 != stime) ) {
    strcpy_s( firstr, firstrsz, stbuff);
	  strcpy_s( lastr,  lastrsz,  ltbuff);
  }
  return(retval);
}
#endif
//
// Update the watchdog file
//
// This creates a file which is updated every 15 seconds, so another process can check this to see if
// this bot is still running. 
// The bot has been quite reliable but once or twice it has crashed, the causes of those crashes
// were fixed but this will ensure any others can be seen early. Not sure want auto rebooting yet
// as if in the middle of a bet it may not resync properly to the state of betfair, that will
// require some thought and possibly extra work in the bot to check the current state on startup.
//
void touchwatchdogfile(char *tag, char *dogfile)
{
  static time_t latestupdate = 0;
  time_t checktime = time(NULL);
  if ( latestupdate > 0 ) {
    if ( (checktime - latestupdate) >= 15 ) {
      std::ofstream watchfile;
  	  watchfile.open(dogfile, std::ios_base::app);
      watchfile << tag << "," << checktime << endl;
      watchfile.close();
	    latestupdate = checktime;
	  }
  } else {
    std::ofstream watchfile;
    watchfile.open(dogfile, std::ios_base::app);
    watchfile << tag << "," << checktime << endl;
    watchfile.close();
	  latestupdate = checktime;
  }
}

//------------------------------------------------------------------------------------------
//
// End of file
//
//------------------------------------------------------------------------------------------
