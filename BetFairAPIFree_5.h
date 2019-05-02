#ifndef BetFairAPIFree_H
#define BetFairAPIFree_H

#include <process.h>
#include <string>
#include <vector>
#include <map>
#include "lrtimer.h"
#include "soapH.h"                        // obtain the generated stub

using namespace std;

// Constants for Betfair Free API timing
// Wait in milliseconds for 70 calls per minute
#define PERMIN70 865
// Wait in milliseconds for 60 calls per minute
#define PERMIN60 1015
// Wait in milliseconds for 12 calls per minute
#define PERMIN12 5020
// Wait in milliseconds for 5 calls per minute
#define PERMIN5 12017
// Wait in milliseconds for 1 calls per minute
#define PERMIN1 60020

// Return values
enum BFAPI_Retvals { BFAPI_OK = 0,               // Success
  	                 BFAPI_NOUSERNAME,           // No BetFair Username set or given
	                 BFAPI_NOPASSWD,             // No BetFair login password set or given
	                 BFAPI_SOAPERROR,            // SOAP library indicated SOAP error occurred
                     BFAPI_NOSESSION,            // BetFair API indicated no session
	                 BFAPI_BFERROR,              // BetFair API returned an error message
                     BFAPI_TIMEOUT,
                     BFAPI_OVERUSE,              // Call use limit in last minute reached
                     BFAPI_BADIPDATA};           // Bad or missing data in call to Betfair Library

enum BFAPI_MktStatus { BFAPI_MKTSTAT_ACTIVE = 0, // Market is open and available for betting
	                   BFAPI_MKTSTAT_CLOSED,     // Market is finalised, bets to be settled
					   BFAPI_MKTSTAT_INACT,      // Market is not yet available for betting
					   BFAPI_MKSTAT_SUSP};       // Market is temporarily closed for betting.
                                                 // Possibly due to pending action such as a
                                                 // goal scored during an in-play match or
                                                 // removing runners from a race.

// Some utility structures which may be used by applications
// Horse racing 
// Structure to build up information about a particular race into
struct RaceData {
  int exchangeId;          // Exchange id
  int id;                  // Event id
  std::string country;     // Country of Race
  std::string venue;       // Race venue
  std::string name;        // Race name
  time_t starttime;        // Race start time
  int completed;           // Race has been run
  int racestate;           // State of race 0 - Unknown, 1 - waiting to start, 2 - running, 3 - completed
                           // Used to create a state machine so can detect race start correctly, not just it is running
};

// Structure to retrieve country and country into for particular event type
struct VenueCtry {
  int id;                  // Event id
  std::string country;     // Country short name
  int eventTypeId;         // Event Type ID
};

// Structure to build up country, venue and corresponding id into
struct VenueData {
  int id;                  // Event id
  std::string country;     // Country of venue
  std::string name;        // Venue name
};

// Structures returned from Betfair API calls
struct CouponLinks {
  int couponId;
  std::string couponName;
};

struct BFEvents {
  int eventId;
  std::string eventName;
  int eventTypeId;
  int menuLevel;
  int orderIndex;
  time_t startTime;
  std::string timezone;
};

struct MarketSummary {
  int eventTypeId;
  int exchangeId;
  int eventParentId;
  int marketId;
  std::string marketName;
  int marketType;
  int marketTypeVariant;
  int menuLevel;
  int orderIndex;
  time_t startTime;
  std::string timeZone;
  std::string venue;
  int betDelay;
  int numberOfWinners;
};

// Raw event type information is retrieved into a structure of this type
struct EventType {
  int id;
  std::string name;
  int exchangeId;
  int nextMarketId;
};

//
struct Events {
  int errorCode;
  vector<BFEvents> eventItems;
  int eventParentId;
  vector<MarketSummary> marketItems;
  vector<CouponLinks> couponLinks;
  std::string minorErrorCode;
};

// Raw information on market runners is retrieved into a structure of this type
struct MarketRunners {
  int asianLineId;
  double handicap;
  std::string name;
  int selectionId;
};

// All static information about a particular race is built up in a structure of this type
struct MarketData {
  int errorCode;
  std::string countryISO3;
  bool discountAllowed;
  int eventTypeId;                     // Not currently used
  LONG64 lastRefresh;
  int licenceId;                       // 1:UK 2:Australian
  double marketBaseRate;
  std::string marketDescription;
  bool marketDescriptionHasDate;
  time_t marketDisplayTime;
  int marketId;
  BFAPI_MktStatus marketStatus;        // 0:ACTIVE 1:INACTIVE 2:CLOSED 3:SUSPENDED
  time_t marketSuspendTime;
  time_t marketTime;
  int marketType;                      // 0:O 1:L 2:R 3:A 4:NOT_APPLICABLE
  int marketTypeVariant;               // 0:D 1:ASL 2:ADL 3:COUP
  std::string menuPath;
  vector<int> eventHierachy;
  std::string name;
  int numberOfWinners;
  int parentEventId;
  vector <MarketRunners> runners;
  int unit;
  int minUnitValue;
  int maxUnitValue;
  int interval;
  bool runnersMayBeAdded;
  std::string timezone;
  vector<CouponLinks> couponLinks;
  int completed;
};

