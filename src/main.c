#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jsmn.h"

static int ensure_tokens(jsmn_parser *parser, const char *json, size_t len,
                         jsmntok_t **tokens_out, int *count_out);

static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [--silent] [--exclude-response-headers] [--version] <config.json>\n",
          prog);
}

static char *read_file(const char *path, size_t *out_len) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return NULL;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long len = ftell(fp);
  if (len < 0) {
    fclose(fp);
    return NULL;
  }
  rewind(fp);
  char *buf = (char *)malloc((size_t)len + 1);
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  size_t read_len = fread(buf, 1, (size_t)len, fp);
  fclose(fp);
  if (read_len != (size_t)len) {
    free(buf);
    return NULL;
  }
  buf[len] = '\0';
  if (out_len) {
    *out_len = (size_t)len;
  }
  return buf;
}

static int skip_token(const jsmntok_t *toks, int index) {
  int i = index;
  switch (toks[i].type) {
    case JSMN_STRING:
    case JSMN_PRIMITIVE:
      return i + 1;
    case JSMN_ARRAY: {
      i++;
      for (int k = 0; k < toks[index].size; k++) {
        i = skip_token(toks, i);
      }
      return i;
    }
    case JSMN_OBJECT: {
      int pairs = toks[index].size / 2;
      i++;
      for (int k = 0; k < pairs; k++) {
        i = skip_token(toks, i);
        i = skip_token(toks, i);
      }
      return i;
    }
    default:
      return i + 1;
  }
}

static bool jsoneq(const char *json, const jsmntok_t *tok, const char *s) {
  size_t len = (size_t)(tok->end - tok->start);
  return tok->type == JSMN_STRING &&
         strlen(s) == len &&
         strncmp(json + tok->start, s, len) == 0;
}

static int find_object_value(const char *json, jsmntok_t *toks, int obj_index,
                             const char *key) {
  if (toks[obj_index].type != JSMN_OBJECT) {
    return -1;
  }
  int i = obj_index + 1;
  int pairs = toks[obj_index].size / 2;
  for (int pair = 0; pair < pairs; pair++) {
    int key_index = i;
    int value_index = i + 1;
    if (jsoneq(json, &toks[key_index], key)) {
      return value_index;
    }
    i = skip_token(toks, value_index);
  }
  return -1;
}

static char *dup_token_string(const char *json, const jsmntok_t *tok) {
  if (tok->type != JSMN_STRING) {
    return NULL;
  }
  size_t len = (size_t)(tok->end - tok->start);
  char *out = (char *)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, json + tok->start, len);
  out[len] = '\0';
  return out;
}

static char *dup_token_raw(const char *json, const jsmntok_t *tok) {
  if (tok->start < 0 || tok->end < 0 || tok->end < tok->start) {
    return NULL;
  }
  size_t len = (size_t)(tok->end - tok->start);
  char *out = (char *)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, json + tok->start, len);
  out[len] = '\0';
  return out;
}

static char *dup_string(const char *src) {
  size_t len = strlen(src);
  char *out = (char *)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, src, len + 1);
  return out;
}

typedef int (*kv_callback)(const char *name, const char *value, void *userdata);

static const char *tok_type_name(jsmntype_t type) {
  switch (type) {
    case JSMN_UNDEFINED:
      return "undefined";
    case JSMN_OBJECT:
      return "object";
    case JSMN_ARRAY:
      return "array";
    case JSMN_STRING:
      return "string";
    case JSMN_PRIMITIVE:
      return "primitive";
    default:
      return "unknown";
  }
}

