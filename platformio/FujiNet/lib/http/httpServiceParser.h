/* FujiNet web server helper class

Broke out parsing functions to make things easier to read.

If a file has an extention pre-determined to support parsing (see/update
    fnHttpServiceParser::is_parsable() for a the list) then the
    following happens:

    * The entire file contents are loaded into an in-memory string.
    * Anything with the pattern <%PARSE_TAG%> is replaced with an
    * appropriate value as determined by the 
    *       string substitute_tag(const string &tag)
    * function.
    * 
Currently handled tags:
    * FN_HOSTNAME
    * FN_VERSION
    * FN_IPADDRESS
*/
#ifndef HTTPSERVICEPARSER_H
#define HTTPSERVICEPARSER_H

class fnHttpServiceParser
{
public:
    static std::string substitute_tag(const std::string &tag);
    static std::string parse_contents(const std::string &contents);
    static bool is_parsable(const char *extension);
};

#endif // HTTPSERVICEPARSER_H