// Raw price data type information is retrieved into a structure of this type
struct Prices {
  double amountAvailable;   // Amount available at odds specified
  std::string betType;      // Bet type enum B(ack) or L(ay)
  int depth;                // Order from best to worst, best is 1
  double price;             // Odds
};

// Raw runner price data type information is retrieved into a structure of this type
struct RPrices {
  int asianLineId;
  vector<Prices>bestPricesToBack;
  vector<Prices>bestPricesToLay;
  double handicap;
  double lastPriceMatched;
  double reductionFactor;
  int selectionId;
  int sortOrder;
  double totalAmountMatched;
  bool vacant;
};

// For map PricesbyselectionId
struct intclasscomp {
    bool operator() (const int& lhs, const int& rhs) const {return (lhs < rhs);}
};


// Raw information on market prices is retrieved into a structure of this type
struct MarketPrices {
  int errorCode;
  std::string currencyCode;
  int delay;                   // Greater than 0 if and only if the market is in-play
  int discountAllowed;
  LONG64 lastRefresh;
  double marketBaseRate;
  int marketid;
  std::string marketInfo;
  int marketStatus;
  int numberOfWinners;
  std::string removedRunners;
  vector<RPrices> runnerPrices;
};

// Response to Get Account Funds request
struct AccountFunds {
  int availBalance;            // Current balance less exposure and retained commission
  int balance;                 // Current balance
  int commissionRetain;        // Commission potentially due on markets which have not been fully settled
  int creditLimit;             // Amount of credit available
  int currentBetfairPoints;    // Total of Betfair points awarded based on commissions or implied commissions paid.
  int expoLimit;               // Total exposure allowed
  int exposure;                // Returned as negative figure. Total funds tied up with current bets
  int holidaysAvail;           // Betfair Holiday to be used to prevent weekly decay of Betfair points
  std::string minorErrorCode;  // UNUSED
  double nextDiscount;         // Discount to be applied when commission is next calculated
  int withdrawBalance;         // Balance available for withdrawl
  int errorCode;               // Specific error code
};

// Place Bets Request
struct PlaceABet {
  int asianLineId;                 // The ID of The Asian Handicap Market to place bets on. Set to 0 for non-Asian handicap markets.
  std::string betCategoryType;     // * E Exchange bet, M market on close bet, L Limit on close SP bet. None is same as E but may change
  std::string betPersistenceType;  // * IP In play persistence, SP Convert to SP on close, NONE Normal exchange or SP bet
  std::string betType;             // 0 for B Back, 1 f or L Lay
  double bspLiability;             // * This is the maximum amount of money to risk for a BSP bet. Needs to be 0.0 if unused
  int marketID;                    // The ID of the Market to place the bet on. Set to 0 for Asian handicap markets.
  double price;                    // The odds you want to set for the bet
  int selectionID;                 // ID of Desired Selection within the Market
  double size;                     // The amount of the bet
};

// Place Bets Response
struct PlaceABetResp {
  double averagePriceMatched;  // Average price taken
  LONG64 betId;                // The unique identifier for the bet
  int resultCode;              // Further information about the success or failure of the bet edit
  double sizeMatched;          // The actual price taken 
  int success;                 // True if bet successfully placed, false otherwise
};

// GBetMatches Information on each matched part of a bet
struct GBetMatches {
  int betStatus;                   //
  time_t matchedDate;              //  
  double priceMatched;             //
  double profitLoss;               //
  time_t settledDate;              //
  double sizeMatched;              //
};

// GetABet Response
struct GetABetResp {
  int asianLineId;                 // The ID of The Asian Handicap Market to place bets on. Set to 0 for non-Asian handicap markets.
  double avgPrice;                 // Average matched price of the bet, null if no part matched
  LONG64 betId;                    // The unique identifier for the bet
  int betStatus;                   //
  int betType;                     //
  time_t cancelledDate;            //
  time_t lapsedDate;               //
  int marketID;                    // The ID of the Market the bet is placed on
  std::string marketName;          // Name of the market
  std::string fullMarketString;    // The full name of the market
  std::string marketType;          // Market type data
  time_t matchedDate;              //
  double matchedSize;              // Amount of stake actually matched
  vector<GBetMatches> matches;     // 
  time_t placedDate;               //
  double price;                    // 
  double profitAndLoss;            // Actual amount won or lost
  int selectionId;                 //
  std::string selectionName;       //
  time_t settledDate;              //
  double remainingSize;            //
  double requestedSize;            //
  time_t voidedDate;               //
  std::string executedBy;          // Used internally by Betfair always returns UNKNOWN
  double handicap;                 //
  int marketTypeVariant;           //
};

