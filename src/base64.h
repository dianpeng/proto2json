#ifndef _BASE64_H_
#define _BASE64_H_
#include <cstddef>
#include <string>
namespace util {
void Base64Encode( const char* input , std::size_t length , std::string* output );
bool Base64Decode( const char* input , std::size_t length , std::string* output );
}// namespace util
#endif // _BASE64_H_
