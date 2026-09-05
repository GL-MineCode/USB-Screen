#ifndef __INC_DECODE_
#define __INC_DECODE_

#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <stringapiset.h>
#include <vector>
#include <sstream>
#include <cstdint>
#include <algorithm>

#define MAX_RETURN_LENGTH 2048

char* GbkToUtf8(const char *src_str) {
	int len = MultiByteToWideChar(CP_ACP, 0, src_str, -1, NULL, 0);
	wchar_t* wstr = new wchar_t[len + 1];
	memset(wstr, 0, len + 1);
	MultiByteToWideChar(CP_ACP, 0, src_str, -1, wstr, len);
	len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	static char str[MAX_RETURN_LENGTH];
	memset(str, 0, len + 1);
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
	if (wstr) delete[] wstr;
	return str;
}

std::wstring Utf8ToUtf16(std::string mbcsString){
	int mbcsLen = mbcsString.length();  
    int wcharLen = MultiByteToWideChar(CP_UTF8, 0, mbcsString.c_str(), mbcsLen, nullptr, 0);  
  
    std::wstring wideString(wcharLen, L'\0');
  
    MultiByteToWideChar(CP_UTF8, 0, mbcsString.c_str(), mbcsLen, &wideString[0], wcharLen);  
  
    return wideString;  
}

wchar_t* Utf8ToUtf16(const char* mbcsString)  
{  
    int mbcsLen = strlen(mbcsString);  
    int wcharLen = MultiByteToWideChar(CP_UTF8, 0, mbcsString, mbcsLen, nullptr, 0);  
   
	static wchar_t wcharString[MAX_RETURN_LENGTH];
  
    MultiByteToWideChar(CP_UTF8, 0, mbcsString, mbcsLen, wcharString, wcharLen);  
   
    wcharString[wcharLen] = L'\0';  
  
    return wcharString;  
}  

char* Utf8ToGbk(const char* utf8String) {  
    if (utf8String == NULL) {  
    	return NULL;  
    }  
    int utf8Length = strlen(utf8String);  
    int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8String, utf8Length, NULL, 0);  
    if (wideLength == 0) {  
        return NULL; 
    }  
    wchar_t* wideString = (wchar_t*)malloc(sizeof(wchar_t) * (wideLength + 1));  
    if (wideString == NULL) {  
        return NULL; 
    }  
    MultiByteToWideChar(CP_UTF8, 0, utf8String, utf8Length, wideString, wideLength);  
    wideString[wideLength] = L'\0';  
    int gbkLength = WideCharToMultiByte(CP_ACP, 0, wideString, wideLength, NULL, 0, NULL, NULL);  
    if (gbkLength == 0) {  
        free(wideString); 
        return NULL; 
    }  
    static char gbkString[MAX_RETURN_LENGTH];
    WideCharToMultiByte(CP_ACP, 0, wideString, wideLength, gbkString, gbkLength, NULL, NULL);  
    gbkString[gbkLength] = '\0'; 
    free(wideString);
    return gbkString;  
}

char* UnicodeToUtf8(const wchar_t* pwszUnicode) {
	int len;
	len = WideCharToMultiByte(CP_UTF8, 0, pwszUnicode, -1, NULL, 0, NULL, NULL);
	static char szUtf8[MAX_RETURN_LENGTH];
	memset(szUtf8, 0, len + 1);
	WideCharToMultiByte(CP_UTF8, 0, pwszUnicode, -1, szUtf8, len, NULL, NULL);
	return szUtf8;
}

// #define utf8(a) GbkToUtf8(a)
// #define utf16(a) Utf8ToUtf16(a)
// #define gbk(a) Utf8ToGbk(a)

size_t countChineseLetter(const char* utf8str) {
	size_t len = strlen(utf8str);
	size_t count = 0;
	for (size_t i = 0; i < len; i++) {
		if ((utf8str[i] & 0xF0) == 0xE0) count++;
	}
	return count;
}

// char* sformat(const char* format, ...) {

// 	va_list val;
// 	va_start(val, format);
// 	//size_t buf_size = _vscprintf(format, val) + 5;