static int iterate_kv(const char *json, jsmntok_t *toks, int index,
                      const char *label, kv_callback cb, void *userdata) {
  if (index < 0) {
    return 0;
  }
  if (toks[index].type == JSMN_ARRAY) {
    int i = index + 1;
    for (int e = 0; e < toks[index].size; e++) {
      int elem_index = i;
      if (toks[elem_index].type == JSMN_OBJECT) {
        int name_idx = find_object_value(json, toks, elem_index, "name");
        if (name_idx < 0) {
          name_idx = find_object_value(json, toks, elem_index, "key");
        }
        int value_idx = find_object_value(json, toks, elem_index, "value");
        if (name_idx >= 0 && value_idx >= 0) {
          if (toks[name_idx].type != JSMN_STRING || toks[value_idx].type != JSMN_STRING) {
            fprintf(stderr,
                    "Invalid %s entry: name/value must be strings (got %s/%s).\n",
                    label, tok_type_name(toks[name_idx].type),
                    tok_type_name(toks[value_idx].type));
            return -1;
          }
          char *name = dup_token_string(json, &toks[name_idx]);
          char *value = dup_token_string(json, &toks[value_idx]);
          if (!name || !value) {
            fprintf(stderr, "Out of memory while reading %s.\n", label);
            free(name);
            free(value);
            return -1;
          }
          cb(name, value, userdata);
          free(name);
          free(value);
        }
      }
      i = skip_token(toks, elem_index);
    }
    return 0;
  }
  if (toks[index].type == JSMN_OBJECT) {
    int i = index + 1;
    int pairs = toks[index].size / 2;
    for (int pair = 0; pair < pairs; pair++) {
      int key_index = i;
      int value_index = i + 1;
      if (toks[key_index].type != JSMN_STRING || toks[value_index].type != JSMN_STRING) {
        fprintf(stderr,
                "Invalid %s entry: key/value must be strings (got %s/%s).\n",
                label, tok_type_name(toks[key_index].type),
                tok_type_name(toks[value_index].type));
        return -1;
      }
      char *name = dup_token_string(json, &toks[key_index]);
      char *value = dup_token_string(json, &toks[value_index]);
      if (!name || !value) {
        fprintf(stderr, "Out of memory while reading %s.\n", label);
        free(name);
        free(value);
        return -1;
      }
      cb(name, value, userdata);
      free(name);
      free(value);
      i = skip_token(toks, value_index);
    }
    return 0;
  }
  fprintf(stderr, "Invalid %s: expected array or object.\n", label);
  return -1;
}

static char *replace_all(const char *src, const char *search, const char *replace) {
  if (!src || !search || !replace) {
    return NULL;
  }
  size_t src_len = strlen(src);
  size_t search_len = strlen(search);
  size_t replace_len = strlen(replace);
  if (search_len == 0) {
    return strdup(src);
  }

  size_t count = 0;
  const char *pos = src;
  while ((pos = strstr(pos, search)) != NULL) {
    count++;
    pos += search_len;
  }
  if (count == 0) {
    return strdup(src);
  }

  size_t out_len = src_len + count * (replace_len - search_len);
  char *out = (char *)malloc(out_len + 1);
  if (!out) {
    return NULL;
  }

  const char *src_it = src;
  char *dst_it = out;
  while ((pos = strstr(src_it, search)) != NULL) {
    size_t chunk = (size_t)(pos - src_it);
    memcpy(dst_it, src_it, chunk);
    dst_it += chunk;
    memcpy(dst_it, replace, replace_len);
    dst_it += replace_len;
    src_it = pos + search_len;
  }
  strcpy(dst_it, src_it);
  return out;
}

static int append_query_param(char **url, const char *name, const char *value,
                              bool *has_query) {
  const char *prefix = *has_query ? "&" : "?";
  size_t new_len = strlen(*url) + strlen(prefix) + strlen(name) + 1 + strlen(value) + 1;
  char *out = (char *)malloc(new_len);
  if (!out) {
    return -1;
  }
  snprintf(out, new_len, "%s%s%s=%s", *url, prefix, name, value);
  free(*url);
  *url = out;
  *has_query = true;
  return 0;
}

