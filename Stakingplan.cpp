//
// Staking plan functions
//

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
#include "sqlite3.h"           // SQLite3 API
#include "BetFairAPIFree_5.h"
#include "Configuration.h"
#include "Stakingplan.h"
#include "Sendemail.h"

extern LARGE_INTEGER sfrequency;  // ticks per second

//
// Create StakingPlan table if it does not exist
//
int SPsetupstakingplan( sqlite3 *thedb )
{
  int retval = 1;
  int sqlret = 0;
  // Create races table if necessary
  const char *spcreate1 = "CREATE TABLE IF NOT EXISTS StakingPlan(thebetnos INTEGER PRIMARY KEY, layodds REAL, backodds REAL, stakeneeded INTEGER, riskthisbet INTEGER, cumulativerisk INTEGER, rewardthisbet INTEGER, target INTEGER, openingbal INTEGER, betid INTEGER, settled INTEGER);";
  sqlret = sqlite3_exec(thedb,spcreate1,0,0,0);
  if ( sqlret != SQLITE_OK ) {
    cout << "SPsetupstakingplan() Failed to create database table StakingPlan : " << sqlret << endl;
	retval = 0;
  }
  return retval;
}

//
// Called when new stake required, adds new row to StakingPlan table, calculates values and returns stake
// returns 0 if error occurs, returns -1 if no bet possible due to insufficient funds or bets still outstanding.
// This checks the account balances to ensure there is no outstanding bets and that there is sufficient
// balance to make and possibly lose this bet without reducing the bank to less than half what it was
// when the set started.
// If a new run is started with a new target, this will complete any existing set at that sets original
// target before using the new target when it starts a new set.
// 
// TODO: What if know the result of the previous race because it completed cleanly,
//  but the next race has started before Betfair have updated the results from it into the
//  account. If checking account is clear before starting new bet could potentially miss
//  bets for races that start close together.
//  Race starts - Race finishes I win - staking plan reset
//  Race starts, as it is new set I know I can continue - 
//
//  Race starts - race finishes I lose - staking plan continues
//  Race starts, how do I know that I know the result from previous race.
//    Maybe add another column to database to store fact that race result known
//    then if Betfair returns bets outstanding I can say OK I know that.
//    This could start getting messy, as relying on nothing else using account
//    at same time, OK can do that, but want to be sure the bets outstanding
//    are what we think, so need to store the bet id's and what if multiple
//    bets to get a stake on.
//  For simplicity at the moment go with the no bets outstanding and only
//  bot doing account betting. Have it email and or log it if something occurs
//  along these lines, so I can see how much of an issue it really is.
//
// returns > 0 - A stake, 0 - no stake available, -1 - error occured, -2 - test mode
int SPgetnewstake( sqlite3 *thedb, BetFairAPIFree_5 *BFApi, HANDLE waitobj, double thisbet  )
{
  extern LARGE_INTEGER sfrequency; // ticks per second
  static LARGE_INTEGER gpt1;
  static LARGE_INTEGER gpt2;
  static int firstthisrun = 1;
  if ( firstthisrun > 0 ) {
    QueryPerformanceCounter(&gpt1);
    QueryPerformanceCounter(&gpt2);
    gpt1.QuadPart -= (7 * sfrequency.QuadPart);
	  firstthisrun = 0;
  }
  int stakeneeded = -1;
  int newset = 1;
  int openbbal = 0;
  int sqlret = 0;
  std::ofstream outfile;
 
  // Check if running in test mode
  int testmode = CFreadtestmode( thedb );
  if ( testmode > 0 ) {
    return -2;
  }
  vector <AccountFunds> TheFunds;
  // Select all from Staking Plan Table
  sqlite3_stmt *spstmt = NULL;
  const char *spselect1 = "SELECT thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, spselect1, -1, &spstmt, NULL)) ) {
    cout << "SPgetnewstake() Could not prepare spstmt Error code : " << sqlret << endl;
    return(0);
  }
  //First calculate as though this is first bet in set
  // Calculate target for set from bank size
  int atarget = SPgettargetforset( thedb, BFApi, waitobj );
  if ( atarget <= 0 ) {
    cout << "SPgetnewstake() Could not calculate a target value" << endl;
    return(0);
  }
  // Get maximum of consecutive losses allowed
  int maxconseclosses = CFreadmaxconseclosses(thedb);
  double layodds = thisbet;
  layodds = floor(layodds * 10000 + 0.5) / 10000;
  // Given lay odds calculate back odds
  double backodds = 1.0+(1.0/(layodds-1));
  double rawbackodds = backodds;
  rawbackodds -= 1.0;
  // backodds  = floor(backodds + 0.5);
  backodds = floor(backodds * 10000 + 0.5) / 10000;
  // Calculate stake needed in pence
  stakeneeded = ((atarget*100)+((100-COMMISSION)-1))/(100-COMMISSION);
  // Calculate amount risked on this bet, some rounding errors
  int riskthisbet = (int)((stakeneeded*100)/(rawbackodds*100));
  // Calculate amount risked so far this set of bets
  int cumulativerisk = riskthisbet;
  // Used to calculate stop loss point at, with back odds of FIXEDBACKODDS (lay of FIXEDLAYODDS)
  int calccumulativerisk = riskthisbet;
  int calcriskthisbet = riskthisbet;
  // Calculate reward if this bet wins
  int rewardthisbet = (stakeneeded * (100-COMMISSION)) / 100;
  sqlret = sqlite3_step( spstmt );
  if ( sqlret != SQLITE_ROW && sqlret != SQLITE_DONE ) {
    cout << "SPgetnewstake() Error selecting from StakingPlan table Error code : " << sqlret << endl;
    sqlret = sqlite3_reset(spstmt);
    if (sqlret != SQLITE_OK) {
      cout << "SPgetnewstake() Could not reset( spstmt ) 1 Error Code: " << sqlret << endl;
    }
    sqlret = sqlite3_finalize(spstmt);
    if (sqlret != SQLITE_OK) {
      cout << "SPgetnewstake() Could not finalize( spstmt ) 1 Error Code: " << sqlret << endl;
    }
    return 0;
  }
  LONG64 oldbetid = 0;
  int oldsettled = 0;
  if ( SQLITE_ROW == sqlret ) {
    // Records in staking plan table so this is in a staking set
    newset = 0;
    // Previous bets in set so recalculate based on figures from previous bet in set
    int lastbetwas = sqlite3_column_int(spstmt, 0);
	  // Check if maximum consecutive loses not reached
	  if ( lastbetwas < maxconseclosses ) { 
      // StakingPlan table not empty, so read existing values and calculate new ones based on them
      int tmpcumulativerisk = sqlite3_column_int(spstmt, 5);
      atarget = sqlite3_column_int(spstmt, 7);
	    cout << "Using target value of : " << atarget << " as still in set" << endl;
	    // Get the opening bank balance at the start of the set
	    openbbal = sqlite3_column_int(spstmt, 8);
	    stakeneeded = (((atarget+tmpcumulativerisk)*100)+((100-COMMISSION)-1))/(100-COMMISSION);
      riskthisbet = (int)((stakeneeded*100)/(rawbackodds*100));
	    calcriskthisbet = (int)((stakeneeded*100)/(FIXEDBACKODDS*100));  // lay odds FIXEDLAYODDS
      cumulativerisk = riskthisbet + tmpcumulativerisk;
	    calccumulativerisk = calcriskthisbet + tmpcumulativerisk;
      rewardthisbet = (stakeneeded * (100-COMMISSION)) / 100;
      sqlret = sqlite3_reset(spstmt);
      if (sqlret != SQLITE_OK) {
        cout << "SPgetnewstake() Could not reset( spstmt ) 1 Error Code: " << sqlret << endl;
        return(0);
      }
      sqlret = sqlite3_finalize(spstmt);
      if (sqlret != SQLITE_OK) {
        cout << "SPgetnewstake() Could not finalize( spstmt ) 1 Error Code: " << sqlret << endl;
        return(0);
      }
	  } else {
      // Reached maximum consecutive losses
      sqlret = sqlite3_reset(spstmt);
      if (sqlret != SQLITE_OK) {
        cout << "SPgetnewstake() Could not reset( spstmt ) 1 Error Code: " << sqlret << endl;
        return(0);
      }
      sqlret = sqlite3_finalize(spstmt);
      if (sqlret != SQLITE_OK) {
        cout << "SPgetnewstake() Could not finalize( spstmt ) 1 Error Code: " << sqlret << endl;
        return(0);
      }
	    // Check if want to stop until next win or reset and continue
	    if ( CFreadbreaklosingrun(thedb) > 0 ) { // Want to wait until next win
	      // If flag to indicate stoploss reached not set then just reached so increment stopstoday
        int jreach = CFreadlosestopped(thedb);
	      if ( 0 == jreach ) {
          int stopswas = CFreadstopstoday(thedb);
		      stopswas++;
		      CFsetstopstoday(thedb, stopswas);
	      }
	      // Now set loss stop flag, and return indicating no stake available
   	    CFsetlosestopped(thedb, 1);
        outfile.open("c:\\tools\\betfairbot1.txt", std::ios_base::app);
	      outfile << " Maximum consecutive losses : " << maxconseclosses << " reached for this set. Bets stopped till win found." << endl; 
        outfile.close();
	      cout  << " Maximum consecutive losses : " << maxconseclosses << " reached for this set. Bets stopped till win found." << endl; 
        return(-1);
	  } else { // Want to reset and continue
	    // Reset the staking plan table
      SPresetstakes(thedb);
		  // Increment the stops today counter
		  int stopswas = CFreadstopstoday(thedb);
		  stopswas++;
		  CFsetstopstoday(thedb, stopswas);
      // Email notification of reset occurred
      // TODO
      // Email notification of reset occurred
      time_t emailrr = time(NULL);
      std::string msgbody1 = "A staking plan reset has occurred due to run of losers.";
	    msgbody1 += "\r";
      std::string smsbody1 = "BFB1:Reset Due To Run Of Losers";
	    if (!SendEmail(smtpdom, authpass, fromnm, fromdom, tonam, todom, "Betfairbot1 Staking Plan Reset Due To Run Of Losers", msgbody1.c_str(), emailrr, smsbody1.c_str())) {
        cout << "Failed to send staking plan reset email" << endl;
      }
	  }
	 }
  } else {
    // Must be no previous bets in this set, this is start of set, so just tidy up
    sqlret = sqlite3_reset(spstmt);
    if (sqlret != SQLITE_OK) {
      cout << "SPgetnewstake() Could not reset( spstmt ) 1 Error Code: " << sqlret << endl;
      return(0);
    }
    sqlret = sqlite3_finalize(spstmt);
    if (sqlret != SQLITE_OK) {
      cout << "SPgetnewstake() Could not finalize( spstmt ) 1 Error Code: " << sqlret << endl;
      return(0);
    }
  }
  // Get the account balance if not already retrieved from Staking plan table, 
  // to check this new stake will not reduce bank to less than approx half
  // of what it was at the start of this set of bets.
  // Also checking there is no oustanding exposure or potential commission liability
  // Start timer
  QueryPerformanceCounter(&gpt2);
  // Only allowed 12 a minute of this next call
  LONGLONG ElapsedMSCount = gpt2.QuadPart - gpt1.QuadPart;
  LONGLONG MilliMSSeconds = (ElapsedMSCount * 1000) / sfrequency.QuadPart;
  long pa2 = 0;
  if ( MilliMSSeconds < PERMIN12 ) {
    pa2 = (long)(PERMIN12 - MilliMSSeconds);
    DWORD ret2 = 0;
    ret2 = WaitForSingleObject( waitobj, pa2 );
    if ( WAIT_TIMEOUT != ret2 ) {
      cout << "SPgetnewstake() Object wait timeout error : " << ret2 << endl;
    }
  }
  if ( BFAPI_OK == BFApi->getAccountFunds( TheFunds ) ) { 
    vector<AccountFunds>::iterator fuit = TheFunds.begin();
    if ( fuit != TheFunds.end() ) {
	    // If no outstanding exposure
      if ( 0 == fuit->exposure ) {
        // No commission potentially due for unsettled markets
	      if ( 0 == fuit->commissionRetain ) {
		      if ( 1 == newset ) {
			      // This is first bet of a set so use the bank balance 
            // Openbbal will equal balance read from Betfair or value from Configuration
			      // whichever is the lower
 		        openbbal = fuit->balance;
			      // Get the maximum bank balance bot should work with from Configuration
			      int confopenbbal = CFreadopeningbank(thedb);
			      // Check both version of tha bank balance have values
			      if ( openbbal > 0 ) {
			        if ( confopenbbal > 0 ) {
                // If openbbal is greater than that set in config set it to use values set in config
			          if ( openbbal > confopenbbal ) {
                  openbbal = confopenbbal;
				        }
			        }
			      }
		      }
		    } else {
          outfile.open("c:\\tools\\betfairbot1.txt", std::ios_base::app);
          outfile << " Potential Commission liability still showing, no stake given." << endl; 
          outfile.close();
		      cout << " Potential Commission liability still showing, no stake given." << endl; 
	        // Reset the timer
          QueryPerformanceCounter(&gpt1);
		      return(-1);
		    }
	    } else {
        outfile.open("c:\\tools\\betfairbot1.txt", std::ios_base::app);
        outfile << " Exposure still showing, no stake given." << endl; 
        outfile.close();
		    cout << " Exposure still showing, no stake given." << endl;
	      // Reset the timer
        QueryPerformanceCounter(&gpt1);
	      return(-1);
	    }
    }
  }
  // Reset the timer
  QueryPerformanceCounter(&gpt1);
  // Now have balance at start of set (openbbal)
  // A sanity check: Is the balances greater than zero
  if ( openbbal <= 0 ) {
    outfile.open("c:\\tools\\betfairbot1.txt", std::ios_base::app);
	  outfile << " Open Balance : " << openbbal << " Opening balance is Zero or negative, no stake given." << endl; 
    outfile.close();
	  cout << " Open Balance : " << openbbal << " Opening balance is Zero or negative, no stake given." << endl; 
    return(-1);
  }
  // Check if stop loss flag already set
  if ( 	CFreadlosestopped(thedb) > 0 ) {
    outfile.open("c:\\tools\\betfairbot1.txt", std::ios_base::app);
	  outfile << " Bets stopped till win found." << endl; 
    outfile.close();
	  cout  << " Bets stopped till win found." << endl; 
    return(-1);
  }
  // Check current bet at worst odds, if lost, would not reduce bank below half of value at start of set
  if ( ( calccumulativerisk * 2 ) > openbbal ) {
	  if ( CFreadbreaklosingrun(thedb) > 0 ) { // Want to wait until next win
      // If flag to indicate stoploss reached not set then just reached so increment stopstoday
      int jreach = CFreadlosestopped(thedb);
	    if ( 0 == jreach ) {
        int stopswas = CFreadstopstoday(thedb);
	      stopswas++;
	      CFsetstopstoday(thedb, stopswas);
	    }
      // Set flag to indicate loses stopped point reached
	    CFsetlosestopped(thedb, 1);
	    // Log event in events log and normal log
      outfile.open("c:\\tools\\betfairbot1.txt", std::ios_base::app);
	    outfile << " Cumulative Risk for this bet : " << cumulativerisk << " would reduce opening bank : " << openbbal << " to less than half, now waiting for win." << endl; 
      outfile.close();
	    cout  << " Cumulative Risk for this bet : " << cumulativerisk << " would reduce opening bank : " << openbbal << " to less than half, now waiting for win." << endl; 
      return(-1);
	  } else {
      int stopswas = CFreadstopstoday(thedb);
	    stopswas++;
	    CFsetstopstoday(thedb, stopswas);
	    // Log event in events log and normal log
      outfile.open("c:\\tools\\betfairbot1.txt", std::ios_base::app);
	    outfile << " Cumulative Risk for this bet : " << cumulativerisk << " would reduce opening bank : " << openbbal << " to less than half, resetting stake." << endl; 
      outfile.close();
	    cout  << " Cumulative Risk for this bet : " << cumulativerisk << " would reduce opening bank : " << openbbal << " to less than half, resetting stake." << endl; 
      // Reset the staking plan table
      SPresetstakes(thedb);
      // Email notification of reset occurred
      time_t emailr = time(NULL);
      std::string msgbody = "A staking plan reset has occurred due to bank level.";
	    msgbody += "\r";
      std::string smsbody2 = "BFB1:Reset Due To Bank Level";
      if (!SendEmail(smtpdom, authpass, fromnm, fromdom, tonam, todom, "Betfairbot1 Staking Plan Reset Due To Bank Level", msgbody.c_str(), emailr, smsbody2.c_str())) {
        cout << "Failed to send staking plan reset email" << endl;
      }
      // Calculate target for set from bank size
	    atarget = SPgettargetforset( thedb, BFApi, waitobj );
	    if ( atarget <= 0 ) {
        cout << "Could not calculate a target value" << endl;
		    return(-1);
      }
      layodds = thisbet;
      layodds = floor(layodds * 10000 + 0.5) / 10000;
      // Given lay odds calculate back odds
      backodds = 1.0+(1.0/(layodds-1));
      rawbackodds = backodds;
      rawbackodds -= 1.0;
      // backodds  = floor(backodds + 0.5);
      backodds = floor(backodds * 10000 + 0.5) / 10000;
      // Calculate stake needed in pence
      stakeneeded = ((atarget*100)+((100-COMMISSION)-1))/(100-COMMISSION);
      // Calculate amount risked on this bet, some rounding errors
      riskthisbet = (int)((stakeneeded*100)/(rawbackodds*100));
      // Calculate amount risked so far this set of bets
      cumulativerisk = riskthisbet;
      // Calculate reward if this bet wins
      rewardthisbet = (stakeneeded * (100-COMMISSION)) / 100;
    }
  }
  // Now insert new row into StakingPlan table
  sqlite3_stmt *ipstmt = NULL;
  const char *ins1 = "INSERT INTO StakingPlan (layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, ins1, -1, &ipstmt, NULL)) ) {
    cout << "SPgetnewstake() Could not prepare ipstmt Error code : " << sqlret << endl;
  }
  sqlite3_bind_double(ipstmt, 1, layodds);
  sqlite3_bind_double(ipstmt, 2, backodds);
  sqlite3_bind_int(ipstmt,    3, stakeneeded);
  sqlite3_bind_int(ipstmt,    4, riskthisbet);
  sqlite3_bind_int(ipstmt,    5, cumulativerisk);
  sqlite3_bind_int(ipstmt,    6, rewardthisbet);
  sqlite3_bind_int(ipstmt,    7, atarget);
  sqlite3_bind_int(ipstmt,    8, openbbal);
  sqlite3_bind_int64(ipstmt,  9, oldbetid);
  sqlite3_bind_int(ipstmt,   10, oldsettled);
  sqlret = sqlite3_step(ipstmt);
  if (sqlret != SQLITE_DONE) {
    cout << "SPgetnewstake() Could not step ( ipstmt ) 1 Error Code: " << sqlret << endl;
	stakeneeded = 0;
  }
  sqlret = sqlite3_reset(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPgetnewstake() Could not reset( ipstmt ) 1 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPgetnewstake() Could not finalize( ipstmt ) 1 Error Code: " << sqlret << endl;
    return(0);
  }
  return stakeneeded;
}