// 	static char buf[MAX_RETURN_LENGTH];

// 	vsprintf(buf, format, val);

// 	return buf;
// }

#define sformat(...) cpp_sformat(__VA_ARGS__).c_str()

std::string cpp_sformat_valist(const char* format, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);

    int len = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    if (len < 0) {
        return std::string();
    }

    std::string result(len, '\0');

    vsnprintf(result.data(), len + 1, format, args);

    return result;
}

std::string cpp_sformat(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string result = cpp_sformat_valist(format, args);
    va_end(args);
    return result;
}

char GetUTF8Size(char head) {

	char upper = 1;
	char bit_mask = 0x80;
	while (head & bit_mask) {
		bit_mask = bit_mask >> 1;
		bit_mask = bit_mask & -bit_mask;
		upper++;
	}
	if (upper != 1) upper--;
	return upper;
}

bool IsUTF8Head(char x) {
	char upper = 1;
	char bit_mask = 0x80;
	while (x & bit_mask) {
		bit_mask = bit_mask >> 1;
		bit_mask = bit_mask & -bit_mask;
		upper++;
	}
	if (upper == 2) return false;
	return true;
}

bool is_utf8_starter(uint8_t byte) {
    return (byte >> 5) == 0b110;
}

size_t get_utf8_starter_size(uint8_t byte){
	if ((byte >> 7) == 0)
	{
		return 1;
	}
	else if ((byte >> 5) == 0b110)
	{
		return 2;
	}
	else if ((byte >> 4) == 0b1110)
	{
		return 3;
	}
	else if ((byte >> 3) == 0b11110)
	{
		return 4;
	}
	else
	{
		return 1;
	}
}

size_t count_utf8_characters(const std::string& utf8_str) {
    size_t char_count = 0;
    size_t byte_index = 0;
 
    while (byte_index < utf8_str.size()) {
        uint8_t first_byte = static_cast<uint8_t>(utf8_str[byte_index]);
        if ((first_byte >> 7) == 0) {
            ++char_count;
            ++byte_index;
        } else if ((first_byte >> 5) == 0b110) {
            ++char_count;
            byte_index += 2;
        } else if ((first_byte >> 4) == 0b1110) {
            ++char_count;
            byte_index += 3;
        } else if ((first_byte >> 3) == 0b11110) {
            ++char_count;
            byte_index += 4;
        } else {
            ++byte_index;
        }
    }
    return char_count;
}

template<typename _T> std::string strnum(_T num) {  
    std::stringstream ss;  
    ss << num;  
    return ss.str();
}  

template<typename _T>
_T to_basetype(const std::string& str, bool* suc) {  
    std::istringstream iss(str);  
    _T value;  
    if (!(iss >> value)) {  
        *suc = false;
        return static_cast<_T>(0);
    }  
      
    if (!iss.eof()) {  
        *suc = false; 
        return static_cast<_T>(0);  
    }  
      
    *suc = true;
    return value;
}  

template <typename... Args> std::string spformat(Args... args) {
	std::stringstream ss; 
	(ss << ... << args);
	return ss.str();
}

// class string_UTF8 : public std::string {

// 	public:

// 		std::string& this_s = *this;
// 		size_t pos = 0;
// 		size_t c_pos = 0;

// 		size_t CountLetter() {
// 			size_t count = 0;
// 			size_t len = this->this_s.length();
// 			for (size_t i = 0; i < len;) {
// 				char p = this->this_s[i];
// 				char upper = 1;
// 				char bit_mask = 0x80;
// 				while (p & bit_mask) {
// 					bit_mask = bit_mask >> 1;
// 					upper++;
// 				}
// 				if (upper != 1) upper--;
// 				i += upper;
// 				count++;
// 			}
// 			return count;
// 		}

// 		//return: success:true ; fail:false
// 		bool MoveCurRight() {
// 			if (this->this_s.length() == pos) {
// 				return false;
// 			}
// 			char pre = this->this_s[this->pos];
// 			char upper = 1;
// 			char bit_mask = 0x80;
// 			while (pre & bit_mask) {
// 				bit_mask = bit_mask >> 1;
// 				upper++;
// 			}
// 			if (upper != 1) upper--;
// 			this->pos += upper;
// 			this->c_pos++;
// 		}

