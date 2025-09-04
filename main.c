// File: main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>
#include <jansson.h>

// Struct to hold the response data from libcurl
struct MemoryStruct {
  char *memory;
  size_t size;
};

// NATIVE: Function to read free memory directly from /proc/meminfo
int get_free_ram_mb() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp == NULL) {
        perror("Could not open /proc/meminfo");
        return -1;
    }
    char line[256];
    long mem_free_kb = -1;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemAvailable: %ld kB", &mem_free_kb) == 1) {
            break;
        }
    }
    fclose(fp);
    return (mem_free_kb != -1) ? mem_free_kb / 1024 : -1;
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

void trim_string(char *str) {
    if (str == NULL || *str == '\0') {
        return;
    }
    char *start = str;
    while (isspace((unsigned char)*start) || *start == '\"') start++;
    char *end = str + strlen(str) - 1;
    while (end > start && (isspace((unsigned char)*end) || *end == '\"')) end--;
    end[1] = '\0';
    memmove(str, start, end - start + 2);
}

int main(void) {
    const char* api_url = "http://192.168.50.5:11434/api/generate";
    CURL *curl;
    CURLcode res;
    char user_prompt[256];

    #define HISTORY_SIZE 2
    char history_user[HISTORY_SIZE][256] = {0};
    char history_ai[HISTORY_SIZE][512] = {0};
    int history_index = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    printf("AI Shell (Final Version). Type 'exit' to quit.\n");
    printf("Connecting to Ollama at: %s\n", api_url);

    while (1) {
        printf("> ");
        if (!fgets(user_prompt, sizeof(user_prompt), stdin)) {
            break;
        }
        user_prompt[strcspn(user_prompt, "\n")] = 0;
        if (strcmp(user_prompt, "exit") == 0) {
            break;
        }
        curl = curl_easy_init();
        if(curl) {
            struct MemoryStruct chunk = { .memory = malloc(1), .size = 0 };
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");

            json_t *json_body = json_object();
            json_object_set_new(json_body, "model", json_string("gpt-oss"));
            
            char context_prompt[2048] = {0};
            int current_len = 0;
            for (int i = 0; i < HISTORY_SIZE; ++i) {
                int idx = (history_index + i) % HISTORY_SIZE;
                if (strlen(history_user[idx]) > 0) {
                    current_len += snprintf(context_prompt + current_len, sizeof(context_prompt) - current_len, "User: %s\nAI: %s\n", history_user[idx], history_ai[idx]);
                }
            }

            const char* system_prompt_template = "You are an assistant that replies in JSON. If the user wants to know about free RAM, reply with {\"intent\": \"check_ram\"}. For any other request, reply with a JSON object containing a single 'command' key, like {\"command\": \"ls -l\"}.\n\nPrevious conversation:\n%s\nCurrent user request: %s\n\nResponse:";
            char final_prompt[4096];
            snprintf(final_prompt, sizeof(final_prompt), system_prompt_template, context_prompt, user_prompt);
            
            json_object_set_new(json_body, "prompt", json_string(final_prompt));
            // FIX: Added parentheses to call the function
            json_object_set_new(json_body, "stream", json_false());
            char *json_data = json_dumps(json_body, 0);

            curl_easy_setopt(curl, CURLOPT_URL, api_url);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

            res = curl_easy_perform(curl);
            if(res == CURLE_OK) {
                json_error_t error;
                json_t *initial_parse = json_loads(chunk.memory, 0, &error);
                json_t *root_res = NULL;
                if(initial_parse) {
                    json_t* response_str_obj = json_object_get(initial_parse, "response");
                    if(response_str_obj && json_is_string(response_str_obj)) {
                         root_res = json_loads(json_string_value(response_str_obj), 0, &error);
                    }
                }
                
                if (root_res) {
                    json_t *intent_obj = json_object_get(root_res, "intent");
                    json_t *command_obj = json_object_get(root_res, "command");

                    if (intent_obj && json_is_string(intent_obj) && strcmp(json_string_value(intent_obj), "check_ram") == 0) {
                        int free_ram = get_free_ram_mb();
                        if (free_ram != -1) {
                            printf("Native Check: Free RAM is approximately %d MB.\n", free_ram);
                        } else {
                            printf("Native Check failed.\n");
                        }
                    } else if (command_obj && json_is_string(command_obj)) {
                        char command_buffer[512];
                        strncpy(command_buffer, json_string_value(command_obj), sizeof(command_buffer) - 1);
                        command_buffer[sizeof(command_buffer) - 1] = '\0';
                        trim_string(command_buffer);
                        
                        printf("Suggested command: \033[1;33m%s\033[0m\n", command_buffer);
                        printf("Execute? [y/N] ");
                        char confirmation[10];
                        if (fgets(confirmation, sizeof(confirmation), stdin)) {
                            if (confirmation[0] == 'y' || confirmation[0] == 'Y') {
                                printf("...Executing command...\n");
                                strncpy(history_user[history_index], user_prompt, sizeof(history_user[0]));
                                history_user[history_index][sizeof(history_user[0]) - 1] = '\0';
                                strncpy(history_ai[history_index], command_buffer, sizeof(history_ai[0]));
                                history_ai[history_index][sizeof(history_ai[0]) - 1] = '\0';
                                history_index = (history_index + 1) % HISTORY_SIZE;
                                
                                int return_code = system(command_buffer);
                                if (return_code != 0) {
                                     fprintf(stderr, "Command finished with a non-zero exit code.\n");
                                }
                                printf("...Done.\n");
                            }
                        }
                    } else {
                         printf("Error: AI response was valid JSON but did not contain 'intent' or 'command' key.\n");
                    }
                    json_decref(root_res);
                } else {
                    printf("Error: Could not parse JSON from AI response. Raw text: %s\n", chunk.memory);
                }

                if(initial_parse) {
                    json_decref(initial_parse);
                }
            } else {
                fprintf(stderr, "Connection failed: %s\n", curl_easy_strerror(res));
            }
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            free(chunk.memory);
            free(json_data);
            json_decref(json_body);
        }
    }
    curl_global_cleanup();
    return 0;
}