static size_t write_stdout(void *ptr, size_t size, size_t nmemb, void *userdata) {
  (void)userdata;
  return fwrite(ptr, size, nmemb, stdout);
}

static size_t write_discard(void *ptr, size_t size, size_t nmemb, void *userdata) {
  (void)ptr;
  (void)userdata;
  return size * nmemb;
}

struct response_buffer {
  char *data;
  size_t len;
};

static size_t write_buffer(void *ptr, size_t size, size_t nmemb, void *userdata) {
  struct response_buffer *buf = (struct response_buffer *)userdata;
  size_t total = size * nmemb;
  char *next = (char *)realloc(buf->data, buf->len + total + 1);
  if (!next) {
    return 0;
  }
  memcpy(next + buf->len, ptr, total);
  buf->data = next;
  buf->len += total;
  buf->data[buf->len] = '\0';
  return total;
}

struct header_entry {
  char *name;
  char *value;
};

struct header_list {
  struct header_entry *items;
  size_t count;
  size_t cap;
};

struct response_headers {
  struct header_list headers;
  char *status_line;
};

static void header_list_free(struct header_list *list) {
  for (size_t i = 0; i < list->count; i++) {
    free(list->items[i].name);
    free(list->items[i].value);
  }
  free(list->items);
  list->items = NULL;
  list->count = 0;
  list->cap = 0;
}

static int header_list_append(struct header_list *list, const char *name, const char *value) {
  if (list->count == list->cap) {
    size_t next_cap = list->cap == 0 ? 8 : list->cap * 2;
    struct header_entry *next = (struct header_entry *)realloc(
        list->items, next_cap * sizeof(struct header_entry));
    if (!next) {
      return -1;
    }
    list->items = next;
    list->cap = next_cap;
  }
  list->items[list->count].name = dup_string(name);
  list->items[list->count].value = dup_string(value);
  if (!list->items[list->count].name || !list->items[list->count].value) {
    free(list->items[list->count].name);
    free(list->items[list->count].value);
    return -1;
  }
  list->count++;
  return 0;
}

static void trim_whitespace(char *str) {
  char *end = str + strlen(str);
  while (end > str && (*(end - 1) == ' ' || *(end - 1) == '\t')) {
    end--;
  }
  *end = '\0';
  while (*str == ' ' || *str == '\t') {
    memmove(str, str + 1, strlen(str));
  }
}

static size_t write_header(void *ptr, size_t size, size_t nmemb, void *userdata) {
  struct response_headers *resp = (struct response_headers *)userdata;
  size_t total = size * nmemb;
  char *line = (char *)malloc(total + 1);
  if (!line) {
    return 0;
  }
  memcpy(line, ptr, total);
  line[total] = '\0';
  while (total > 0 && (line[total - 1] == '\n' || line[total - 1] == '\r')) {
    line[--total] = '\0';
  }
  if (total == 0) {
    free(line);
    return size * nmemb;
  }
  if (strncmp(line, "HTTP/", 5) == 0) {
    free(resp->status_line);
    resp->status_line = dup_string(line);
    free(line);
    return size * nmemb;
  }
  char *colon = strchr(line, ':');
  if (!colon) {
    free(line);
    return size * nmemb;
  }
  *colon = '\0';
  char *name = line;
  char *value = colon + 1;
  while (*value == ' ' || *value == '\t') {
    value++;
  }
  trim_whitespace(name);
  trim_whitespace(value);
  header_list_append(&resp->headers, name, value);
  free(line);
  return size * nmemb;
}

