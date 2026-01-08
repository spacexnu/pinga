# pinga

A tiny C CLI to execute HTTP requests from a JSON file. The idea is to keep it simple: run from the terminal and store request configs as files.

## Why

I built this mainly because heavy tools like Postman are overkill for my day-to-day use. I rarely touch most features, and I wanted a lightweight CLI that fits into my workflow and version control. The second reason is that I was rusty in C and wanted a practical way to shake the dust off.

### Why “pinga”?

Pinga is Brazilian slang for cachaça: cheap, fast, and effective.

This CLI shares the same philosophy.
No UI. No workspaces. No nonsense.

_And honestly?
I’d rather drink a lot of pinga than open heavy tools like Postman._

## Features

- Read request config from a JSON file
- Supports method, headers, query params, path params, and body
- `payload` can be a string or any JSON value
- `payload_file` lets you send body from a file
- Simple CLI output: prints response body to stdout (pipe to `jq` for pretty JSON)

## Quick start

```bash
cmake -S . -B build
cmake --build build
./build/pinga config.example.json
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

Or:

```bash
make build
```

## Usage

```bash
./build/pinga config.json
```

Pretty-print JSON output:

```bash
./build/pinga config.json | jq
```

Exit codes (with `--silent`, suppresses response body output):

- `0` success (HTTP status < 400)
- `1` config/JSON error
- `2` network error
- `3` HTTP error (>= 400)

Silent run (no response body output):

```bash
./build/pinga --silent config.json
```

Quick example (uses httpbin.org):

```bash
make run
```

Tests (network optional):

```bash
make test
```

To include the network test (httpbin):

```bash
ENABLE_NETWORK_TESTS=1 make test
```

Local test without network (mock):

```bash
make test-mock
```

Included files:

- `config.httpbin.json` uses `payload_file` with `payload.example.json`.

## JSON reference

Accepts `headers`, `query_params`, and `path_params` as object or array.

### Fields

| Field | Type | Required | Notes |
| --- | --- | --- | --- |
| `url` | string | yes | Base URL, supports `{name}` placeholders for path params |
| `method` | string | no | Defaults to `GET`, or `POST` if a body is present |
| `headers` | object or array | no | Map or list of `{name,value}` pairs |
| `query_params` | object or array | no | Map or list of `{name,value}` pairs |
| `path_params` | object or array | no | Map or list of `{name,value}` pairs |
| `payload` | string or JSON | no | If JSON, the raw JSON is sent as body |
| `payload_file` | string | no | File path to load body from (mutually exclusive with `payload`) |

### Full example (object)

```json
{
  "url": "https://api.example.com/users/{id}",
  "method": "POST",
  "headers": {
    "Content-Type": "application/json",
    "X-Trace-Id": "abc-123"
  },
  "path_params": {
    "id": "42"
  },
  "query_params": {
    "verbose": "true",
    "limit": "10"
  },
  "payload": {
    "name": "Ada",
    "role": "admin"
  }
}
```

### Example (array)

```json
{
  "url": "https://api.example.com/search",
  "method": "GET",
  "headers": [
    { "name": "Accept", "value": "application/json" }
  ],
  "query_params": [
    { "name": "q", "value": "pinga" },
    { "name": "page", "value": "1" }
  ]
}
```

## Rules

- `url` is required.
- `method` is optional. Without `payload`, it uses `GET`. With `payload`, it uses `POST`.
- `payload` accepts string or JSON (object/array/primitive). If JSON, the raw value is sent as-is.
- `payload_file` is optional. If present, it sends the file contents as the body.
- use only one of `payload` or `payload_file`.
- `headers`, `query_params`, `path_params` accept:
  - object: `{ "key": "value" }`
  - array: `[{ "name": "key", "value": "value" }]`
- values for `headers`, `query_params`, `path_params` must be strings.

## FAQ

**Does it support HTTPS?**  
Yes, it uses libcurl and supports HTTPS out of the box.

**Can I send JSON objects as body?**  
Yes. Set `payload` to any JSON value (object/array/primitive) and it will be sent as the raw JSON body.

**How do I load a body from a file?**  
Use `payload_file` with a path to your file. It is mutually exclusive with `payload`.

**Can I store request configs in git?**  
That is the main idea. The JSON files are designed to be committed and shared.

## Limitations

- No built-in auth helpers yet (bearer/basic). Use headers for now.
- No redirects or retry settings exposed.
- Body is sent as-is; no automatic JSON formatting or validation.

___

_Simple tools. Simple configs. No hangover._