// 		bool MoveCurLeft() {
// 			if (this->pos) {
// 				pos--;
// 				while (!IsUTF8Head(this->this_s[this->pos])) {
// 					pos--;
// 				}
// 				this->c_pos--;
// 				return true;
// 			} else {
// 				return false;
// 			}
// 		}


// };

//cut string with ' ' or '\"'(include with ' ' that within it)
int cut_string(const std::string& source, std::vector<std::string>& output) {
	std::string counter;
	bool within = false;
	for (auto i : source) {
		if (i == ' ') {
			if (!within) {
				if(counter.empty()){
					continue;
				}
				output.push_back(counter);
				counter.clear();
				continue;
			}
		} else if (i == '\"') {
			if (within) {
				output.push_back(counter);
				counter.clear();
			}
			within = !within;
			continue;
		}
		counter += i;
	}
	if(!counter.empty()){
		output.push_back(counter);
	}
	return output.size();
}

int split_string(const std::string& source,char acdto, std::vector<std::string>& output) {
	std::string counter;
	for (auto i : source) {
		if(i == acdto){
			output.push_back(counter);
			counter = "";
			continue;
		}
		counter += i;
	}
	if(!counter.empty()){
		output.push_back(counter);
	}
	return output.size();
}

std::string unescape_string(const std::string& str) {
    std::string ret;
    ret.reserve(str.size());
    bool flag = false;
    
    for (auto it = str.begin(); it != str.end(); ++it) {
        if (flag) {
            flag = false;
            switch (*it) {
                
                case 'a':  ret.push_back('\a'); break;
                case 'b':  ret.push_back('\b'); break;
                case 'f':  ret.push_back('\f'); break;
                case 'n':  ret.push_back('\n'); break;
                case 'r':  ret.push_back('\r'); break;
                case 't':  ret.push_back('\t'); break;
                case 'v':  ret.push_back('\v'); break;
                case '\'': ret.push_back('\''); break;
                case '"':  ret.push_back('"');  break;
                case '\\': ret.push_back('\\'); break;
                case '/':  ret.push_back('/');  break;
                case '0':  ret.push_back('\0'); break;
                
                case 'u': {
                    if (std::distance(it, str.end()) < 5) {
                        throw std::invalid_argument("Invalid Unicode escape: insufficient characters");
                    }
                    
                    std::string hex_str(it + 1, it + 5);

                    for (char c : hex_str) {
                        if (!isxdigit(static_cast<unsigned char>(c))) {
                            throw std::invalid_argument("Invalid Unicode escape: non-hex character - " + hex_str);
                        }
                    }
                    
                    uint16_t unicode_code = static_cast<uint16_t>(std::stoi(hex_str, nullptr, 16));

                    if (unicode_code <= 0x7F) {
                        ret.push_back(static_cast<char>(unicode_code));
                    } else if (unicode_code <= 0x7FF) {
                        ret.push_back(static_cast<char>(0xC0 | ((unicode_code >> 6) & 0x1F)));
                        ret.push_back(static_cast<char>(0x80 | (unicode_code & 0x3F)));
                    } else {
                        ret.push_back(static_cast<char>(0xE0 | ((unicode_code >> 12) & 0x0F)));
                        ret.push_back(static_cast<char>(0x80 | ((unicode_code >> 6) & 0x3F)));
                        ret.push_back(static_cast<char>(0x80 | (unicode_code & 0x3F)));
                    }
                    
                    it += 4;
                    break;
                }
                
                default:
                    throw std::invalid_argument(std::string("Invalid escape character in JSON string: \\") + *it);
            }
        } else {
            if (*it == '\\') {
                flag = true;
            } else {
                ret.push_back(*it);
            }
        }
    }
    if (flag) {
        throw std::invalid_argument("Invalid escaped string: ends with unescaped backslash");
    }
    return ret;
}