static char *json_escape(const char *src) {
  size_t len = 0;
  for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
    switch (*p) {
      case '\\':
      case '"':
      case '\b':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
        len += 2;
        break;
      default:
        len += (*p < 0x20) ? 6 : 1;
    }
  }
  char *out = (char *)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  char *dst = out;
  for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
    switch (*p) {
      case '\\':
        *dst++ = '\\';
        *dst++ = '\\';
        break;
      case '"':
        *dst++ = '\\';
        *dst++ = '"';
        break;
      case '\b':
        *dst++ = '\\';
        *dst++ = 'b';
        break;
      case '\f':
        *dst++ = '\\';
        *dst++ = 'f';
        break;
      case '\n':
        *dst++ = '\\';
        *dst++ = 'n';
        break;
      case '\r':
        *dst++ = '\\';
        *dst++ = 'r';
        break;
      case '\t':
        *dst++ = '\\';
        *dst++ = 't';
        break;
      default:
        if (*p < 0x20) {
          snprintf(dst, 7, "\\u%04x", *p);
          dst += 6;
        } else {
          *dst++ = (char)*p;
        }
    }
  }
  *dst = '\0';
  return out;
}

static bool is_valid_json(const char *json, size_t len) {
  jsmn_parser parser;
  jsmntok_t *tokens = NULL;
  int tok_count = 0;
  if (ensure_tokens(&parser, json, len, &tokens, &tok_count) != 0) {
    return false;
  }
  bool ok = tok_count > 0;
  free(tokens);
  return ok;
}

static void print_json_response(long status, const char *status_line,
                                const struct header_list *headers,
                                const struct response_buffer *body) {
  const char *status_src = status_line ? status_line : "";
  char *status_esc = json_escape(status_src);
  if (!status_esc) {
    return;
  }
  printf("{\"status\":%ld,\"status_text\":\"%s\",\"headers\":[", status, status_esc);
  free(status_esc);
  for (size_t i = 0; i < headers->count; i++) {
    char *name_esc = json_escape(headers->items[i].name);
    char *value_esc = json_escape(headers->items[i].value);
    if (!name_esc || !value_esc) {
      free(name_esc);
      free(value_esc);
      return;
    }
    if (i > 0) {
      fputc(',', stdout);
    }
    printf("{\"name\":\"%s\",\"value\":\"%s\"}", name_esc, value_esc);
    free(name_esc);
    free(value_esc);
  }
  printf("],\"body\":");
  if (body && body->data && body->len > 0 && is_valid_json(body->data, body->len)) {
    fwrite(body->data, 1, body->len, stdout);
  } else {
    const char *body_src = body && body->data ? body->data : "";
    char *body_esc = json_escape(body_src);
    if (!body_esc) {
      return;
    }
    printf("\"%s\"", body_esc);
    free(body_esc);
  }
  printf("}\n");
}

struct curl_ctx {
  CURL *curl;
  char **url;
  bool *has_query;
  struct curl_slist **headers;
};

static int apply_path_param(const char *name, const char *value, void *userdata) {
  struct curl_ctx *ctx = (struct curl_ctx *)userdata;
  char *escaped = curl_easy_escape(ctx->curl, value, 0);
  if (!escaped) {
    return 0;
  }
  size_t placeholder_len = strlen(name) + 2;
  char *placeholder = (char *)malloc(placeholder_len + 1);
  if (placeholder) {
    snprintf(placeholder, placeholder_len + 1, "{%s}", name);
    char *replaced = replace_all(*ctx->url, placeholder, escaped);
    if (replaced) {
      free(*ctx->url);
      *ctx->url = replaced;
    }
    free(placeholder);
  }
  curl_free(escaped);
  return 0;
}

static int apply_query_param(const char *name, const char *value, void *userdata) {
  struct curl_ctx *ctx = (struct curl_ctx *)userdata;
  char *enc_name = curl_easy_escape(ctx->curl, name, 0);
  char *enc_value = curl_easy_escape(ctx->curl, value, 0);
  if (enc_name && enc_value) {
    append_query_param(ctx->url, enc_name, enc_value, ctx->has_query);
  }
  if (enc_name) {
    curl_free(enc_name);
  }
  if (enc_value) {
    curl_free(enc_value);
  }
  return 0;
}

