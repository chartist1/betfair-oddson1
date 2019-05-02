//
// Sendemail methods
//

#include <time.h>
#include <windows.h>
#include <iostream>
#include <string>
#include "Sendemail.h"

// Move these items to config table. Have default value for database filename overrideable from the command line
std::string smtpdom = "ws1.colinh.com";
std::string authpass = "buTTer98";
std::string fromnm = "betfairbot1";
std::string fromdom = "nxtgn.com";
std::string tonam = "colinh";
std::string todom = "nxtgn.com";

//------------------------------------------------------------------------------------------
//
//
//------------------------------------------------------------------------------------------
bool SendEmail(std::string smtpdomain, std::string authpasswd, std::string fromname, std::string  fromdomain, std::string toname, std::string todomain, std::string subject, std::string message, time_t timestamp, std::string smsmsg)
{
  bool retval = false;
  char tbuff[50] = {0};
  WSADATA wsaData = {0};
  int iResult = 0;
  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    int wsaerr = WSAGetLastError();
    cout << "WSAStartup Failure Connecting To The Email Server code : " << wsaerr << endl;
    return retval;
  }
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if(s == -1 ) {
   	int wsaerr = WSAGetLastError();
    cout <<  "Socket Initialization Error Connecting To The Email Server code : " << wsaerr << endl;
    return retval;
  }
  SOCKADDR_IN serveraddr;
  struct hostent *hostentry;
  u_short portno = 25;
  bool bSent = false;
  hostentry = gethostbyname(smtpdomain.c_str());
  char *pipaddr = inet_ntoa (*(struct in_addr *)*hostentry->h_addr_list);
  memset(&serveraddr,0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_port = htons(portno);
  serveraddr.sin_addr.s_addr = inet_addr(pipaddr);
  if ( -1 == connect(s,(SOCKADDR*)&serveraddr,sizeof(SOCKADDR_IN)) ) {
    int wsaerr = WSAGetLastError();
    cout << "ERROR Connecting To The Email Server code : " << wsaerr << endl;
    return retval;
  }
  size_t passwdsz = 0;
  char *encpasswd = base64_encode((const unsigned char *)authpasswd.c_str(), (size_t)authpasswd.size(), &passwdsz);
  std::string ausername = "";
  ausername += fromname;
  ausername += '@';
  ausername += fromdomain;
  size_t unamesz = 0;
  char *encusername = base64_encode((const unsigned char *)ausername.c_str(), (size_t)ausername.size(), &unamesz);
  char sbuf[1024] = {0};
  char rbuf[1024] = {0};
  if(recv(s, rbuf, 1024, 0) > 0) {
    if(_strnicmp(rbuf, "220", 3) == 0) {
      strcpy_s(sbuf, sizeof(sbuf), "helo betfairbot1\r\n");
      if(send(s, sbuf, strlen(sbuf), 0) == (int)strlen(sbuf)) {
        recv(s, rbuf, 1024, 0);
        if(_strnicmp(rbuf, "250", 3) == 0) {
 		      strcpy_s( sbuf, sizeof(sbuf), "AUTH LOGIN\r\n");
		      send(s, sbuf, strlen(sbuf), 0);
          recv(s, rbuf, 1024, 0);
		      // Base64 encoded Username
		      if(_strnicmp(rbuf, "334 VXNlcm5hbWU6", 16) == 0) {
			      memcpy( sbuf, encusername, unamesz);
			      free(encusername);
            send(s, sbuf, unamesz, 0);
            recv(s, rbuf, 1024, 0);
            // Base64 encoded Password
		        if(_strnicmp(rbuf, "334 UGFzc3dvcmQ6", 16) == 0) {
 		          memcpy( sbuf, encpasswd, passwdsz);
              free(encpasswd);
              send(s, sbuf, passwdsz, 0);
              recv(s, rbuf, 1024, 0);
		          if(_strnicmp(rbuf, "235 Authentication sucessful", 28) == 0) {
    		        sprintf_s(sbuf, sizeof(sbuf), "mail from: <%s@%s>\r\n", fromname.c_str(), fromdomain.c_str());
				        send(s, sbuf, strlen(sbuf), 0);
                recv(s, rbuf, 1024, 0);
                if(_strnicmp(rbuf, "250", 3) == 0) {
                  sprintf_s(sbuf, sizeof(sbuf), "rcpt to: <%s@%s>\r\n", toname.c_str(), todomain.c_str());
				          send(s, sbuf, strlen(sbuf), 0);
                  recv(s, rbuf, 1024, 0);
                  if(_strnicmp(rbuf, "250", 3) == 0) {
                    strcpy_s(sbuf, sizeof(sbuf), "data\r\n");
					          send(s, sbuf, strlen(sbuf), 0);
                    recv(s, rbuf, 1024, 0);
                    if(_strnicmp(rbuf, "354", 3) == 0) {
					            struct tm  timing = {};
                      gmtime_s( &timing, (const time_t *)&timestamp);
					            // Check if UK time is currently BST not GMT, a simple equals here
					            // would not be safe because of time taken to get here since the
					            // timestamp passed in this function was created.
					            // The intention is emails will have timestamp of current UK time
					            // as that is the time zone the races are being run in.
					            if ( (timestamp - time(NULL)) > 900 ) {
					              strftime(tbuff,sizeof(tbuff),"%a, %d %b %Y %X +0100",&timing);
					            } else {
					              strftime(tbuff,sizeof(tbuff),"%a, %d %b %Y %X +0000",&timing);
					            }
                      sprintf_s(sbuf, sizeof(sbuf), "SMS-MSG: %s\r\nFrom: %s@%s\r\nTo: %s@%s\r\nSubject:%s\r\nDate: %s\r\n\r\n%s\r\n.\r\n", smsmsg.c_str(), fromname.c_str(), fromdomain.c_str(), toname.c_str(), todomain.c_str(), subject.c_str(), tbuff, message.c_str());
					            send(s, sbuf, strlen(sbuf), 0);
                      recv(s, rbuf, 1024, 0);
                      if(_strnicmp(rbuf, "250", 3) == 0) {
                        bSent = true;
						            // Now send QUIT to email server to inidcate end of SMTP conversation.
						            // Some servers don't seem to send until this or the the next email is received.
 		                    strcpy_s( sbuf, sizeof(sbuf), "QUIT\r\n");
		                    send(s, sbuf, strlen(sbuf), 0);
                        recv(s, rbuf, 1024, 0);
                        if(_strnicmp(rbuf, "221", 3) != 0) {
						              cout << "QUIT message response from server not as expected" << endl;
						            }
					            } else {
                        cout << "Email subject and body etc not accepted by server" << endl;
					            }
					          } else {
                      cout << "Email data marker not accepted by server" << endl;
					          }
				          } else {
                    cout << "Email to address not accepted by server" << endl;
				          }
				        } else {
                  cout << "Email from address not accepted by server" << endl;
				        }
			        } else {
                cout << "Email password not accepted by server" << endl;
			        }
			      } else {
              cout << "Email username not accepted by server" << endl;
			      }
		      } else {
            cout << "AUTH LOGIN Does not appear to be supported by the Email server" << endl;
		      }
	      } else {
          cout << "Our HELO message not accepted by the Email server" << endl;
	      }
	    } else {
        cout << "Problem sending our HELO message to the Email server" << endl;
	    }
    } else {
      cout << "Did not receive expected welcome message '220' from the Email server" << endl;
    }
  }
  else {
    cout << "Did not receive anything from the Email server at start of connection" << endl;
  }
  if(bSent == false) {
    cout << rbuf << endl;
  } else {
    retval = true;
  }
  ::closesocket(s);
  iResult = WSACleanup();
  if (iResult != 0) {
    cout << "WSACleanup Failed" << endl;
  }
  return retval;
}
//------------------------------------------------------------------------------------------
//
// Need to free the data buffer returned when used.
//
//------------------------------------------------------------------------------------------
char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length)
{
typedef unsigned __int32 uint32_t;
static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                  'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                  'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                  'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                  'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                  'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                  '4', '5', '6', '7', '8', '9', '+', '/'};
  static int mod_table[] = {0, 2, 1};

  *output_length = 4 * ((input_length + 2) / 3);
  char *encoded_data = (char *)malloc((*output_length)+3);
  if (encoded_data == NULL) {
    return NULL;
  }
  for (size_t i = 0, j = 0; i < input_length;) {
    uint32_t octet_a = i < input_length ? data[i++] : 0;
    uint32_t octet_b = i < input_length ? data[i++] : 0;
    uint32_t octet_c = i < input_length ? data[i++] : 0;
    uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
    encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
  }
  for (int i = 0; i < mod_table[input_length % 3]; i++) {
    encoded_data[*output_length - 1 - i] = '=';
  }
  encoded_data[*output_length] = '\r';
  encoded_data[(*output_length)+1] = '\n';
  encoded_data[(*output_length)+2] = 0;
  (*output_length) += 2;
  return encoded_data;
}