std::string escape_string(const std::string& str){
	std::string ret;
	for(auto i = str.begin();i<str.end();i++){
		switch (*i)
		{
		case '\a':
			ret.push_back('\\');
			ret.push_back('a');
			break;
		case '\b':
			ret.push_back('\\');
			ret.push_back('b');
			break;
		case '\f':
			ret.push_back('\\');
			ret.push_back('f');
			break;
		case '\n':
			ret.push_back('\\');
			ret.push_back('n');
			break;
		case '\r':
			ret.push_back('\\');
			ret.push_back('r');
			break;
		case '\t':
			ret.push_back('\\');
			ret.push_back('t');
			break;
		case '\v':
			ret.push_back('\\');
			ret.push_back('v');
			break;
		case '\'':
			ret.push_back('\\');
			ret.push_back('\'');
			break;
		case '\"':
			ret.push_back('\\');
			ret.push_back('\"');
			break;
		case '\\':
			ret.push_back('\\');
			ret.push_back('\\');
			break;
		case '\0':
			//ret.push_back('\\');
			//ret.push_back('0');
			break;
		default:
			ret.push_back(*i);
			break;
		}
	}
	return ret;
}

std::string SubUTF8String(const std::string& str, size_t length) {
    // 如果要求长度为0，直接返回空字符串
    if (length == 0) {
        return "";
    }
    
    size_t charCount = 0;
    size_t pos = 0;
    const size_t totalBytes = str.size();
    
    while (pos < totalBytes && charCount < length) {
        unsigned char c = static_cast<unsigned char>(str[pos]);
        size_t charLen = 0;
        if (c < 0x80) {
            charLen = 1;
        } else if ((c & 0xE0) == 0xC0) {
            charLen = 2;
        } else if ((c & 0xF0) == 0xE0) {
            charLen = 3;
        } else if ((c & 0xF8) == 0xF0) {
            charLen = 4;
        } else {
            charLen = 1;
        }
        if (pos + charLen > totalBytes) {
            break;
        }
        bool valid = true;
        for (size_t i = 1; i < charLen; ++i) {
            if ((static_cast<unsigned char>(str[pos + i]) & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            charLen = 1;
        }
        pos += charLen;
        charCount++;
    }
    return str.substr(0, pos);
}

bool is_valid_unicode(uint32_t code_point) {
    if (code_point > 0x10FFFF) {
        return false;
    }
    
    if (code_point >= 0xD800 && code_point <= 0xDFFF) {
        return false;
    }
    
    return true;
}

std::string code_point_to_utf8(uint32_t code_point) {
    std::string utf8_char;
    
    if (code_point <= 0x7F) {
        utf8_char += static_cast<char>(code_point);
    }
    else if (code_point <= 0x07FF) {
        utf8_char += static_cast<char>(0xC0 | (code_point >> 6));
        utf8_char += static_cast<char>(0x80 | (code_point & 0x3F));
    }
    else if (code_point <= 0xFFFF) {
        utf8_char += static_cast<char>(0xE0 | (code_point >> 12));
        utf8_char += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        utf8_char += static_cast<char>(0x80 | (code_point & 0x3F));
    }
    else {
        utf8_char += static_cast<char>(0xF0 | (code_point >> 18));
        utf8_char += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        utf8_char += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        utf8_char += static_cast<char>(0x80 | (code_point & 0x3F));
    }
    
    return utf8_char;
}

std::vector<uint32_t> utf8_to_codepoints(const std::string& utf8_str) {
    std::vector<uint32_t> codepoints;
    
    for (size_t i = 0; i < utf8_str.size(); ) {
        uint8_t first_byte = static_cast<uint8_t>(utf8_str[i]);
        
        size_t char_len = 0;
        uint32_t codepoint = 0;
        
        if (first_byte <= 0x7F) {
            char_len = 1;
            codepoint = first_byte;
        } else if ((first_byte & 0xE0) == 0xC0) {
            char_len = 2;
            codepoint = first_byte & 0x1F;
        } else if ((first_byte & 0xF0) == 0xE0) {
            char_len = 3;
            codepoint = first_byte & 0x0F;
        } else if ((first_byte & 0xF8) == 0xF0) {
            char_len = 4;
            codepoint = first_byte & 0x07;
        } else {
            throw std::runtime_error("Invalid UTF-8 sequence");
        }
        
        if (i + char_len > utf8_str.size()) {
            throw std::runtime_error("Incomplete UTF-8 sequence");
        }
        
        for (size_t j = 1; j < char_len; ++j) {
            uint8_t next_byte = static_cast<uint8_t>(utf8_str[i + j]);
            
            if ((next_byte & 0xC0) != 0x80) {
                throw std::runtime_error("Invalid UTF-8 continuation byte");
            }
            
            codepoint = (codepoint << 6) | (next_byte & 0x3F);
        }
        
        if ((char_len == 1 && codepoint > 0x7F) ||
            (char_len == 2 && (codepoint < 0x80 || codepoint > 0x7FF)) ||
            (char_len == 3 && (codepoint < 0x800 || codepoint > 0xFFFF)) ||
            (char_len == 4 && (codepoint < 0x10000 || codepoint > 0x10FFFF))) {
            throw std::runtime_error("Invalid Unicode codepoint");
        }
        
        codepoints.push_back(codepoint);
        i += char_len;
    }
    
    return codepoints;
}

std::vector<uint32_t> utf8_to_codepoints_noexcept(const std::string& utf8_str) noexcept {
    std::vector<uint32_t> codepoints;
    
    for (size_t i = 0; i < utf8_str.size(); ) {
        uint8_t first_byte = static_cast<uint8_t>(utf8_str[i]);
        
        size_t char_len = 0;
        uint32_t codepoint = 0;
        
        if (first_byte <= 0x7F) {
            char_len = 1;
            codepoint = first_byte;
        } else if ((first_byte & 0xE0) == 0xC0) {
            char_len = 2;
            codepoint = first_byte & 0x1F;
        } else if ((first_byte & 0xF0) == 0xE0) {
            char_len = 3;
            codepoint = first_byte & 0x0F;
        } else if ((first_byte & 0xF8) == 0xF0) {
            char_len = 4;
            codepoint = first_byte & 0x07;
        } else {
            return codepoints;
        }
        
        if (i + char_len > utf8_str.size()) {
            return codepoints;
        }
        
        for (size_t j = 1; j < char_len; ++j) {
            uint8_t next_byte = static_cast<uint8_t>(utf8_str[i + j]);
            
            if ((next_byte & 0xC0) != 0x80) {
                return codepoints;
            }
            
            codepoint = (codepoint << 6) | (next_byte & 0x3F);
        }
        
        if ((char_len == 1 && codepoint > 0x7F) ||
            (char_len == 2 && (codepoint < 0x80 || codepoint > 0x7FF)) ||
            (char_len == 3 && (codepoint < 0x800 || codepoint > 0xFFFF)) ||
            (char_len == 4 && (codepoint < 0x10000 || codepoint > 0x10FFFF))) {
            return codepoints;
        }
        
        codepoints.push_back(codepoint);
        i += char_len;
    }
    
    return codepoints;
}

std::string codepoints_to_utf8(const std::vector<uint32_t>& codepoints) {
    std::string utf8_str;
    
    for (uint32_t codepoint : codepoints) {
        if (codepoint <= 0x7F) {
            utf8_str += static_cast<char>(codepoint);
        } else if (codepoint <= 0x7FF) {
            utf8_str += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
            utf8_str += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0xFFFF) {
            utf8_str += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
            utf8_str += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            utf8_str += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0x10FFFF) {
            utf8_str += static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
            utf8_str += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            utf8_str += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            utf8_str += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            throw std::runtime_error("Invalid Unicode codepoint");
        }
    }
    return utf8_str;
}

std::string codepoints_to_utf8(std::vector<uint32_t>::const_iterator _begin,std::vector<uint32_t>::const_iterator _end) {
    std::string utf8_str;
    
    for (auto i = _begin;i < _end;i++) {
        uint32_t codepoint = *i;
        if (codepoint <= 0x7F) {
            utf8_str += static_cast<char>(codepoint);
        } else if (codepoint <= 0x7FF) {
            utf8_str += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
            utf8_str += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0xFFFF) {
            utf8_str += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
            utf8_str += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            utf8_str += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0x10FFFF) {
            utf8_str += static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
            utf8_str += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            utf8_str += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            utf8_str += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            throw std::runtime_error("Invalid Unicode codepoint");
        }
    }
    return utf8_str;
}

std::string trim(const std::string& s) {
    const std::string WHITESPACE = " \t\n\r\v\f";
    size_t start = s.find_first_not_of(WHITESPACE);
    if (start == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(WHITESPACE);
    return s.substr(start, end - start + 1);
}

int calculateLevenshtein(const std::string& str1, const std::string& str2) {
    int m = str1.size();
    int n = str2.size();

    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));

    for (int j = 0; j <= n; ++j) {
        dp[0][j] = j;
    }

    for (int i = 0; i <= m; ++i) {
        dp[i][0] = i;
    }

    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            if (str1[i - 1] == str2[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = 1 + std::min({
                    dp[i - 1][j],
                    dp[i][j - 1],
                    dp[i - 1][j - 1]
                });
            }
        }
    }

    return dp[m][n];
}

