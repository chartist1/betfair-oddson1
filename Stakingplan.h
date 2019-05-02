#ifndef StakingPlan_H
#define StakingPlan_H

// Default values
// Commission rate charged by Betfair a percentage
#define COMMISSION 5
// Odds to look for and use
#define FIXEDBACKODDS 2.0000
#define FIXEDLAYODDS 2.0000

// Staking plan functions
int SPsetupstakingplan(sqlite3 *thedb);
int SPresetstakes(sqlite3 *thedb);
int SPgetnewstake(sqlite3 *thedb, BetFairAPIFree_5 *BFApi, HANDLE waitobj, double thisbet);
int SPremovelaststake(sqlite3 *thedb);
double SPreadlaststake(sqlite3 *thedb);
int SPsetoddsreceived(sqlite3 *thedb, double realodds);
int SPsetbetstake(sqlite3 *thedb, int realstake);
int SPsetbetrisk(sqlite3 *thedb, int realrisk);
int SPsetrealprofit(sqlite3 *thedb, int realprofit);
int SPgettargetforset( sqlite3 *thedb, BetFairAPIFree_5 *BFApi, HANDLE waitobj);
bool SPinstakingset( sqlite3 *thedb, BetFairAPIFree_5 *BFApi, HANDLE waitobj, int *steps, int *cumreward, int *ctarget);
bool SPanystepsinset( sqlite3 *thedb);
LONG64 SPreadlastbetid( sqlite3 *thedb );
int SPsetlastbetid( sqlite3 *thedb, LONG64 newbetid );
int SPreadlastsettled( sqlite3 *thedb );
int SPsetlastsettled( sqlite3 *thedb, int newsettled );

#endif