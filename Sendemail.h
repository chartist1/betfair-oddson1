#ifndef SendEmail_H
#define SendEmail_H

using namespace std;

extern std::string smtpdom;
extern std::string authpass;
extern std::string fromnm;
extern std::string fromdom;
extern std::string tonam;
extern std::string todom;

bool SendEmail(std::string smtpdomain, std::string authpasswd, std::string fromname, std::string  fromdomain, std::string toname, std::string todomain, std::string subject, std::string message, time_t timestamp, std::string smsmsg);
char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length);

#endif