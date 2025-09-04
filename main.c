// File: main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <jansson.h>

// Struct to hold the response data from libcurl
struct MemoryStruct {
  char *memory;
  size_t size;
};

// Callback function for libcurl to write received data into our struct
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

// Function to trim leading/trailing whitespace and quotes from a string
void trim_string(char *str) {
    char *start = str;
    while (isspace((unsigned char)*start) || *start == '\"') start++;

    char *end = str + strlen(str) - 1;
    while (end > start && (isspace((unsigned char)*end) || *end == '\"')) end--;

    end[1] = '\0';
    memmove(str, start, end - start + 2);
}


int main(void) {
    CURL *curl;
    CURLcode res;
    char user_prompt[256];

    curl_global_init(CURL_GLOBAL_ALL);

    printf("AI Shell (Phase 1: Translator). Type 'exit' to quit.\n");

    while (1) {
        printf("> ");
        if (!fgets(user_prompt, sizeof(user_prompt), stdin)) {
            break;
        }
        
        // Remove trailing newline
        user_prompt[strcspn(user_prompt, "\n")] = 0;

        if (strcmp(user_prompt, "exit") == 0) {
            break;
        }

        curl = curl_easy_init();
        if(curl) {
            struct MemoryStruct chunk = { .memory = malloc(1), .size = 0 };
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");

            // --- Construct JSON payload ---
            json_t *json_body = json_object();
            json_object_set_new(json_body, "model", json_string("codellama")); // Or another model like llama3
            // This is the prompt engineering part
            const char* system_prompt_template = "You are a helpful assistant that translates natural language into a single, safe, and syntactically correct Linux shell command. Do not provide any explanation, preamble, or markdown formatting. Just return the command.\n\nUser request: %s\n\nCommand:";
            char final_prompt[512];
            snprintf(final_prompt, sizeof(final_prompt), system_prompt_template, user_prompt);
            json_object_set_new(json_body, "prompt", json_string(final_prompt));
            json_object_set_new(json_body, "stream", json_false());
            char *json_data = json_dumps(json_body, 0);

            // --- Set CURL options ---
            curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:11434/api/generate");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

            // --- Perform request ---
            res = curl_easy_perform(curl);
            if(res == CURLE_OK) {
                json_error_t error;
                json_t *root_res = json_loads(chunk.memory, 0, &error);
                if (root_res) {
                    json_t *response_obj = json_object_get(root_res, "response");
                    if (json_is_string(response_obj)) {
                        char command_buffer[512];
                        strncpy(command_buffer, json_string_value(response_obj), sizeof(command_buffer) - 1);
                        command_buffer[sizeof(command_buffer) - 1] = '\0';
                        
                        trim_string(command_buffer);
                        printf("Suggested command: \033[1;33m%s\033[0m\n", command_buffer);

                    } else {
                        printf("Error: Could not find 'response' string in JSON.\n");
                    }
                    json_decref(root_res);
                } else {
                    printf("Error: Could not parse JSON response.\n");
                }
            } else {
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            }

            // --- Cleanup ---
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