//
// When bet is placed set the odds actually received so next stake can be calculated
//
int SPsetoddsreceived( sqlite3 *thedb, double realodds )
{
  // Select all from Staking Plan Table
  int sqlret = 0;
  int retval = 1;
  sqlite3_stmt *spstmt = NULL;
  const char *spselect1 = "SELECT thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, spselect1, -1, &spstmt, NULL)) ) {
    cout << "SPsetoddsreceived() Could not prepare spstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( spstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "SPsetoddsreceived() Error selecting from StakingPlan table Error code : " << sqlret << endl;
    sqlret = sqlite3_reset(spstmt);
    if (sqlret != SQLITE_OK) {
      cout << "SPsetoddsreceived() Could not reset( spstmt ) 3 Error Code: " << sqlret << endl;
    }
    sqlret = sqlite3_finalize(spstmt);
    if (sqlret != SQLITE_OK) {
      cout << "SPsetoddsreceived() Could not finalize( spstmt ) 3 Error Code: " << sqlret << endl;
    }
	  return 0;
  }

  // Get all values from StakingPlan table
  int thebetnos      = sqlite3_column_int(spstmt, 0);
  int stakeneeded    = sqlite3_column_int(spstmt, 3);
  int riskthisbet    = sqlite3_column_int(spstmt, 4);
  int cumulativerisk = sqlite3_column_int(spstmt, 5);
  int rewardthisbet  = sqlite3_column_int(spstmt, 6);
  int target         = sqlite3_column_int(spstmt, 7);
  int openingbal     = sqlite3_column_int(spstmt, 8);
  LONG64 betid       = sqlite3_column_int64(spstmt, 9);
  int settled        = sqlite3_column_int(spstmt, 10);
  double layodds = realodds;
  layodds = floor(layodds * 10000 + 0.5) / 10000;
  // Given lay odds calculate back odds
  double backodds = 1.0+(1.0/(layodds-1));
  double rawbackodds = backodds;
  rawbackodds -= 1.0;
  // backodds  = floor(backodds + 0.5);
  backodds = floor(backodds * 10000 + 0.5) / 10000;
  // Subtract current riskthisbet from cumulative to get previous cumulative
  cumulativerisk -= riskthisbet;
  // Recalculate amount risked on this bet, some rounding errors
  riskthisbet = (int)((stakeneeded*100)/(rawbackodds*100));
  // Recalculate amount risked so far this set of bets
  cumulativerisk = riskthisbet + cumulativerisk;
  sqlret = sqlite3_reset(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetoddsreceived() Could not reset( spstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetoddsreceived() Could not finalize( spstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  // Now update row into StakingPlan table
  sqlite3_stmt *ipstmt = NULL;
  const char *ins1 = "INSERT OR REPLACE INTO StakingPlan (thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, ins1, -1, &ipstmt, NULL)) ) {
    cout << "SPsetoddsreceived() Could not prepare ipstmt Error code : " << sqlret << endl;
  }
  sqlite3_bind_int(ipstmt,    1, thebetnos);
  sqlite3_bind_double(ipstmt, 2, layodds);
  sqlite3_bind_double(ipstmt, 3, backodds);
  sqlite3_bind_int(ipstmt,    4, stakeneeded);
  sqlite3_bind_int(ipstmt,    5, riskthisbet);
  sqlite3_bind_int(ipstmt,    6, cumulativerisk);
  sqlite3_bind_int(ipstmt,    7, rewardthisbet);
  sqlite3_bind_int(ipstmt,    8, target);
  sqlite3_bind_int(ipstmt,    9, openingbal);
  sqlite3_bind_int64(ipstmt,  10, betid);
  sqlite3_bind_int(ipstmt,    11, settled);
  sqlret = sqlite3_step(ipstmt);
  if (sqlret != SQLITE_DONE) {
    cout << "SPsetoddsreceived() Could not step ( ipstmt ) 3 Error Code: " << sqlret << endl;
    retval = 0;
  }
  sqlret = sqlite3_reset(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetoddsreceived() Could not reset( ipstmt ) 3 Error Code: " << sqlret << endl;
    retval = 0;
  }
  sqlret = sqlite3_finalize(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetoddsreceived() Could not finalize( ipstmt ) 3 Error Code: " << sqlret << endl;
    retval = 0;
  }
  return(retval);
}

//
// When bet is settled set the stake actually placed
//
int SPsetbetstake( sqlite3 *thedb, int realstake )
{
  // Select all from Staking Plan Table
  int sqlret = 0;
  sqlite3_stmt *spstmt = NULL;
  const char *spselect1 = "SELECT thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, spselect1, -1, &spstmt, NULL)) ) {
    cout << "SPsetbetstake() Could not prepare spstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( spstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "SPsetbetstake() Error selecting from StakingPlan table Error code : " << sqlret << endl;
	return 0;
  }
  // Get all values from StakingPlan table
  int thebetnos      = sqlite3_column_int(spstmt, 0);
  double layodds     = sqlite3_column_double(spstmt, 1);
  int riskthisbet    = sqlite3_column_int(spstmt, 4);
  int cumulativerisk = sqlite3_column_int(spstmt, 5);
  int rewardthisbet  = sqlite3_column_int(spstmt, 6);
  int target         = sqlite3_column_int(spstmt, 7);
  int openingbal     = sqlite3_column_int(spstmt, 8);
  LONG64 betid       = sqlite3_column_int64(spstmt, 9);
  int settled        = sqlite3_column_int(spstmt, 10);
  // Given lay odds calculate back odds
  double backodds = 1.0+(1.0/(layodds-1));
  double rawbackodds = backodds;
  rawbackodds -= 1.0;
  // backodds  = floor(backodds + 0.5);
  backodds = floor(backodds * 10000 + 0.5) / 10000;
  // Subtract current riskthisbet from cumulative to get previous cumulative
  cumulativerisk -= riskthisbet;
  // Recalculate amount risked on this bet, some rounding errors
  riskthisbet = (int)((realstake*100)/(rawbackodds*100));
  // Recalculate amount risked so far this set of bets
  cumulativerisk = riskthisbet + cumulativerisk;
  // Calculate new reward if this bet wins
  rewardthisbet = (realstake * (100-COMMISSION)) / 100;
  sqlret = sqlite3_reset(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetbetstake() Could not reset( spstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetbetstake() Could not finalize( spstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  // Now update row into StakingPlan table
  sqlite3_stmt *ipstmt = NULL;
  const char *ins1 = "INSERT OR REPLACE INTO StakingPlan (thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, ins1, -1, &ipstmt, NULL)) ) {
    cout << "SPsetbetstake() Could not prepare ipstmt Error code : " << sqlret << endl;
  }
  sqlite3_bind_int(ipstmt,    1,  thebetnos);
  sqlite3_bind_double(ipstmt, 2,  layodds);
  sqlite3_bind_double(ipstmt, 3,  backodds);
  sqlite3_bind_int(ipstmt,    4,  realstake);
  sqlite3_bind_int(ipstmt,    5,  riskthisbet);
  sqlite3_bind_int(ipstmt,    6,  cumulativerisk);
  sqlite3_bind_int(ipstmt,    7,  rewardthisbet);
  sqlite3_bind_int(ipstmt,    8,  target);
  sqlite3_bind_int(ipstmt,    9,  openingbal);
  sqlite3_bind_int64(ipstmt,  10, betid);
  sqlite3_bind_int(ipstmt,    11, settled);
  sqlret = sqlite3_step(ipstmt);
  if (sqlret != SQLITE_DONE) {
    cout << "SPsetbetstake() Could not step ( ipstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetbetstake() Could not reset( ipstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetbetstake() Could not finalize( ipstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  return 1;
}

//
// When bet is settled set the real profit made and adjust cumulative risk to take it into account
// used when win but only part of stake was matched, so can carry remained of profit required
// to the next bets.
//
int SPsetrealprofit( sqlite3 *thedb, int realprofit )
{
  // Select all from Staking Plan Table
  int sqlret = 0;
  sqlite3_stmt *spstmt = NULL;
  const char *spselect1 = "SELECT thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, spselect1, -1, &spstmt, NULL)) ) {
    cout << "SPsetrealprofit() Could not prepare spstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( spstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "SPsetrealprofit() Error selecting from StakingPlan table Error code : " << sqlret << endl;
	return 0;
  }
  // Get all values from StakingPlan table
  int thebetnos      = sqlite3_column_int(spstmt, 0);
  double layodds     = sqlite3_column_double(spstmt, 1);
  double backodds    = sqlite3_column_double(spstmt, 2);
  int thestake       = sqlite3_column_int(spstmt, 3);
  int riskthisbet    = sqlite3_column_int(spstmt, 4);
  int cumulativerisk = sqlite3_column_int(spstmt, 5);
  int target         = sqlite3_column_int(spstmt, 7);
  int openingbal     = sqlite3_column_int(spstmt, 8);
  LONG64 betid       = sqlite3_column_int64(spstmt, 9);
  int settled        = sqlite3_column_int(spstmt, 10);
  // Given lay odds calculate back odds
  // Subtract reward this bet from cumulative risk
  cumulativerisk -= realprofit;
  sqlret = sqlite3_reset(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetrealprofit() Could not reset( spstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetrealprofit() Could not finalize( spstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  // Now update row into StakingPlan table
  sqlite3_stmt *ipstmt = NULL;
  const char *ins1 = "INSERT OR REPLACE INTO StakingPlan (thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, ins1, -1, &ipstmt, NULL)) ) {
    cout << "SPsetrealprofit() Could not prepare ipstmt Error code : " << sqlret << endl;
  }
  sqlite3_bind_int(ipstmt,    1,  thebetnos);
  sqlite3_bind_double(ipstmt, 2,  layodds);
  sqlite3_bind_double(ipstmt, 3,  backodds);
  sqlite3_bind_int(ipstmt,    4,  thestake);
  sqlite3_bind_int(ipstmt,    5,  riskthisbet);
  sqlite3_bind_int(ipstmt,    6,  cumulativerisk);
  sqlite3_bind_int(ipstmt,    7,  realprofit);
  sqlite3_bind_int(ipstmt,    8,  target);
  sqlite3_bind_int(ipstmt,    9,  openingbal);
  sqlite3_bind_int64(ipstmt,  10, betid);
  sqlite3_bind_int(ipstmt,    11, settled);
  sqlret = sqlite3_step(ipstmt);
  if (sqlret != SQLITE_DONE) {
    cout << "SPsetrealprofit() Could not step ( ipstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetrealprofit() Could not reset( ipstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetrealprofit() Could not finalize( ipstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  return 1;
}

//
// Set risk actually realised when race is completed and lost
//
int SPsetbetrisk( sqlite3 *thedb, int realrisk )
{
  // Select all from Staking Plan Table
  int sqlret = 0;
  sqlite3_stmt *spstmt = NULL;
  const char *spselect1 = "SELECT thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, spselect1, -1, &spstmt, NULL)) ) {
    cout << "SPsetbetrisk() Could not prepare spstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( spstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "SPsetbetrisk() Error selecting from StakingPlan table Error code : " << sqlret << endl;
	return 0;
  }
  // Get all values from StakingPlan table
  int thebetnos      = sqlite3_column_int(spstmt, 0);
  double layodds     = sqlite3_column_double(spstmt, 1);
  double backodds    = sqlite3_column_double(spstmt, 2);
  int stakeneeded    = sqlite3_column_int(spstmt, 3);
  int riskthisbet    = sqlite3_column_int(spstmt, 4);
  int cumulativerisk = sqlite3_column_int(spstmt, 5);
  int rewardthisbet  = sqlite3_column_int(spstmt, 6);
  int target         = sqlite3_column_int(spstmt, 7);
  int openingbal     = sqlite3_column_int(spstmt, 8);
  LONG64 betid       = sqlite3_column_int64(spstmt, 9);
  int settled        = sqlite3_column_int(spstmt, 10);
  cumulativerisk -= riskthisbet;
  cumulativerisk += realrisk;
  riskthisbet = realrisk;
  sqlret = sqlite3_reset(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetbetrisk() Could not reset( spstmt ) 2 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetbetrisk() Could not finalize( spstmt ) 2 Error Code: " << sqlret << endl;
    return(0);
  }
  // Now update row into StakingPlan table
  sqlite3_stmt *ipstmt = NULL;
  const char *ins1 = "INSERT OR REPLACE INTO StakingPlan (thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, ins1, -1, &ipstmt, NULL)) ) {
    cout << "SPsetbetrisk() Could not prepare ipstmt Error code : " << sqlret << endl;
  }
  sqlite3_bind_int(ipstmt,    1,  thebetnos);
  sqlite3_bind_double(ipstmt, 2,  layodds);
  sqlite3_bind_double(ipstmt, 3,  backodds);
  sqlite3_bind_int(ipstmt,    4,  stakeneeded);
  sqlite3_bind_int(ipstmt,    5,  riskthisbet);
  sqlite3_bind_int(ipstmt,    6,  cumulativerisk);
  sqlite3_bind_int(ipstmt,    7,  rewardthisbet);
  sqlite3_bind_int(ipstmt,    8,  target);
  sqlite3_bind_int(ipstmt,    9,  openingbal);
  sqlite3_bind_int64(ipstmt,  10, betid);
  sqlite3_bind_int(ipstmt,    11, settled);
  sqlret = sqlite3_step(ipstmt);
  if (sqlret != SQLITE_DONE) {
    cout << "SPsetbetrisk() Could not step ( ipstmt ) 2 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetbetrisk() Could not reset( ipstmt ) 2 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetbetrisk() Could not finalize( ipstmt ) 2 Error Code: " << sqlret << endl;
    return(0);
  }
  return 1;
}

//
// Read amount of last stake asked to placed
//
double SPreadlaststake( sqlite3 *thedb )
{
  // Select all from Staking Plan Table
  int sqlret = 0;
  double thestake = 0.0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT stakeneeded FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "SPreadlaststake() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW && sqlret != SQLITE_DONE ) {
    cout << "SPreadlaststake() Error selecting latest stake from StakingPlan table Error code : " << sqlret << endl;
	return(0.0);
  }
  if ( SQLITE_ROW == sqlret ) {
    // Get the stake needed value from StakingPlan table
    int stakeneeded = sqlite3_column_int(rsstmt, 0);
	thestake = ((double)stakeneeded) / 100;
    thestake = floor(thestake * 10000 + 0.5) / 10000;
  }
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPreadlaststake() Could not reset( spstmt ) 2 Error Code: " << sqlret << endl;
    return(0.0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPreadlaststake() Could not finalize( spstmt ) 2 Error Code: " << sqlret << endl;
    return(0.0);
  }
  return(thestake);
}

//
// Clears the StakingPlan table ready to start a new set
// This is called when a bet is won to reset the staking plan.
//
int SPresetstakes( sqlite3 *thedb )
{
  // Drop the staking plan table and recreate it
  int sqlret = 0;
  const char *dropit = "DROP TABLE IF EXISTS StakingPlan;";
  sqlret = sqlite3_exec(thedb,dropit,0,0,0);
  if ( sqlret != SQLITE_OK ) {
    cout << "SPresetstakes() Failed to drop database table StakingPlan error code : " << sqlret << endl;
    return (0);
  }
  // Call setup staking plan function to recreate table
  return (SPsetupstakingplan( thedb ));
}

//
// Removes the latest row in the Staking database table, effectivly removing the
// last stake given. Used when stake given and not used, so can reset that to give
// the same stake again next time.
//
int SPremovelaststake( sqlite3 *thedb )
{
  int sqlret = 0;
  int lastrow = 0;
  std::ofstream outfile;
  vector <AccountFunds> TheFunds;
  // Select latest row in table to get betnos value
  sqlite3_stmt *cntbet1 = NULL;
  const char *countbet = "SELECT thebetnos FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, countbet, -1, &cntbet1, NULL)) ) {
    cout << "SPremovelaststake() Could not prepare cntbet1 Error code : " << sqlret << endl;
    return(0);
  }
  // Delete row from staking table
  sqlite3_stmt *delbet1 = NULL;
  const char *spdelete1 = "DELETE FROM StakingPlan WHERE thebetnos = ?";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, spdelete1, -1, &delbet1, NULL)) ) {
    cout << "SPremovelaststake() Could not prepare spdelete1 Error code : " << sqlret << endl;
    return(0);
  }
  // Get betnos of row to delete
  sqlret = sqlite3_step( cntbet1 );
  if ( sqlret != SQLITE_ROW && sqlret != SQLITE_DONE ) {
    cout << "SPremovelaststake() Error selecting from StakingPlan table Error code : " << sqlret << endl;
	return 0;
  }
  if ( SQLITE_ROW == sqlret ) {
    lastrow = sqlite3_column_int(cntbet1, 0);     
    sqlret = sqlite3_reset(cntbet1);
    if (sqlret != SQLITE_OK) {
      cout << "SPremovelaststake() Could not reset( cntbet1 ) 1 Error Code: " << sqlret << endl;
      return(0);
    }
    sqlret = sqlite3_finalize(cntbet1);
    if (sqlret != SQLITE_OK) {
      cout << "SPremovelaststake() Could not finalize( cntbet1 ) 1 Error Code: " << sqlret << endl;
      return(0);
    }
    // Got betnos of latest row, so now delete it
    sqlret = sqlite3_bind_int(delbet1,  1, lastrow);
    if (sqlret != SQLITE_OK) {
      cout << "SPremovelaststake() Could not bind( " << lastrow << " ) to delbet1 Error Code: " <<  sqlret << endl;
      return(0);
    }
    sqlret = sqlite3_step( delbet1 );
    if ( sqlret != SQLITE_DONE ) {
      cout << "SPremovelaststake() Error deleting row from StakingPlan table Error code : " << sqlret << endl;
	  return 0;
    }
    sqlret = sqlite3_reset(delbet1);
    if (sqlret != SQLITE_OK) {
      cout << "SPremovelaststake() Could not reset( delbet1 ) 1 Error Code: " << sqlret << endl;
      return(0);
    }
    sqlret = sqlite3_finalize(delbet1);
    if (sqlret != SQLITE_OK) {
      cout << "SPremovelaststake() Could not finalize( delbet1 ) 1 Error Code: " << sqlret << endl;
      return(0);
    }
  } else {
    sqlret = sqlite3_reset(cntbet1);
    if (sqlret != SQLITE_OK) {
      cout << "SPremovelaststake() Could not reset( cntbet1 ) 1 Error Code: " << sqlret << endl;
      return(0);
    }
    sqlret = sqlite3_finalize(cntbet1);
    if (sqlret != SQLITE_OK) {
      cout << "SPremovelaststake() Could not finalize( cntbet1 ) 1 Error Code: " << sqlret << endl;
      return(0);
    }
  }
  return(1);
}

