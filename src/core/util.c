#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

/**
 * @brief Reserve capacity in tokenizer. Returns 0 on success, -1 on allocation failure.
 */
static int tokenizer_reserve(Tokenizer *t, int min_capacity) {
	if (t->capacity >= min_capacity) {
		return 0;
	}
	int new_capacity = t->capacity > 0 ? t->capacity : 4;
	while (new_capacity < min_capacity) {
		new_capacity <<= 1;
	}
	char **new_tokens = (char **) realloc(t->tokens, sizeof(char *) * new_capacity);
	if (new_tokens == NULL) {
		/* Return error instead of calling exit() - let caller decide how to handle */
		return -1;
	}
	t->tokens = new_tokens;
	t->capacity = new_capacity;
	return 0;
}


char *ltrim(char *str, const char *seps)	{
	size_t totrim;
	if (seps == NULL) {
		seps = "\t\n\v\f\r ";
	}
	totrim = strspn(str, seps);
	if (totrim > 0) {
		size_t len = strlen(str);
		if (totrim == len) {
			str[0] = '\0';
		}
		else {
			memmove(str, str + totrim, len + 1 - totrim);
		}
	}
	return str;
}

char *rtrim(char *str, const char *seps)	{
	int i;
	if (seps == NULL) {
		seps = "\t\n\v\f\r ";
	}
	i = strlen(str) - 1;
	while (i >= 0 && strchr(seps, str[i]) != NULL) {
		str[i] = '\0';
		i--;
	}
	return str;
}

char *trim(char *str, const char *seps)	{
	return ltrim(rtrim(str, seps), seps);
}

int indexOf(char *s,const char **array,int length_array)	{
	int index = -1,i,continuar = 1;
	for(i = 0; i <length_array && continuar; i++)	{
		if(strcmp(s,array[i]) == 0)	{
			index = i;
			continuar = 0;
		}
	}
	return index;
}

char *nextToken(Tokenizer *t)	{
	if(t == NULL || t->current >= t->n)	{
		return NULL;
	}
	return t->tokens[t->current++];
}

int hasMoreTokens(Tokenizer *t)	{
	return (t != NULL && t->current < t->n);
}

void stringtokenizer(char *data,Tokenizer *t)	{
	char *token;
	if(t == NULL || data == NULL)	{
		return;
	}
	if(t->tokens != NULL)	{
		free(t->tokens);
	}
	t->tokens = NULL;
	t->n = 0;
	t->current = 0;
	t->capacity = 0;
	trim(data,"\t\n\r :");
	token = strtok(data," \t:");
	while(token != NULL)	{
		if (tokenizer_reserve(t, t->n + 1) != 0) {
			/* Allocation failed - stop tokenizing but keep what we have */
			break;
		}
		t->tokens[t->n++] = token;
		token = strtok(NULL," \t:");
	}
}

void freetokenizer(Tokenizer *t)	{
	if(t == NULL)	{
		return;
	}
	if(t->tokens != NULL)	{
		free(t->tokens);
	}
	memset(t,0,sizeof(Tokenizer));
}


/*
	Aux function to get the hexvalues of the data
*/
char *tohex(char *ptr,int length){
  char *buffer;
  int offset = 0;
  unsigned char c;
  buffer = (char *) malloc((length * 2)+1);
  if (buffer == NULL) return NULL;
  for (int i = 0; i <length; i++) {
    c = ptr[i];
	snprintf((char*) (buffer + offset), 3, "%.2x", c);
	offset+=2;
  }
  buffer[length*2] = 0;
  return buffer;
}

void tohex_dst(char *ptr,int length,char *dst)	{
  int offset = 0;
  unsigned char c;
  for (int i = 0; i <length; i++) {
    c = ptr[i];
	snprintf((char*) (dst + offset), 3, "%.2x", c);
	offset+=2;
  }
  dst[length*2] = 0;
}

int hexs2bin(char *hex, unsigned char *out)	{
	int len;
	char   b1;
	char   b2;
	int i;

	if (hex == NULL || *hex == '\0' || out == NULL)
		return 0;

	len = strlen(hex);
	if (len % 2 != 0)
		return 0;
	len /= 2;

	memset(out, 'A', len);
	for (i=0; i<len; i++) {
		if (!hexchr2bin(hex[i*2], &b1) || !hexchr2bin(hex[i*2+1], &b2)) {
			return 0;
		}
		out[i] = (b1 << 4) | b2;
	}
	return len;
}

int hexchr2bin(const char hex, char *out)	{
	if (out == NULL)
		return 0;

	if (hex >= '0' && hex <= '9') {
		*out = hex - '0';
	} else if (hex >= 'A' && hex <= 'F') {
		*out = hex - 'A' + 10;
	} else if (hex >= 'a' && hex <= 'f') {
		*out = hex - 'a' + 10;
	} else {
		return 0;
	}

	return 1;
}

void addItemList(char *data, List *l)	{
	l->data = (char**) realloc(l->data,sizeof(char*)* (l->n +1));
	l->data[l->n] = data;
	l->n++;
}

int isValidHex(char *data)	{
	char c;
	int len,i,valid = 1;
	len = strlen(data);
	for(i = 0 ; i <  len && valid ;i++ )	{
		c = data[i];
		valid = ( (c >= '0' && c <='9') || (c >= 'A' && c <='F' ) || (c >= 'a' && c <='f' ) );
	}
	return valid;
}