int calculateMatchScore(const std::string& src, const std::string& dst) {
    int totalScore = 0;
    
    int levDistance = calculateLevenshtein(src, dst);
    if (levDistance < 5) {
        totalScore += (5 - levDistance);
    }
    
    bool isSubstring = (src.find(dst) != std::string::npos) || (dst.find(src) != std::string::npos);
    if (isSubstring) {
        totalScore += 5;
    }
    
    return totalScore;
}

/**
 * @brief 将UTF-8字符串按行拆分为Codepoints列表（丢弃\r，以\n分行），至少返回一行
 * 
 * @param text 原始UTF-8文本
 * @return std::vector<std::vector<uint32_t>> 每行的Codepoint列表
 */
std::vector<std::vector<uint32_t>> utf8_to_codepoints_lines(const std::string& text){
    std::vector<std::vector<uint32_t>> lines;
    std::vector<uint32_t> current_line;
    size_t i = 0;
    size_t len = text.size();
    while(i < len){
        unsigned char c = (unsigned char)text[i];
        if(c < 0x80){
            if(c == '\n'){
                lines.push_back(std::move(current_line));
                current_line.clear();
            }
            else if(c != '\r'){
                current_line.push_back(c);
            }
            i++;
            continue;
        }
        uint32_t cp = 0;
        size_t extra = 0;
        if((c & 0xE0) == 0xC0){ cp = c & 0x1F; extra = 1; }
        else if((c & 0xF0) == 0xE0){ cp = c & 0x0F; extra = 2; }
        else if((c & 0xF8) == 0xF0){ cp = c & 0x07; extra = 3; }
        else{ i++; continue; }
        bool valid = (i + extra < len);
        for(size_t k = 1; valid && k <= extra; k++){
            unsigned char cc = (unsigned char)text[i + k];
            if((cc & 0xC0) != 0x80) valid = false;
            else cp = (cp << 6) | (cc & 0x3F);
        }
        if(valid) current_line.push_back(cp);
        //替换为U+FFFD
        else{
            for(size_t k = 0; k <= extra; k++){
                current_line.push_back(0xFFFD);
            }
        }
        i += extra + 1;
    }
    lines.push_back(std::move(current_line));
    return lines;
}

/**
 * @brief 将多行Codepoints列表拼接为UTF-8字符串，行间以指定换行符连接
 * 
 * @param lines 每行的Codepoint列表
 * @param newline 换行符，默认CRLF（"\r\n"）
 * @return std::string 拼接后的UTF-8字符串
 */
std::string codepoints_lines_to_utf8(const std::vector<std::vector<uint32_t>>& lines,const std::string& newline = "\r\n"){
    std::string result;
    for(size_t i = 0; i < lines.size(); i++){
        if(i > 0) result += newline;
        result += codepoints_to_utf8(lines[i]);
    }
    return result;
}

#endif