//
// When staking plan is reset get the staking set target based on current bank level
//
int SPgettargetforset( sqlite3 *thedb, BetFairAPIFree_5 *BFApi, HANDLE waitobj )
{
  extern LARGE_INTEGER sfrequency; // ticks per second
  static LARGE_INTEGER gpt1;
  static LARGE_INTEGER gpt2;
  static int firstthisrun = 1;
  int mintarget = CFreadmintargetsize(thedb);
  int maxtarget = CFreadmaxtargetsize(thedb);
  if ( firstthisrun > 0 ) {
    QueryPerformanceCounter(&gpt1);
    QueryPerformanceCounter(&gpt2);
    gpt1.QuadPart -= (7 * sfrequency.QuadPart);
	firstthisrun = 0;
  }
  int openbbal = 0;
  std::ofstream outfile;
  vector <AccountFunds> TheFunds;
  int retval = 0;
  // Get bank level from Betfair to work out target to use for this bank level
  QueryPerformanceCounter(&gpt2);
  // Only allowed 12 a minute of this next call
  LONGLONG ElapsedMSCount = gpt2.QuadPart - gpt1.QuadPart;
  LONGLONG MilliMSSeconds = (ElapsedMSCount * 1000) / sfrequency.QuadPart;
  long pa2 = 0;
  if ( MilliMSSeconds < PERMIN12 ) {
    pa2 = (long)(PERMIN12 - MilliMSSeconds);
    DWORD ret2 = 0;
    ret2 = WaitForSingleObject( waitobj, pa2 );
    if ( WAIT_TIMEOUT != ret2 ) {
      cout << "SPgettargetforset() Object wait timeout error : " << ret2 << endl;
    }
  }
  if ( BFAPI_OK == BFApi->getAccountFunds( TheFunds ) ) { 
    vector<AccountFunds>::iterator fuit = TheFunds.begin();
    if ( fuit != TheFunds.end() ) {
	  // If no outstanding exposure
      if ( 0 == fuit->exposure ) {
        // No commission potentially due for unsettled markets
	    if ( 0 == fuit->commissionRetain ) {
          // Openbbal will equal balance read from Betfair or value from Configuration
		  // whichever is the lower
 		  openbbal = fuit->balance;
		  // Get the maximum bank balance bot should work with from Configuration
		  int confopenbbal = CFreadopeningbank(thedb);
		  // Check both version of tha bank balance have values
		  if ( openbbal > 0 ) {
		    if ( confopenbbal > 0 ) {
              // If openbbal is greater than that set in config set it to use values set in config
			  if ( openbbal > confopenbbal ) {
                openbbal = confopenbbal;
			  }
			}
		  }
		} else {
          outfile.open("c:\\tools\\betfairbot1.txt", std::ios_base::app);
          outfile << " Potential Commission liability still showing, no target given." << endl; 
          outfile.close();
		  cout << " Potential Commission liability still showing, no target given." << endl; 
	      // Reset the timer
          QueryPerformanceCounter(&gpt1);
		  return(-1);
		}
	  } else {
        outfile.open("c:\\tools\\betfairbot1.txt", std::ios_base::app);
        outfile << " Exposure still showing, no target given." << endl; 
        outfile.close();
		cout << " Exposure still showing, no target given." << endl;
	    // Reset the timer
        QueryPerformanceCounter(&gpt1);
	    return(-1);
	  }
    }
  }
  // Reset the timer
  QueryPerformanceCounter(&gpt1);
  // Get number of losing bets before reset from config
  int maxloop = CFreadmaxconseclosses(thedb);
  // Get stake level for set based on bank level
  struct calcbank {
    double layodds;
    double backodds;
    int stakeneeded;
    int riskthisbet;
    int cumulativerisk;
  };
  calcbank steps[10];
  int foundit = 0;
  // Set atarget to minimum target size and step up until reach maximum value supported by bank level
  int atarget = CFreadmintargetsize(thedb);
  int prevatarget = atarget;
  double rawbackodds = 0.0;
  int j = 0;
  while ( 0 == foundit ) {
    // Fill array for number of maxconseclosses
    for ( j = 0; j < maxloop; j++ ) {
      steps[j].layodds = FIXEDLAYODDS;
      steps[j].layodds = floor(steps[j].layodds * 10000 + 0.5) / 10000;
      // Given lay odds calculate back odds
      steps[j].backodds = 1.0+(1.0/(steps[j].layodds-1));
      rawbackodds = steps[j].backodds;
      rawbackodds -= 1.0;
      steps[j].backodds = floor(steps[j].backodds * 10000 + 0.5) / 10000;
	  // Calculate stake needed in pence
	  if ( 0 == j ) {
        steps[j].stakeneeded = ((atarget*100)+((100-COMMISSION)-1))/(100-COMMISSION);
		// Calculate amount risked on this bet, some rounding errors
        steps[j].riskthisbet = (int)((steps[j].stakeneeded*100)/(rawbackodds*100));
		// First bet of a set so cumulative risk is same as risk this bet
		steps[j].cumulativerisk = steps[j].riskthisbet;
	  } else {
	    int tmpstake = atarget + steps[j-1].cumulativerisk;
        steps[j].stakeneeded = ((tmpstake*100)+((100-COMMISSION)-1))/(100-COMMISSION);
		// Calculate amount risked on this bet, some rounding errors
        steps[j].riskthisbet = (int)((steps[j].stakeneeded*100)/(rawbackodds*100));
		// First bet of a set so cumulative risk is same as risk this bet
		steps[j].cumulativerisk = steps[j].riskthisbet + steps[j-1].cumulativerisk;
	  }
    }
	j--; // Index of last entry used
	// Check the amount required in the bank for this stake
	if ( openbbal > (steps[j].cumulativerisk * 2 )) {
	  // Take copy of value for last target value that create cumulative risk under bank available
	  prevatarget = atarget;
  	  // Increase stake and try again
	  atarget++;
	  if ( atarget >= maxtarget ) {
	    // If maximum target reached then return that
        retval = maxtarget;
		foundit = 1;
	  }
	} else if ( mintarget == atarget  ) { 
      // If insufficient bank balance to properly cover mintarget then return mintarget
	  retval = mintarget;
	  foundit = 1;
	} else {
	  // Found the target to use
      retval = prevatarget;
	  foundit = 1;
	}
  }
  cout << "New Target Calculated, Bank is : " << openbbal << " Calculated Target is : " << retval << endl;
  // Return stake level
  return(retval);
}