static int apply_header(const char *name, const char *value, void *userdata) {
  struct curl_ctx *ctx = (struct curl_ctx *)userdata;
  size_t header_len = strlen(name) + strlen(value) + 3;
  char *header = (char *)malloc(header_len);
  if (header) {
    snprintf(header, header_len, "%s: %s", name, value);
    *ctx->headers = curl_slist_append(*ctx->headers, header);
    free(header);
  }
  return 0;
}

static int ensure_tokens(jsmn_parser *parser, const char *json, size_t len,
                         jsmntok_t **tokens_out, int *count_out) {
  int token_count = 256;
  for (;;) {
    jsmntok_t *tokens = (jsmntok_t *)calloc((size_t)token_count, sizeof(jsmntok_t));
    if (!tokens) {
      return -1;
    }
    jsmn_init(parser);
    int parsed = jsmn_parse(parser, json, len, tokens, (unsigned int)token_count);
    if (parsed >= 0) {
      *tokens_out = tokens;
      *count_out = parsed;
      return 0;
    }
    free(tokens);
    if (parsed == -1) {
      token_count *= 2;
      if (token_count > 4096) {
        return -1;
      }
      continue;
    }
    return -1;
  }
}

static void print_parse_error(void) {
  fprintf(stderr, "Invalid JSON structure.\n");
}

