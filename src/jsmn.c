/*
 * MIT License
 *
 * Copyright (c) 2010 Serge A. Zaitsev
 */
#include "jsmn.h"

#include <stddef.h>

static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser, jsmntok_t *tokens,
                                  size_t num_tokens) {
  if (parser->toknext >= num_tokens) {
    return NULL;
  }
  jsmntok_t *tok = &tokens[parser->toknext++];
  tok->start = tok->end = -1;
  tok->size = 0;
  tok->type = JSMN_UNDEFINED;
  return tok;
}

static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type, int start, int end) {
  token->type = type;
  token->start = start;
  token->end = end;
  token->size = 0;
}

static int jsmn_parse_primitive(jsmn_parser *parser, const char *js, size_t len,
                                jsmntok_t *tokens, size_t num_tokens) {
  int start = parser->pos;
  for (; parser->pos < len; parser->pos++) {
    char c = js[parser->pos];
    if (c == '\t' || c == '\r' || c == '\n' || c == ' ' || c == ',' ||
        c == ']' || c == '}') {
      jsmntok_t *tok = jsmn_alloc_token(parser, tokens, num_tokens);
      if (!tok) {
        parser->pos = start;
        return -1;
      }
      jsmn_fill_token(tok, JSMN_PRIMITIVE, start, parser->pos);
      parser->pos--;
      return 0;
    }
    if (c < 32 || c >= 127) {
      parser->pos = start;
      return -2;
    }
  }

  jsmntok_t *tok = jsmn_alloc_token(parser, tokens, num_tokens);
  if (!tok) {
    parser->pos = start;
    return -1;
  }
  jsmn_fill_token(tok, JSMN_PRIMITIVE, start, parser->pos);
  parser->pos--;
  return 0;
}

static int jsmn_parse_string(jsmn_parser *parser, const char *js, size_t len,
                             jsmntok_t *tokens, size_t num_tokens) {
  int start = parser->pos;
  parser->pos++;

  for (; parser->pos < len; parser->pos++) {
    char c = js[parser->pos];
    if (c == '\"') {
      jsmntok_t *tok = jsmn_alloc_token(parser, tokens, num_tokens);
      if (!tok) {
        parser->pos = start;
        return -1;
      }
      jsmn_fill_token(tok, JSMN_STRING, start + 1, parser->pos);
      return 0;
    }
    if (c == '\\' && parser->pos + 1 < len) {
      parser->pos++;
      switch (js[parser->pos]) {
        case '\"':
        case '/':
        case '\\':
        case 'b':
        case 'f':
        case 'r':
        case 'n':
        case 't':
          break;
        case 'u':
          parser->pos += 4;
          break;
        default:
          parser->pos = start;
          return -2;
      }
    }
  }

  parser->pos = start;
  return -2;
}

void jsmn_init(jsmn_parser *parser) {
  parser->pos = 0;
  parser->toknext = 0;
  parser->toksuper = -1;
}

int jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
               jsmntok_t *tokens, unsigned int num_tokens) {
  int count = 0;
  for (; parser->pos < len; parser->pos++) {
    char c = js[parser->pos];
    jsmntok_t *tok;
    int r;
    switch (c) {
      case '{':
      case '[':
        count++;
        tok = jsmn_alloc_token(parser, tokens, num_tokens);
        if (!tok) {
          return -1;
        }
        tok->type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
        tok->start = parser->pos;
        if (parser->toksuper != -1) {
          tokens[parser->toksuper].size++;
        }
        parser->toksuper = (int)(parser->toknext - 1);
        break;
      case '}':
      case ']':
        for (int i = (int)parser->toknext - 1; i >= 0; i--) {
          tok = &tokens[i];
          if (tok->start != -1 && tok->end == -1) {
            if ((tok->type == JSMN_OBJECT && c == '}') ||
                (tok->type == JSMN_ARRAY && c == ']')) {
              tok->end = parser->pos + 1;
              parser->toksuper = -1;
              for (int j = i - 1; j >= 0; j--) {
                if (tokens[j].start != -1 && tokens[j].end == -1) {
                  parser->toksuper = j;
                  break;
                }
              }
              break;
            }
            return -2;
          }
        }
        break;
      case '\"':
        r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
        if (r < 0) {
          return r;
        }
        count++;
        if (parser->toksuper != -1) {
          tokens[parser->toksuper].size++;
        }
        break;
      case '\t':
      case '\r':
      case '\n':
      case ' ':
      case ':':
      case ',':
        break;
      default:
        r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
        if (r < 0) {
          return r;
        }
        count++;
        if (parser->toksuper != -1) {
          tokens[parser->toksuper].size++;
        }
        break;
    }
  }

  for (unsigned int i = 0; i < parser->toknext; i++) {
    if (tokens[i].start != -1 && tokens[i].end == -1) {
      return -2;
    }
  }

  return count;
}