//
// Call this to determine if we are already in a staking plan set
// Returns true if in set false if not.
// Also sets variables pointed to by passed pointers to appropriate value
//
bool SPinstakingset( sqlite3 *thedb, BetFairAPIFree_5 *BFApi, HANDLE waitobj, int *steps, int *cumreward, int *ctarget)
{
  bool retval = true;
  // Select all from Staking Plan Table
  int sqlret = 0;
  sqlite3_stmt *spstmt = NULL;
  const char *spselect1 = "SELECT thebetnos,rewardthisbet,target FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, spselect1, -1, &spstmt, NULL)) ) {
    cout << "SPinstakingset() Could not prepare spstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( spstmt );
  if ( sqlret != SQLITE_ROW ) {
    retval = false;
	  *steps = 0;
	  *cumreward = 0;
	  // Get next target to use
    *ctarget = SPgettargetforset( thedb, BFApi, waitobj );
  } else {
    // In a staking plan so get betnos, latest cumulative risk, and target for set
	  *steps =  sqlite3_column_int(spstmt, 0);
	  *cumreward = sqlite3_column_int(spstmt, 1);
	  *ctarget =  sqlite3_column_int(spstmt, 2);
  }
  sqlret = sqlite3_reset(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPinstakingset() Could not reset( spstmt ) 1 Error Code: " << sqlret << endl;
    return( false );
  }
  sqlret = sqlite3_finalize(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPinstakingset() Could not finalize( spstmt ) 1 Error Code: " << sqlret << endl;
    return( false );
  }
  return( retval);
}