int main(int argc, char **argv) {
  bool use_exit_codes = false;
  bool include_headers = true;
  const char *config_path = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0) {
      printf("pinga %s\n", PINGA_VERSION);
      return 0;
    }
    if (strcmp(argv[i], "--silent") == 0) {
      use_exit_codes = true;
      continue;
    }
    if (strcmp(argv[i], "--exclude-response-headers") == 0) {
      include_headers = false;
      continue;
    }
    if (argv[i][0] == '-') {
      print_usage(argv[0]);
      return 1;
    }
    if (config_path) {
      print_usage(argv[0]);
      return 1;
    }
    config_path = argv[i];
  }

  if (!config_path) {
    print_usage(argv[0]);
    return 1;
  }

  size_t json_len = 0;
  char *json = read_file(config_path, &json_len);
  if (!json) {
    fprintf(stderr, "Failed to read file: %s\n", config_path);
    return 1;
  }

  jsmn_parser parser;
  jsmntok_t *tokens = NULL;
  int tok_count = 0;
  if (ensure_tokens(&parser, json, json_len, &tokens, &tok_count) != 0) {
    free(json);
    print_parse_error();
    return 1;
  }

  if (tok_count < 1 || tokens[0].type != JSMN_OBJECT) {
    free(tokens);
    free(json);
    print_parse_error();
    return 1;
  }

  int url_idx = find_object_value(json, tokens, 0, "url");
  if (url_idx < 0) {
    fprintf(stderr, "Missing required field: url\n");
    free(tokens);
    free(json);
    return 1;
  }
  char *url = dup_token_string(json, &tokens[url_idx]);
  if (!url) {
    fprintf(stderr, "Invalid url value.\n");
    free(tokens);
    free(json);
    return 1;
  }

  bool method_provided = false;
  char *method = NULL;
  int method_idx = find_object_value(json, tokens, 0, "method");
  if (method_idx >= 0) {
    method = dup_token_string(json, &tokens[method_idx]);
    if (!method) {
      fprintf(stderr, "Invalid method value.\n");
      free(url);
      free(tokens);
      free(json);
      return 1;
    }
    method_provided = true;
  }

  int payload_idx = find_object_value(json, tokens, 0, "payload");
  char *payload = NULL;
  if (payload_idx >= 0) {
    if (tokens[payload_idx].type == JSMN_STRING) {
      payload = dup_token_string(json, &tokens[payload_idx]);
    } else {
      payload = dup_token_raw(json, &tokens[payload_idx]);
    }
    if (!payload) {
      fprintf(stderr, "Invalid payload value.\n");
      free(method);
      free(url);
      free(tokens);
      free(json);
      return 1;
    }
  }

  int payload_file_idx = find_object_value(json, tokens, 0, "payload_file");
  if (payload_file_idx >= 0) {
    if (payload) {
      fprintf(stderr, "Use only one of payload or payload_file.\n");
      free(payload);
      free(method);
      free(url);
      free(tokens);
      free(json);
      return 1;
    }
    char *payload_path = dup_token_string(json, &tokens[payload_file_idx]);
    if (!payload_path) {
      fprintf(stderr, "Invalid payload_file value.\n");
      free(method);
      free(url);
      free(tokens);
      free(json);
      return 1;
    }
    size_t payload_len = 0;
    payload = read_file(payload_path, &payload_len);
    if (!payload) {
      fprintf(stderr, "Failed to read payload_file: %s\n", payload_path);
      free(payload_path);
      free(method);
      free(url);
      free(tokens);
      free(json);
      return 1;
    }
    free(payload_path);
  }

  if (!method_provided) {
    if (payload) {
      method = strdup("POST");
    } else {
      method = strdup("GET");
    }
  }
  if (!method) {
    fprintf(stderr, "Failed to set method.\n");
    free(payload);
    free(url);
    free(tokens);
    free(json);
    return 1;
  }

  if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
    fprintf(stderr, "Failed to init curl globals.\n");
    free(method);
    free(payload);
    free(url);
    free(tokens);
    free(json);
    return 1;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "Failed to init curl.\n");
    curl_global_cleanup();
    free(method);
    free(payload);
    free(url);
    free(tokens);
    free(json);
    return 1;
  }

  bool has_query = strchr(url, '?') != NULL;
  struct curl_slist *headers = NULL;
  struct curl_ctx ctx = {
    .curl = curl,
    .url = &url,
    .has_query = &has_query,
    .headers = &headers
  };

  int path_idx = find_object_value(json, tokens, 0, "path_params");
  if (iterate_kv(json, tokens, path_idx, "path_params", apply_path_param, &ctx) != 0) {
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    free(method);
    free(payload);
    free(url);
    free(tokens);
    free(json);
    return 1;
  }

  int query_idx = find_object_value(json, tokens, 0, "query_params");
  if (iterate_kv(json, tokens, query_idx, "query_params", apply_query_param, &ctx) != 0) {
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    free(method);
    free(payload);
    free(url);
    free(tokens);
    free(json);
    return 1;
  }

  int headers_idx = find_object_value(json, tokens, 0, "headers");
  if (iterate_kv(json, tokens, headers_idx, "headers", apply_header, &ctx) != 0) {
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    free(method);
    free(payload);
    free(url);
    free(tokens);
    free(json);
    return 1;
  }

  struct response_buffer body = {0};
  struct response_headers resp_headers = {0};

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  if (!use_exit_codes && include_headers) {
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp_headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_buffer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  } else if (use_exit_codes) {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_discard);
  } else {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_stdout);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
  }

  if (payload) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(payload));
  }

  CURLcode res = curl_easy_perform(curl);
  long http_status = 0;
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
  }
  if (res != CURLE_OK) {
    fprintf(stderr, "\nRequest failed: %s\n", curl_easy_strerror(res));
  }

  if (!use_exit_codes && include_headers && res == CURLE_OK) {
    print_json_response(http_status, resp_headers.status_line, &resp_headers.headers,
                        &body);
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  curl_global_cleanup();
  free(body.data);
  header_list_free(&resp_headers.headers);
  free(resp_headers.status_line);
  free(method);
  free(payload);
  free(url);
  free(tokens);
  free(json);

  if (!use_exit_codes) {
    return res == CURLE_OK ? 0 : 1;
  }
  if (res != CURLE_OK) {
    return 2;
  }
  if (http_status >= 400) {
    return 3;
  }
  return 0;
}