//CancelBets Response
struct CancelbetsResp {
  LONG64 betId;
  int resultCode;
  double sizeCancelled;
  double sizeMatched;
  int success;
};

// Number of calls per minute info in comments from Betfair update 10th April 2006

// Number of seconds to wait for API to become available
#define BFAPI_Timeout    5

class BetFairAPIFree_5 {
  private:
    // Items used to instantiate this class as a singleton object
    static bool instanceFlag;
    static BetFairAPIFree_5 *single;
    // private constructor
    BetFairAPIFree_5();
    // Internal use utility methods
    bool getSOAPFlag(void);
    bool getSOAPFlag(bool relogin);
    // Internal items
    std::string m_username;
    std::string m_password;
    std::string m_sessiontoken;
    time_t m_lasttimestamp;
    std::string m_lasterrormsg;
    int m_locationId;
    int m_productId;
    int m_vendorSoftwareId;
    // SOAP in use indicator
    bool m_SOAPInUse;
    // Login has been requested
    bool m_LogInRequested;
    // Logged in
    bool m_LoggedIn;
    // Pointer to timer object used
    LRTimer *m_sessiontimer;
    // SOAP structure
    struct soap m_soap;

public:
    
	// Singleton use this to access object
    static BetFairAPIFree_5* getInstance(void);

    // default destructor
    ~BetFairAPIFree_5();

    // Prevent session keep alive clashing with user calls
    CRITICAL_SECTION m_cs;

	// Utility method
	time_t timeGMTtoUK(time_t evttime);

	// This usually used internally   
    bool getLogInRequested(void);

    // This usually used internally   
    bool getLoggedIn(void);

    // This usually used internally   
    void setLoggedIn(bool loggedin);

    // If drilling down first call this to get list of events to start from
    //  Unlimited calls per minute
    BFAPI_Retvals getActiveEventTypes(vector<EventType>&);

    // If drillling down call this repeatedly to drill down from top list to actual event
    //  Unlimited calls per minute
    BFAPI_Retvals getEvents(int eventid, vector<Events>&EventList);

    // Drill down to get static details of the market(s) of interest found using getEvents
	// This method will give you information such as runners in a horse race
    // Allowed 5 per minute
    BFAPI_Retvals getMarket(int eventid, vector<MarketData>&MarketDataList);

    // Retrieve dynamic details (prices) of market(s) of interest found using getEvents
    // Allowed 10 per minute
    BFAPI_Retvals getMarketPrices(int marketid, vector<MarketPrices>&MarketPrices);
    
	// Retrieve dynamic details (prices) of market(s) of interest found using getEvents
	// This returns data in the same format as getMarketPrices but uses the
	// getmarketPricesCompressed API call which allows a higher call rate on the free plan
	// Allowed 60 per minute
    BFAPI_Retvals getMarketPricesCompressed(int marketid, vector<MarketPrices>&MarketPrices);
	
	// Should not be necessary to call this as class uses a timer and maintains the login session itself
    // Unlimited calls per minute
    BFAPI_Retvals keepAlive(void);

    // Set the username and password with setUserName and setPassword then invoke this to login to Betfair
    // Allowed 24 per minute
    BFAPI_Retvals login(void);

    // This usually used internally   
    BFAPI_Retvals login(bool relogin);

    // Not mentioned in free API, a utility function anyway
    void logout(void);

    // Set BetFair username to use
    BFAPI_Retvals setUserName( std::string username );

    // Set password for BetFair username
    BFAPI_Retvals setPassword( std::string password );

    // Timer callback for internal use only
    static void sessionTimerCallback(void);

	// Call this periodically to reinitialise gsoap object and reduce memory leaks
    BFAPI_Retvals BetFairAPIFree_5::reSetSOAP(void);
	
	// Call this to close the gsoap object
	BFAPI_Retvals BetFairAPIFree_5::destroySOAP(void);

    // Allowed 12 per minute
    BFAPI_Retvals BetFairAPIFree_5::getAccountFunds(vector<AccountFunds>&AccountFList);

	// Allowed 100 per minute
    BFAPI_Retvals BetFairAPIFree_5::placeBets(vector<PlaceABet>&BetPlaceRequest, vector<PlaceABetResp>&PlaceBetResponse);

	// Allowed 60 per minute
	BFAPI_Retvals BetFairAPIFree_5::getABet(LONG64 betid, vector<GetABetResp>&BetDetails);

    // No limit
	BFAPI_Retvals BetFairAPIFree_5::cancelBets(vector<LONG64>&betid, vector<CancelbetsResp>&CancelBetsResp);

	// No Limit
    void updateBets(void);
	// Allowed 60 per minute
    void getCurrentBets(void);
	// Allowed 1 per minute
    void getAccountStatement(void);
    // Allowed 1 per minute
    void getBetHistory(void);
    // Allowed 60 per minute
    void getMarketTradedVolume(void);
	// Allowed 60 per minute
    void getmarketProfitAndLoss(void);
};

#endif