//
// Call this to determine if we are already in a staking plan set
// Returns true if in set false if not.
//
bool SPanystepsinset( sqlite3 *thedb)
{
  bool retval = true;
  // Select all from Staking Plan Table
  int sqlret = 0;
  sqlite3_stmt *spstmt = NULL;
  const char *spselect1 = "SELECT thebetnos,rewardthisbet,target FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, spselect1, -1, &spstmt, NULL)) ) {
    cout << "SPanystepsinset() Could not prepare spstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( spstmt );
  if ( sqlret != SQLITE_ROW ) {
    retval = false;
  }
  sqlret = sqlite3_reset(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPanystepsinset() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return( false);
  }
  sqlret = sqlite3_finalize(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPanystepsinset() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(false);
  }
  return( retval);
}

//
// Read exchange id of last bet placed
//
LONG64 SPreadlastbetid( sqlite3 *thedb )
{
  // Select all betid from Staking Plan Table
  int sqlret = 0;
  LONG64 thebid = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT betid FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "SPreadlastbetid() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW && sqlret != SQLITE_DONE ) {
    cout << "SPreadlastbetid() Error selecting latest betid from StakingPlan table Error code : " << sqlret << endl;
	  return(0);
  }
  if ( SQLITE_ROW == sqlret ) {
    // Get the betid
    thebid = sqlite3_column_int64(rsstmt, 0);
  }
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPreadlastbetid() Could not reset( spstmt ) 2 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPreadlastbetid() Could not finalize( spstmt ) 2 Error Code: " << sqlret << endl;
    return(0);
  }
  return(thebid);
}

