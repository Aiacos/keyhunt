#ifndef CUSTOMUTILH
#define CUSTOMUTILH

typedef struct str_list {
	int n;
	char **data;
	int *lengths;
} List;

typedef struct str_tokenizer {
	int current;
	int n;
	int capacity;
	char **tokens;
} Tokenizer;

/**
 * @brief Remove leading separator characters from a string in place.
 * @param str Mutable string to trim. Must not be NULL.
 * @param seps Optional string containing separator characters; defaults to whitespace.
 * @return Pointer to the trimmed string (same as @p str).
 */
char *ltrim(char *str, const char *seps);

/**
 * @brief Remove trailing separator characters from a string in place.
 * @param str Mutable string to trim. Must not be NULL.
 * @param seps Optional string containing separator characters; defaults to whitespace.
 * @return Pointer to the trimmed string (same as @p str).
 */
char *rtrim(char *str, const char *seps);

/**
 * @brief Remove leading and trailing separator characters from a string.
 * @param str Mutable string to trim. Must not be NULL.
 * @param seps Optional string containing separator characters; defaults to whitespace.
 * @return Pointer to the trimmed string (same as @p str).
 */
char *trim(char *str, const char *seps);

/**
 * @brief Locate the index of a string inside a string array.
 * @param s Needle string to search for.
 * @param array Array of strings to scan.
 * @param length_array Number of entries in @p array.
 * @return Zero-based index if found, otherwise -1.
 */
int indexOf(char *s, const char **array, int length_array);

/**
 * @brief Convert a hexadecimal character into its integer value.
 * @param hex Input hexadecimal character.
 * @param out Output pointer receiving the value [0, 15].
 * @return 1 on success, 0 if the input is not hexadecimal.
 */
int hexchr2bin(char hex, char *out);

/**
 * @brief Convert an ASCII hexadecimal string to its binary representation.
 * @param hex Zero-terminated string containing an even number of hex digits.
 * @param out Output buffer receiving the binary data. Must have at least strlen(hex)/2 bytes.
 * @return Number of bytes written on success, 0 otherwise.
 */
int hexs2bin(char *hex, unsigned char *out);

/**
 * @brief Allocate and return a newly created hexadecimal string representation.
 * @param ptr Pointer to the input buffer.
 * @param length Number of bytes to convert.
 * @return Newly allocated string containing the hex representation. Caller must free.
 */
char *tohex(char *ptr, int length);

/**
 * @brief Write a hexadecimal string representation into the provided destination buffer.
 * @param ptr Pointer to the input buffer.
 * @param length Number of bytes to convert.
 * @param dst Destination buffer with space for length*2+1 characters.
 */
void tohex_dst(char *ptr, int length, char *dst);

/**
 * @brief Determine whether more tokens are available.
 * @param t Tokenizer instance.
 * @return Non-zero when at least one more token can be read.
 */
int hasMoreTokens(Tokenizer *t);

/**
 * @brief Retrieve the next token from a tokenizer.
 * @param t Tokenizer instance.
 * @return Pointer to the next token, or NULL if exhausted.
 */
char *nextToken(Tokenizer *t);

/**
 * @brief Check whether a string only contains hexadecimal characters.
 * @param data Input string to validate.
 * @return Non-zero if the string is a valid hexadecimal sequence, zero otherwise.
 */
int isValidHex(char *data);

/**
 * @brief Release every resource owned by a tokenizer and reset its state.
 * @param t Tokenizer instance.
 */
void freetokenizer(Tokenizer *t);

/**
 * @brief Tokenize an input string in place using whitespace, tab or colon separators.
 * @param data Input string to tokenize. Tokens are zero-terminated slices of this buffer.
 * @param t Tokenizer instance that will reference tokens within @p data.
 */
void stringtokenizer(char *data, Tokenizer *t);

#endif // CUSTOMUTILH
