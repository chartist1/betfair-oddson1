//
// Configuration table methods
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

//
// Create the configuration table if it does not exist
// TODO: Populate with default values
//
int CFsetupconfiguration( sqlite3 *thedb )
{
  int retval = 1;
  int sqlret = 0;
  // Create the config table if it does not exist
  // This table is not dropped, the information here is used to setup items and store some state and counter items
  // testmode        - If greater than 1 then the staking plan will always return -2 so no bets will actually be placed
  // minrunners      - The Miniumm number of runners there should be still running in the race when placing a bet
  // maxrunners      - The Maximum number of runners there should be still running in the race when placing a bet
  // bfgaveresult    - *The race result was retrieved from Betfair as the bot could not determine it
  // botsawresult    - *The bot was able to determine the result of the race
  // racestoday      - *Total number of races found today (reset when new day detected, new day detected by comparing todays date against value stored in today variable in this table).
  // racemissed      - *
  // oddsnotfound    - *Number of races that did not reach required odds
  // nomatchedbets   - *How many bets were placed today which were not matched  (reset when new day detected, new day detected by comparing todays date against value stored in today variable in this table).
  // mintargetsize   - Minimum target size for a set, value is worked out from bank size and set to this if less
  // losestoday      - How many losing bets today (reset when new day detected, new day detected by comparing todays date against value stored in today variable in this table).
  // nomorebetsabs   - If > 0 then nobetbefore and nomorebets are values in seconds not percentage of average race time for distance
  // breaklosingrun  - If > 0 indicates if we have a losing run of maxconseclosses size then should stop betting until the next winning 
  //                   bet would have been placed, then restart betting, the idea was to break long losing streaks but sometime causing
  //                   problems as it was in effect throwing away winning bets.
  // stopstoday      - *How many times betting was stopped due to a losing streak reaching maxconseclosses in length    
  // maxconseclosses - Maximum number of consecutive losses allowed before reset staking plan, checked before halving bank rule
  //                   so halving bank is a backup up safety stop. This consecutive losses rule was introduced as halving bank
  //                   sometimes happened twice in quick succession and rapidly depleted the bank to a level where it would take a long time to recover.
  // nobetbefore     - Do not bet before before this percentage (as integer) of average race time for distance has run
  // primkey         -
  // todayis         - *Store todays date (just day of month will do, only want to determine if day has changed). When set to new day reset winstoday & stopstoday counters below.
  // winstoday       - *How many winning sets today (reset when new day detected, new day detected by comparing todays date against value stored in today variable in this table).
  // losestopped     - *Flag 1 indicates stopped staking because of losing streak, 0 not stopped staking because of losing streak.
  // nomorebets      - Do not bet after this percentage (as integer) of average race time for distance has run
  // openingbank     - figure to use for opening bank, can be compared against betfair account to check is available
  // maxtargetsize   - Maximum target size for a set, value is worked out from bank size and set to this if greater
  const char *rtcreate8 = "CREATE TABLE IF NOT EXISTS Configuration(primkey INTEGER PRIMARY KEY, todayis INTEGER, winstoday INTEGER, stopstoday INTEGER, losestopped INTEGER, maxconseclosses INTEGER, nomorebets INTEGER, nobetbefore INTEGER, openingbank INTEGER, maxtargetsize INTEGER, mintargetsize INTEGER, nomorebetsabs INTEGER, breaklosingrun INTEGER, losestoday INTEGER, nomatchedbets INTEGER, oddsnotfound INTEGER, racestoday INTEGER, bfgaveresult INTEGER, botsawresult INTEGER, minrunners INTEGER, testmode INTEGER, maxrunners INTEGER);";
  sqlret = sqlite3_exec(thedb,rtcreate8,0,0,0);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetupconfiguration() Failed to create database table Configuration error code : " << sqlret << endl;
    retval = 0;
  }
  return ( retval);
}
//
// Set losestopped flag in Configuration
// Returns 0 - No problems, 1 - Error occured
int CFsetlosestopped( sqlite3 *thedb, int loses )
{
  sqlite3_stmt *rtup20 = NULL;
  const char *rtupdate20 = "UPDATE Configuration SET losestopped = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate20, -1, &rtup20, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetlosestopped() Failed to prepare rtupdate20 database table error code : " << sqlret << endl;
    return(0);
  }
  // Set the value of the losestopped flag in the Configuration table
  sqlite3_bind_int(rtup20,  1, loses);
  sqlret = sqlite3_step(rtup20);
  if (sqlret != SQLITE_DONE) {
    cout << "CFsetlosestopped() Could not step (execute) rtup10 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup20);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetlosestopped() Could not reset( rtup20 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup20);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetlosestopped() Could not finalize( rtup20 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}
//
// Read state of losestopped flag
//
int CFreadlosestopped( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int losestopped = 1;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT losestopped FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadlosestopped() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadlosestopped() Error selecting losestopped from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the losestopped flag value for the Configuration table
  losestopped = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadlosestopped() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadlosestopped() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return losestopped;
}
//
// Read opening bank to use for runs
//
int CFreadopeningbank( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int openingbank = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT openingbank FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadopeningbank() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadopeningbank() Error selecting openingbank from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the openingbank value from Configuration table
  openingbank = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadopeningbank() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadopeningbank() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return openingbank;
}
//
// Read maxtarget size in pennies
//
int CFreadmaxtargetsize( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int maxtargetsize = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT maxtargetsize FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadmaxtargetsize() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadmaxtargetsize() Error selecting maxtargetsize from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the maxtargetsize value from Configuration table
  maxtargetsize = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadmaxtargetsize() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadmaxtargetsize() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(maxtargetsize);
}
//
// Read mintarget size in pennies
//
int CFreadmintargetsize( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int mintargetsize = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT mintargetsize FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadmintargetsize() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadmintargetsize() Error selecting mintargetsize from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the mintargetsize value from Configuration table
  mintargetsize = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadmintargetsize() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadmintargetsize() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(mintargetsize);
}
//
// Read stopstoday count
//
int CFreadstopstoday( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int stopstoday = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT stopstoday FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadstopstoday() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadstopstoday() Error selecting stopstoday from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get number of stops today from the Configuration table
  stopstoday = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadstopstoday() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadstopstoday() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(stopstoday);
}
//
// Set stopstoday count
//
int CFsetstopstoday( sqlite3 *thedb, int stopstoday )
{
  sqlite3_stmt *rtup21 = NULL;
  const char *rtupdate21 = "UPDATE Configuration SET stopstoday = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate21, -1, &rtup21, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetstopstoday() Failed to prepare rtupdate21 database table error code : " << sqlret << endl;
    return(1);
  }
  // Set number of stops today in the Configuration table
  sqlite3_bind_int(rtup21,  1, stopstoday);
  sqlret = sqlite3_step(rtup21);
  if (sqlret != SQLITE_DONE) {
    cout << "CFsetstopstoday() Could not step (execute) rtup21 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetstopstoday() Could not reset( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetstopstoday() Could not finalize( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}
//
// Read winstoday count
//
int CFreadwinstoday( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int winstoday = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT winstoday FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadwinstoday() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadwinstoday() Error selecting winstoday from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get number of wins today from the Configuration table
  winstoday = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadwinstoday() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadwinstoday() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(winstoday);
}
//
// Read bfgaveresult count
//
int CFreadbfgaveresult( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int bfgaveresult = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT bfgaveresult FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadbfgaveresult() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadbfgaveresult() Error selecting winstoday from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get number of bfgaveresult today from the Configuration table
  bfgaveresult = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadbfgaveresult() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadbfgaveresult() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(bfgaveresult);
}

//
// Read botsawresult count
//
int CFreadbotsawresult( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int botsawresult = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT botsawresult FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadbotsawresult() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadbotsawresult() Error selecting winstoday from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get number of botsawresult today from the Configuration table
  botsawresult = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadbotsawresult() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadbotsawresult() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(botsawresult);
}
//
// Set winstoday count
//
int CFsetwinstoday( sqlite3 *thedb, int winstoday )
{
  sqlite3_stmt *rtup21 = NULL;
  const char *rtupdate21 = "UPDATE Configuration SET winstoday = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate21, -1, &rtup21, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetwinstoday() Failed to prepare rtupdate21 database table error code : " << sqlret << endl;
    return(1);
  }
  // Set number of wins today in the Configuration table
  sqlite3_bind_int(rtup21,  1, winstoday);
  sqlret = sqlite3_step(rtup21);
  if (sqlret != SQLITE_DONE) {
    cout << "CFsetwinstoday() Could not step (execute) rtup21 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetwinstoday() Could not reset( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetwinstoday() Could not finalize( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}
//
// Read losestoday count
//
int CFreadlosestoday( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int losestoday = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT losestoday FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFrealosestoday() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadlosestoday() Error selecting losestoday from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get number of lose today from the Configuration table
  losestoday = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadlosestoday() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadlosestoday() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(losestoday);
}


//
// Set losestoday count
//
int CFsetlosestoday( sqlite3 *thedb, int losestoday )
{
  sqlite3_stmt *rtup21 = NULL;
  const char *rtupdate21 = "UPDATE Configuration SET losestoday = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate21, -1, &rtup21, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetlosestoday() Failed to prepare rtupdate21 database table error code : " << sqlret << endl;
    return(1);
  }
  // Set number of loses today in the Configuration table
  sqlite3_bind_int(rtup21,  1, losestoday);
  sqlret = sqlite3_step(rtup21);
  if (sqlret != SQLITE_DONE) {
    cout << "CFsetlosestoday() Could not step (execute) rtup21 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetlosestoday() Could not reset( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetlosestoday() Could not finalize( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}


//
// Read value of nomorebets variable
//
int CFreadnomorebets( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int nomorebets = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT nomorebets FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadnomorebets() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadnomorebets() Error selecting latest nomorebets from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get no bets allowed after this percntage of race has run value from the Configuration table
  nomorebets = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadnomorebets() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadnomorebets() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return nomorebets;
}
//
// Read value of nobetsbefore variable
//
int CFreadnobetbefore( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int nobetbefore = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT nobetbefore FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadnobetbefore() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadnobetbefore() Error selecting latest nobetbefore from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get no bets allowed before this percntage of race has run value from the Configuration table
  nobetbefore = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadnobetbefore() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadnobetbefore() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return nobetbefore;
}
//
// Read value of maxconseclosses variable
//
int CFreadmaxconseclosses( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int maxconseclosses = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT maxconseclosses FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadmaxconseclosses() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadmaxconseclosses() Error selecting latest nobetbefore from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the maximum number of consecutive losses allowed from Configuration table
  maxconseclosses = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadmaxconseclosses() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadmaxconseclosses() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return maxconseclosses;
}
//
// Read value of breaklosingrun variable
//
int CFreadbreaklosingrun( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int breaklosingrun = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT breaklosingrun FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadbreaklosingrun() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadbreaklosingrun() Error selecting latest breaklosingrun from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the break losing run flag from Configuration table
  breaklosingrun = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadbreaklosingrun() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadbreaklosingrun() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return breaklosingrun;
}
//
// Read value of nomorebetsabs variable
//
int CFreadnomorebetsabsolute( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int nomorebetsabs = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT nomorebetsabs FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadnomorebetsabsolute() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadnomorebetsabsolute() Error selecting latest nomorebetsabs from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the nomorebets values are absolute flag from Configuration table
  nomorebetsabs = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadnomorebetsabsolute() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadnomorebetsabsolute() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return nomorebetsabs;
}
//
// Read value of oddsnotfound variable
//
int CFreadoddsnotfound( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int oddsnotfound = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT oddsnotfound FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadoddsnotfound() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadoddsnotfound() Error selecting latest oddsnotfound from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the oddsnotfound value from Configuration table
  oddsnotfound = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadoddsnotfound() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadoddsnotfound() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return oddsnotfound;
}
//
// Read value of nomatchedbets variable
//
int CFreadnomatchedbets( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int nomatchedbets = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT nomatchedbets FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadnomatchedbets() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadnomatchedbets() Error selecting latest nomatchedbets from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the nomatchedbets value from Configuration table
  nomatchedbets = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadnomatchedbets() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadnomatchedbets() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return nomatchedbets;
}
//
// Read value of racemissed variable
//
int CFreadracemissed( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int racemissed = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT racemissed FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadracemissed() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadracemissed() Error selecting latest racemissed from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the racemissed value from Configuration table
  racemissed = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadracemissed() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadracemissed() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return racemissed;
}
//
// Read value of racedraw variable
//
int CFreadracedraw( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int racedraw = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT racedraw FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadracedraw() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadracedraw() Error selecting latest racedraw from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the racedraw value from Configuration table
  racedraw = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadracedraw() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadracedraw() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return racedraw;
}
//
// Read value of racestoday variable
//
int CFreadracestoday( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int racestoday = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT racestoday FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadracestoday() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadracestoday() Error selecting latest racestoday from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the racestoday value from Configuration table
  racestoday = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadracestoday() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadracestoday() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return racestoday;
}
// Set todayis day of the month value, set to current day
// Returns 0 - No problems, 1 - Error occured
int CFsettodayis( sqlite3 *thedb)
{
  sqlite3_stmt *rtup20 = NULL;
  const char *rtupdate20 = "UPDATE Configuration SET todayis = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate20, -1, &rtup20, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsettodayis() Failed to prepare rtupdate20 database table error code : " << sqlret << endl;
    return(0);
  }
  // Set the value of today is based on current date
  time_t nowis = time(NULL);
  struct tm nowistiming = {};
  gmtime_s( &nowistiming, (const time_t *)&nowis);
  sqlite3_bind_int(rtup20,  1, nowistiming.tm_mday);
  sqlret = sqlite3_step(rtup20);
  if (sqlret != SQLITE_DONE) {
    cout << "CFsettodayis() Could not step (execute) rtup10 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup20);
  if (sqlret != SQLITE_OK) {
    cout << "CFsettodayis() Could not reset( rtup20 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup20);
  if (sqlret != SQLITE_OK) {
    cout << "CFsettodayis() Could not finalize( rtup20 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}
//
// Set oddsnotfound count
//
int CFsetoddsnotfound( sqlite3 *thedb, int oddsnotfound )
{
  sqlite3_stmt *rtup21 = NULL;
  const char *rtupdate21 = "UPDATE Configuration SET oddsnotfound = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate21, -1, &rtup21, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetoddsnotfound() Failed to prepare rtupdate21 database table error code : " << sqlret << endl;
    return(1);
  }
  // Set number of oddsnotfound today in the Configuration table
  sqlite3_bind_int(rtup21,  1, oddsnotfound);
  sqlret = sqlite3_step(rtup21);
  if (sqlret != SQLITE_DONE) {
    cout << "CFsetoddsnotfound() Could not step (execute) rtup21 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetoddsnotfound() Could not reset( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetoddsnotfound() Could not finalize( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}
//
// Set nomatchedbets count
//
int CFsetnomatchedbets( sqlite3 *thedb, int nomatchedbets )
{
  sqlite3_stmt *rtup21 = NULL;
  const char *rtupdate21 = "UPDATE Configuration SET nomatchedbets = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate21, -1, &rtup21, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetnomatchedbets() Failed to prepare rtupdate21 database table error code : " << sqlret << endl;
    return(1);
  }
  // Set number of nomatchedbets today in the Configuration table
  sqlite3_bind_int(rtup21,  1, nomatchedbets);
  sqlret = sqlite3_step(rtup21);
  if (sqlret != SQLITE_DONE) {
    cout << "CFsetnomatchedbets() Could not step (execute) rtup21 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetnomatchedbets() Could not reset( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetnomatchedbets() Could not finalize( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}
//
// Set racemissed count
//
int CFsetracemissed( sqlite3 *thedb, int racemissed )
{
  sqlite3_stmt *rtup21 = NULL;
  const char *rtupdate21 = "UPDATE Configuration SET racemissed = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate21, -1, &rtup21, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetracemissed() Failed to prepare rtupdate21 database table error code : " << sqlret << endl;
    return(1);
  }
  // Set number of racemissed today in the Configuration table
  sqlite3_bind_int(rtup21,  1, racemissed);
  sqlret = sqlite3_step(rtup21);
  if (sqlret != SQLITE_DONE) {
    cout << "CFsetracemissed() Could not step (execute) rtup21 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetracemissed() Could not reset( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetracemissed() Could not finalize( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}
//
// Set racedraw count
//
int CFsetracedraw( sqlite3 *thedb, int racedraw )
{
  sqlite3_stmt *rtup21 = NULL;
  const char *rtupdate21 = "UPDATE Configuration SET racedraw = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate21, -1, &rtup21, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetracedraw() Failed to prepare rtupdate21 database table error code : " << sqlret << endl;
    return(1);
  }
  // Set number of racedraw today in the Configuration table
  sqlite3_bind_int(rtup21,  1, racedraw);
  sqlret = sqlite3_step(rtup21);
  if (sqlret != SQLITE_DONE) {
    cout << "CFsetracedraw() Could not step (execute) rtup21 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetracedraw() Could not reset( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetracedraw() Could not finalize( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}
//
// Set racestoday count
//
int CFsetracestoday( sqlite3 *thedb, int racestoday )
{
  sqlite3_stmt *rtup21 = NULL;
  const char *rtupdate21 = "UPDATE Configuration SET racestoday = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate21, -1, &rtup21, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetracestoday() Failed to prepare rtupdate21 database table error code : " << sqlret << endl;
    return(1);
  }
  // Set number of racestoday today in the Configuration table
  sqlite3_bind_int(rtup21,  1, racestoday);
  sqlret = sqlite3_step(rtup21);
  if (sqlret != SQLITE_DONE) {
    cout << "CFsetracestoday() Could not step (execute) rtup21 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetracestoday() Could not reset( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetracestoday() Could not finalize( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}
//
// Set bfgaveresult count
//
int CFsetbfgaveresult( sqlite3 *thedb, int bfgaveresult )
{
  sqlite3_stmt *rtup21 = NULL;
  const char *rtupdate21 = "UPDATE Configuration SET bfgaveresult = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate21, -1, &rtup21, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetbfgaveresult() Failed to prepare rtupdate21 database table error code : " << sqlret << endl;
    return(1);
  }
  // Set number of bfgaveresult today in the Configuration table
  sqlite3_bind_int(rtup21,  1, bfgaveresult);
  sqlret = sqlite3_step(rtup21);
  if (sqlret != SQLITE_DONE) {
    cout << "CFbfgaveresult() Could not step (execute) rtup21 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFbfgaveresult() Could not reset( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFsetbfgaveresult() Could not finalize( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}
//
// Set botsawresult count
//
int CFsetbotsawresult( sqlite3 *thedb, int botsawresult )
{
  sqlite3_stmt *rtup21 = NULL;
  const char *rtupdate21 = "UPDATE Configuration SET botsawresult = ? WHERE (primkey == 1);";
  int sqlret = sqlite3_prepare_v2(thedb, rtupdate21, -1, &rtup21, NULL);
  if ( sqlret != SQLITE_OK ) {
    cout << "CFsetbotsawresult() Failed to prepare rtupdate21 database table error code : " << sqlret << endl;
    return(1);
  }
  // Set number of botsawresult today in the Configuration table
  sqlite3_bind_int(rtup21,  1, botsawresult);
  sqlret = sqlite3_step(rtup21);
  if (sqlret != SQLITE_DONE) {
    cout << "CFbotsawresult() Could not step (execute) rtup21 Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_reset(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFbotsawresult() Could not reset( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rtup21);
  if (sqlret != SQLITE_OK) {
    cout << "CFbotsawresult() Could not finalize( rtup21 ) Error Code: " <<  sqlret << endl;
    return(0);
  }                       
  return(1);
}
//
// Read minrunners value
//
int CFreadminrunners( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int minrunners = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT minrunners FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadminrunners() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadminrunners() Error selecting minrunners from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the minrunners value from Configuration table
  minrunners = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadminrunners() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadminrunners() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(minrunners);
}
//
// Read maxrunners value
//
int CFreadmaxrunners( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int maxrunners = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT maxrunners FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadmaxrunners() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadmaxrunners() Error selecting maxrunners from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the maxrunners value from Configuration table
  maxrunners = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadmaxrunners() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadmaxrunners() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(maxrunners);
}
//
// Read testmode value
//
int CFreadtestmode( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int testmode = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT testmode FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadtestmode() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadtestmode() Error selecting testmode from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the testmode value from Configuration table
  testmode = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadtestmode() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadtestmode() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(testmode);
}

//
// Read minimum furlongs value
//
int CFreadminfurlongs( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int minfurlongs = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT minfurlongs FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadminfurlongs() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadminfurlongs() Error selecting minfurlongs from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the minfurlongs value from Configuration table
  minfurlongs = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadminfurlongs() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadminfurlongs() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(minfurlongs);
}
//
// Read maximum fallen value, this is a percentage represented as number 0 to 100
//
int CFreadmaxfallen( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int maxfallen = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT maxfallen FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFreadmaxfallen() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFreadmaxfallen() Error selecting maxfallen from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get the maxfallen value from Configuration table
  maxfallen = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadmaxfallen() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFreadmaxfallen() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  return(maxfallen);
}

//
// Check if value of todayis is same as current date
// If not then reset winstoday and stopstoday to 0
// Returns 0 - No problems, 1 - Error occured
int CFchecktodayis( sqlite3 *thedb )
{
  // Select from Configuration Table
  int sqlret = 0;
  int todayis = 0;
  sqlite3_stmt *rsstmt = NULL;
  const char *srselect1 = "SELECT todayis FROM Configuration ORDER BY primkey DESC LIMIT 1;";
  if ( SQLITE_OK != ( sqlret = sqlite3_prepare_v2(thedb, srselect1, -1, &rsstmt, NULL)) ) {
    cout << "CFchecktodayis() Could not prepare rsstmt Error code : " << sqlret << endl;
  }
  sqlret = sqlite3_step( rsstmt );
  if ( sqlret != SQLITE_ROW ) {
    cout << "CFchecktodayis() Error selecting todayis from Configuration table Error code : " << sqlret << endl;
	return(0);
  }
  // Get value of todayis field from the Configuration table
  todayis = sqlite3_column_int(rsstmt, 0);
  sqlret = sqlite3_reset(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFchecktodayis() Could not reset( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  sqlret = sqlite3_finalize(rsstmt);
  if (sqlret != SQLITE_OK) {
    cout << "CFchecktodayis() Could not finalize( spstmt ) Error Code: " << sqlret << endl;
    return(0);
  }
  // get value of today is based on current date
  time_t nowis = time(NULL);
  struct tm nowistiming = {};
  gmtime_s( &nowistiming, (const time_t *)&nowis);
  if ( todayis != nowistiming.tm_mday) {
    // If a new day reset all the counters for the days values
    CFsetwinstoday(thedb, 0);
	  CFsetlosestoday(thedb, 0 );
	  CFsetstopstoday(thedb, 0 );
    CFsetoddsnotfound(thedb, 0);
    CFsetnomatchedbets(thedb, 0);
    CFsetracemissed(thedb, 0);
	  CFsetracestoday(thedb, 0);
	  CFsetbfgaveresult(thedb, 0);
	  CFsetbotsawresult(thedb, 0);
  	CFsetracedraw(thedb, 0);
  }
  return(1);
}