//
// Read settled status of last bet placed
//
int SPreadlastsettled( sqlite3 *thedb )
{
  // Select all settled from Staking Plan Table
  int sqlret = 0;
  int settled = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT settled FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "SPreadlastsettled() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW && sqlret != SQLITE_DONE ) {
    cout << "SPreadlastsettled() Error selecting latest settled from StakingPlan table Error code : " << sqlret << endl;
	  return(0);
  }
  if ( SQLITE_ROW == sqlret ) {
    // Get settled
    settled = sqlite3_column_int(rsstmt, 0);
  }
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPreadlastsettled() Could not reset( spstmt ) 2 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPreadlastsettled() Could not finalize( spstmt ) 2 Error Code: " << sqlret << endl;
    return(0);
  }
  return(settled);
}

//
// When bet is placed save the exchange betid of the placed bet to be able to check the status later
// this can be used when restarting the bot to check status of bet unsettled when bot stopped.
//
int SPsetlastbetid( sqlite3 *thedb, LONG64 newbetid )
{
  // Select all from Staking Plan Table
  int sqlret = 0;
  int retval = 1;
  sqlite3_stmt *spstmt = NULL;
  const char *spselect1 = "SELECT thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, spselect1, -1, &spstmt, NULL)) ) {
    cout << "SPsetlastbetid() Could not prepare spstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( spstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "SPsetlastbetid() Error selecting from StakingPlan table Error code : " << sqlret << endl;
    sqlret = sqlite3_reset(spstmt);
    if (sqlret != SQLITE_OK) {
      cout << "SPsetlastbetid() Could not reset( spstmt ) 3 Error Code: " << sqlret << endl;
    }
    sqlret = sqlite3_finalize(spstmt);
    if (sqlret != SQLITE_OK) {
      cout << "SPsetlastbetid() Could not finalize( spstmt ) 3 Error Code: " << sqlret << endl;
    }
	  return 0;
  }

