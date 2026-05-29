/*
 * server.c - Study Planner HTTP Server (Windows / WinSock2)
 *
 * Compile:
 *   gcc backend/c/server.c -o bin/server.exe -lws2_32 -Wall
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT          8080
#define BACKLOG       8
#define BUF_SIZE      65536
#define FILE_BUF_SIZE (1 << 20)
#define MAX_BODY      (1 << 17)

#define DATA_INPUT    "data/input.txt"
#define DATA_OUTPUT   "data/output.json"
#define DATA_STATE    "data/state.json"
#define CHAT_INPUT    "data/chat/chat_input.json"
#define CHAT_OUTPUT   "data/chat/chat_output.json"
#define FRONTEND_DIR  "frontend"

static char *read_file(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    long len;
    char *buf;
    size_t r;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    rewind(f);

    if (len < 0 || len > FILE_BUF_SIZE) {
        fclose(f);
        return NULL;
    }

    buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    r = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[r] = '\0';
    if (out_len) *out_len = (long)r;
    return buf;
}

static int write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(data, 1, len, f);
    fclose(f);
    return 1;
}

static int run_study_exe(int replan) {
    char exe_path[MAX_PATH];
    char project_root[MAX_PATH];
    char cmd[MAX_PATH + 32];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;

    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    {
        char *last_slash = strrchr(exe_path, '\\');
        if (last_slash) strcpy(last_slash + 1, "study.exe");
        else strcpy(exe_path, "bin\\study.exe");
    }

    GetCurrentDirectoryA(MAX_PATH, project_root);
    snprintf(cmd, sizeof(cmd), "\"%s\"%s", exe_path, replan ? " --replan" : "");

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    if (!CreateProcessA(exe_path, cmd, NULL, NULL, FALSE, 0, NULL,
                        project_root, &si, &pi)) {
        return -1;
    }

    WaitForSingleObject(pi.hProcess, 15000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

static int launch_stopwatch(void) {
    char exe_path[MAX_PATH];
    char project_root[MAX_PATH];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    BOOL ok;

    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    {
        char *last_slash = strrchr(exe_path, '\\');
        if (last_slash) strcpy(last_slash + 1, "stopwatch.exe");
        else strcpy(exe_path, "bin\\stopwatch.exe");
    }

    GetCurrentDirectoryA(MAX_PATH, project_root);
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    ok = CreateProcessA(exe_path, NULL, NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, project_root, &si, &pi);
    if (!ok) return 0;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 1;
}

static int run_assistant_python(void) {
    char project_root[MAX_PATH];
    char cmd[512];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;

    GetCurrentDirectoryA(MAX_PATH, project_root);
    snprintf(cmd, sizeof(cmd), "python backend\\python\\assistant_api.py");

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL,
                        project_root, &si, &pi)) {
        return -1;
    }

    if (WaitForSingleObject(pi.hProcess, 120000) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return -2;
    }

    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}


static void send_response(SOCKET s, int status_code, const char *status_text,
                          const char *content_type, const char *body,
                          size_t body_len) {
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text, content_type, (unsigned long)body_len);

    send(s, header, hlen, 0);
    if (body && body_len > 0) send(s, body, (int)body_len, 0);
}

static void send_json(SOCKET s, int code, const char *status, const char *json) {
    send_response(s, code, status, "application/json; charset=utf-8",
                  json, strlen(json));
}

static void send_error(SOCKET s, int code, const char *status, const char *msg) {
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg);
    send_json(s, code, status, buf);
}

static char *json_escape_alloc(const char *raw, long len) {
    char *escaped = (char *)malloc((size_t)len * 2 + 8);
    int ei = 0;

    if (!escaped) return NULL;
    for (int i = 0; i < len; i++) {
        if (raw[i] == '"') {
            escaped[ei++] = '\\';
            escaped[ei++] = '"';
        } else if (raw[i] == '\\') {
            escaped[ei++] = '\\';
            escaped[ei++] = '\\';
        } else if (raw[i] == '\n') {
            escaped[ei++] = '\\';
            escaped[ei++] = 'n';
        } else if (raw[i] == '\r') {
        } else {
            escaped[ei++] = raw[i];
        }
    }
    escaped[ei] = '\0';
    return escaped;
}

static void send_schedule_payload(SOCKET client, const char *missing_msg) {
    long flen = 0;
    char *raw = read_file(DATA_OUTPUT, &flen);
    char *escaped;
    char *resp;
    size_t resp_size;

    if (!raw) {
        send_error(client, 404, "Not Found", missing_msg);
        return;
    }

    escaped = json_escape_alloc(raw, flen);
    resp_size = (size_t)flen * 2 + 80;
    resp = (char *)malloc(resp_size);
    if (!escaped || !resp) {
        free(raw);
        free(escaped);
        free(resp);
        send_error(client, 500, "Internal Server Error", "OOM");
        return;
    }

    snprintf(resp, resp_size, "{\"schedule\":%s,\"raw\":\"%s\"}", raw, escaped);
    send_json(client, 200, "OK", resp);
    free(resp);
    free(escaped);
    free(raw);
}

static int json_get_string(const char *src, const char *key, char *out, int out_sz) {
    char search[128];
    const char *p;
    int i = 0;

    snprintf(search, sizeof(search), "\"%s\":", key);
    p = strstr(src, search);
    if (!p) return 0;
    p += strlen(search);

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return 0;
    p++;

    while (*p && *p != '"' && i < out_sz - 1) {
        if (*p == '\\') {
            p++;
            if (*p == 'n') out[i++] = '\n';
            else if (*p == 'r') out[i++] = '\r';
            else if (*p == 't') out[i++] = '\t';
            else out[i++] = *p;
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return 1;
}

static int parse_content_length(const char *request) {
    const char *p = strstr(request, "Content-Length:");
    if (!p) p = strstr(request, "content-length:");
    if (!p) return 0;
    p += 15;
    return atoi(p);
}

static const char *find_body(const char *request, int req_len) {
    for (int i = 0; i < req_len - 3; i++) {
        if (request[i] == '\r' && request[i + 1] == '\n' &&
            request[i + 2] == '\r' && request[i + 3] == '\n') {
            return request + i + 4;
        }
    }
    return NULL;
}

static const char *content_type_for_path(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    return "application/octet-stream";
}

static int serve_public_file(SOCKET client, const char *path) {
    char local_path[512];
    const char *rel = path;
    long flen = 0;
    char *body;

    if (strcmp(path, "/") == 0) rel = "/index.html";
    if (strstr(rel, "..")) return 0;

    if (strcmp(rel, "/css/shared.css") != 0 && !strstr(rel, ".html")) {
        return 0;
    }

    snprintf(local_path, sizeof(local_path), FRONTEND_DIR "%s", rel);
    for (char *p = local_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }

    body = read_file(local_path, &flen);
    if (!body) return 0;

    send_response(client, 200, "OK", content_type_for_path(local_path),
                  body, (size_t)flen);
    free(body);
    return 1;
}

static void handle_request(SOCKET client) {
    char *req_buf = (char *)calloc(BUF_SIZE + 1, 1);
    int total = 0;
    int n;
    char method[8] = {0};
    char path[256] = {0};
    char *qs;

    printf("[Server] Handling new client connection...\n");
    fflush(stdout);

    if (!req_buf) {
        closesocket(client);
        return;
    }

    while (total < BUF_SIZE) {
        printf("[Server] Calling recv()...\n");
        fflush(stdout);
        n = recv(client, req_buf + total, BUF_SIZE - total, 0);
        printf("[Server] recv() returned %d bytes\n", n);
        fflush(stdout);
        if (n <= 0) break;
        total += n;
        req_buf[total] = '\0';
        
        printf("[Server] Current req_buf total length: %d\n", total);
        fflush(stdout);

        if (strstr(req_buf, "\r\n\r\n")) {
            int clen = parse_content_length(req_buf);
            const char *body_start = find_body(req_buf, total);
            int body_received = body_start ? (int)(req_buf + total - body_start) : 0;
            printf("[Server] Found separator. clen=%d, body_received=%d\n", clen, body_received);
            fflush(stdout);
            if (clen <= 0 || body_received >= clen) {
                printf("[Server] Break condition met. Breaking recv loop.\n");
                fflush(stdout);
                break;
            }
        }
    }

    printf("[Server] Parsing request method and path...\n");
    fflush(stdout);
    sscanf(req_buf, "%7s %255s", method, path);
    printf("[Server] Method: %s, Path: %s\n", method, path);
    fflush(stdout);
    qs = strchr(path, '?');
    if (qs) *qs = '\0';

    if (strcmp(method, "OPTIONS") == 0) {
        send_response(client, 204, "No Content", "text/plain", NULL, 0);
        goto cleanup;
    }

    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/api/output") == 0) {
            send_schedule_payload(client, "output.json not found");
            goto cleanup;
        }

        if (strcmp(path, "/api/input") == 0) {
            long flen = 0;
            char *raw = read_file(DATA_INPUT, &flen);
            char *escaped;
            char *resp;
            size_t resp_size;

            if (!raw) {
                send_json(client, 200, "OK", "{\"raw\":\"\"}");
                goto cleanup;
            }
            escaped = json_escape_alloc(raw, flen);
            resp_size = (size_t)flen * 2 + 24;
            resp = (char *)malloc(resp_size);
            if (!escaped || !resp) {
                free(raw);
                free(escaped);
                free(resp);
                send_error(client, 500, "Internal Server Error", "OOM");
                goto cleanup;
            }
            snprintf(resp, resp_size, "{\"raw\":\"%s\"}", escaped);
            send_json(client, 200, "OK", resp);
            free(resp);
            free(escaped);
            free(raw);
            goto cleanup;
        }

        if (serve_public_file(client, path)) goto cleanup;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/schedule") == 0) {
        const char *body = find_body(req_buf, total);
        char *input_text;
        int exit_code;

        if (!body || strlen(body) == 0) {
            send_error(client, 400, "Bad Request", "Empty body");
            goto cleanup;
        }

        input_text = (char *)calloc(MAX_BODY, 1);
        if (!input_text) {
            send_error(client, 500, "Internal Server Error", "OOM");
            goto cleanup;
        }

        if (!json_get_string(body, "inputText", input_text, MAX_BODY)) {
            free(input_text);
            send_error(client, 400, "Bad Request", "inputText field missing");
            goto cleanup;
        }

        if (strlen(input_text) == 0) {
            free(input_text);
            send_error(client, 400, "Bad Request", "inputText is empty");
            goto cleanup;
        }

        if (!write_file(DATA_INPUT, input_text, strlen(input_text))) {
            free(input_text);
            send_error(client, 500, "Internal Server Error", "Cannot write data/input.txt");
            goto cleanup;
        }
        free(input_text);

        exit_code = run_study_exe(0);
        if (exit_code != 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "study.exe exited with code %d", exit_code);
            send_error(client, 500, "Internal Server Error", msg);
            goto cleanup;
        }

        send_schedule_payload(client, "study.exe did not produce output.json");
        goto cleanup;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/replan") == 0) {
        const char *body = find_body(req_buf, total);
        int clen = parse_content_length(req_buf);
        int exit_code;

        if (!body || clen <= 0) {
            send_error(client, 400, "Bad Request", "Empty body");
            goto cleanup;
        }

        if (!write_file(DATA_STATE, body, (size_t)clen)) {
            send_error(client, 500, "Internal Server Error", "Cannot write data/state.json");
            goto cleanup;
        }

        exit_code = run_study_exe(1);
        if (exit_code != 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "study.exe --replan exited with code %d", exit_code);
            send_error(client, 500, "Internal Server Error", msg);
            goto cleanup;
        }

        send_schedule_payload(client, "study.exe did not produce output.json");
        goto cleanup;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/stopwatch/launch") == 0) {
        if (launch_stopwatch()) send_json(client, 200, "OK", "{\"ok\":true}");
        else send_error(client, 404, "Not Found", "stopwatch.exe not found or failed to launch");
        goto cleanup;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/chat") == 0) {
        const char *body = find_body(req_buf, total);
        int clen = parse_content_length(req_buf);
        int exit_code;
        long flen = 0;
        char *resp;

        if (!body || clen <= 0) {
            send_error(client, 400, "Bad Request", "Empty body");
            goto cleanup;
        }

        if (!write_file(CHAT_INPUT, body, (size_t)clen)) {
            send_error(client, 500, "Internal Server Error", "Cannot write chat input");
            goto cleanup;
        }

        exit_code = run_assistant_python();
        if (exit_code != 0) {
            resp = read_file(CHAT_OUTPUT, &flen);
            if (resp) {
                send_json(client, 500, "Internal Server Error", resp);
                free(resp);
            } else {
                send_error(client, 500, "Internal Server Error", "Assistant API failed");
            }
            goto cleanup;
        }

        resp = read_file(CHAT_OUTPUT, &flen);
        if (!resp) {
            send_error(client, 500, "Internal Server Error", "Assistant did not produce chat output");
            goto cleanup;
        }

        send_json(client, 200, "OK", resp);
        free(resp);
        goto cleanup;
    }

    send_error(client, 404, "Not Found", "Route not found");

cleanup:
    free(req_buf);
    closesocket(client);
}

int main(void) {
    WSADATA wsa;
    SOCKET server;
    struct sockaddr_in addr;
    int reuse = 1;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }

    server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&reuse, sizeof(reuse));

    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
        closesocket(server);
        WSACleanup();
        return 1;
    }

    if (listen(server, BACKLOG) == SOCKET_ERROR) {
        fprintf(stderr, "listen() failed: %d\n", WSAGetLastError());
        closesocket(server);
        WSACleanup();
        return 1;
    }

    printf("=================================================\n");
    printf("  Study Planner C Server running on port %d\n", PORT);
    printf("  Open http://localhost:%d/ in your browser\n", PORT);
    printf("  Press Ctrl+C to stop.\n");
    printf("=================================================\n");
    fflush(stdout);

    while (1) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET client = accept(server, (struct sockaddr *)&client_addr, &addr_len);
        if (client == INVALID_SOCKET) continue;
        handle_request(client);
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
