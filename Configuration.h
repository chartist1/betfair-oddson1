#ifndef ConfigurationItems_H
#define ConfigurationItems_H

// Configuration table setting functions
int CFsetlosestopped(sqlite3 *thedb, int loses);
int CFsetwinstoday(sqlite3 *thedb, int winstoday);
int CFsetlosestoday(sqlite3 *thedb, int losestoday);
int CFsetstopstoday(sqlite3 *thedb, int stopstoday);
int CFsetoddsnotfound(sqlite3 *thedb, int oddsnotfound);
int CFsetnomatchedbets(sqlite3 *thedb, int nomatchedbets);
int CFsetracemissed(sqlite3 *thedb, int racemissed);
int CFsetracedraw(sqlite3 *thedb, int racemissed);
int CFsetracestoday(sqlite3 *thedb, int racestoday);
int CFsetbfgaveresult(sqlite3 *thedb, int bfgaveresult);
int CFsetbotsawresult(sqlite3 *thedb, int botsawresult);
int CFsettodayis(sqlite3 *thedb);
// Configuration table reading functions
int CFreadmintargetsize(sqlite3 *thedb);
int CFreadlosestoday(sqlite3 *thedb);
int CFreadnomorebetsabsolute(sqlite3 *thedb);
int CFreadbreaklosingrun(sqlite3 *thedb);
int CFreadstopstoday(sqlite3 *thedb);
int CFreadmaxconseclosses(sqlite3 *thedb);
int CFreadnobetbefore(sqlite3 *thedb);
int CFreadwinstoday( sqlite3 *thedb );
int CFreadlosestopped(sqlite3 *thedb);
int CFreadnomorebets(sqlite3 *thedb);
int CFreadopeningbank(sqlite3 *thedb);
int CFreadmaxtargetsize(sqlite3 *thedb);
int CFreadoddsnotfound(sqlite3 *thedb);
int CFreadnomatchedbets(sqlite3 *thedb);
int CFreadracemissed(sqlite3 *thedb);
int CFreadracedraw(sqlite3 *thedb);
int CFreadracestoday(sqlite3 *thedb);
int CFreadbfgaveresult(sqlite3 *thedb);
int CFreadbotsawresult(sqlite3 *thedb);
int CFreadminrunners(sqlite3 *thedb);
int CFreadmaxrunners(sqlite3 *thedb);
int CFreadtestmode(sqlite3 *thedb);
int CFreadminfurlongs(sqlite3 *thedb);
int CFreadmaxfallen(sqlite3 *thedb);
// Configuration table other functions
int CFsetupconfiguration(sqlite3 *thedb);
int CFchecktodayis(sqlite3 *thedb);

#endif