// Get all values from StakingPlan table
  int thebetnos      = sqlite3_column_int(spstmt, 0);
  double layodds     = sqlite3_column_double(spstmt, 1);
  double backodds    = sqlite3_column_double(spstmt, 2);
  int stakeneeded    = sqlite3_column_int(spstmt, 3);
  int riskthisbet    = sqlite3_column_int(spstmt, 4);
  int cumulativerisk = sqlite3_column_int(spstmt, 5);
  int rewardthisbet  = sqlite3_column_int(spstmt, 6);
  int target         = sqlite3_column_int(spstmt, 7);
  int openingbal     = sqlite3_column_int(spstmt, 8);
  int settled        = sqlite3_column_int(spstmt, 10);
  sqlret = sqlite3_reset(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetlastbetid() Could not reset( spstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetlastbetid() Could not finalize( spstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  // Now update row into StakingPlan table
  sqlite3_stmt *ipstmt = NULL;
  const char *ins1 = "INSERT OR REPLACE INTO StakingPlan (thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, ins1, -1, &ipstmt, NULL)) ) {
    cout << "SPsetlastbetid() Could not prepare ipstmt Error code : " << sqlret << endl;
  }
  sqlite3_bind_int(ipstmt,    1, thebetnos);
  sqlite3_bind_double(ipstmt, 2, layodds);
  sqlite3_bind_double(ipstmt, 3, backodds);
  sqlite3_bind_int(ipstmt,    4, stakeneeded);
  sqlite3_bind_int(ipstmt,    5, riskthisbet);
  sqlite3_bind_int(ipstmt,    6, cumulativerisk);
  sqlite3_bind_int(ipstmt,    7, rewardthisbet);
  sqlite3_bind_int(ipstmt,    8, target);
  sqlite3_bind_int(ipstmt,    9, openingbal);
  sqlite3_bind_int64(ipstmt,  10, newbetid);
  sqlite3_bind_int(ipstmt,    11, settled);
  sqlret = sqlite3_step(ipstmt);
  if (sqlret != SQLITE_DONE) {
    cout << "SPsetlastbetid() Could not step ( ipstmt ) 3 Error Code: " << sqlret << endl;
    retval = 0;
  }
  sqlret = sqlite3_reset(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetlastbetid() Could not reset( ipstmt ) 3 Error Code: " << sqlret << endl;
    retval = 0;
  }
  sqlret = sqlite3_finalize(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetlastbetid() Could not finalize( ipstmt ) 3 Error Code: " << sqlret << endl;
    retval = 0;
  }
  return(retval);
}

//
// When bet is settled save the status to be able to check the status later
// this can be used when restarting the bot to check status of last when bot stopped.
//
int SPsetlastsettled( sqlite3 *thedb, int newsettled )
{
  // Select all from Staking Plan Table
  int sqlret = 0;
  int retval = 1;
  sqlite3_stmt *spstmt = NULL;
  const char *spselect1 = "SELECT thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled FROM StakingPlan ORDER BY thebetnos DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, spselect1, -1, &spstmt, NULL)) ) {
    cout << "SPsetlastsettled() Could not prepare spstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( spstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "SPsetlastsettled() Error selecting from StakingPlan table Error code : " << sqlret << endl;
    sqlret = sqlite3_reset(spstmt);
    if (sqlret != SQLITE_OK) {
      cout << "SPsetlastsettled() Could not reset( spstmt ) 3 Error Code: " << sqlret << endl;
    }
    sqlret = sqlite3_finalize(spstmt);
    if (sqlret != SQLITE_OK) {
      cout << "SPsetlastsettled() Could not finalize( spstmt ) 3 Error Code: " << sqlret << endl;
    }
	  return 0;
  }

  // Get all values from StakingPlan table
  int thebetnos      = sqlite3_column_int(spstmt, 0);
  double layodds     = sqlite3_column_double(spstmt, 1);
  double backodds    = sqlite3_column_double(spstmt, 2);
  int stakeneeded    = sqlite3_column_int(spstmt, 3);
  int riskthisbet    = sqlite3_column_int(spstmt, 4);
  int cumulativerisk = sqlite3_column_int(spstmt, 5);
  int rewardthisbet  = sqlite3_column_int(spstmt, 6);
  int target         = sqlite3_column_int(spstmt, 7);
  int openingbal     = sqlite3_column_int(spstmt, 8);
  LONG64 betid       = sqlite3_column_int64(spstmt, 9);
  sqlret = sqlite3_reset(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetlastsettled() Could not reset( spstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(spstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetlastsettled() Could not finalize( spstmt ) 3 Error Code: " << sqlret << endl;
    return(0);
  }
  // Now update row into StakingPlan table
  sqlite3_stmt *ipstmt = NULL;
  const char *ins1 = "INSERT OR REPLACE INTO StakingPlan (thebetnos,layodds,backodds,stakeneeded,riskthisbet,cumulativerisk,rewardthisbet,target,openingbal,betid,settled) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, ins1, -1, &ipstmt, NULL)) ) {
    cout << "SPsetlastsettled() Could not prepare ipstmt Error code : " << sqlret << endl;
  }
  sqlite3_bind_int(ipstmt,    1, thebetnos);
  sqlite3_bind_double(ipstmt, 2, layodds);
  sqlite3_bind_double(ipstmt, 3, backodds);
  sqlite3_bind_int(ipstmt,    4, stakeneeded);
  sqlite3_bind_int(ipstmt,    5, riskthisbet);
  sqlite3_bind_int(ipstmt,    6, cumulativerisk);
  sqlite3_bind_int(ipstmt,    7, rewardthisbet);
  sqlite3_bind_int(ipstmt,    8, target);
  sqlite3_bind_int(ipstmt,    9, openingbal);
  sqlite3_bind_int64(ipstmt,  10, betid);
  sqlite3_bind_int(ipstmt,    11, newsettled);
  sqlret = sqlite3_step(ipstmt);
  if (sqlret != SQLITE_DONE) {
    cout << "SPsetlastsettled() Could not step ( ipstmt ) 3 Error Code: " << sqlret << endl;
    retval = 0;
  }
  sqlret = sqlite3_reset(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetlastsettled() Could not reset( ipstmt ) 3 Error Code: " << sqlret << endl;
    retval = 0;
  }
  sqlret = sqlite3_finalize(ipstmt);
  if (sqlret != SQLITE_OK) {
    cout << "SPsetlastsettled() Could not finalize( ipstmt ) 3 Error Code: " << sqlret << endl;
    retval = 0;
  }
  return(retval);
}

//------------------------------------------------------------------------------------------
//
// End of file
//
//------------------------------------------------------------------------------------------