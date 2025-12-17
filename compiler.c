// pgt.c - PGT Toolchain v0.5.0 (окончательно исправлена генерация строк)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>

#define MAX_CODE 1000000

// === Типы токенов ===
typedef enum {
    TOK_EOF, TOK_ID, TOK_STRING, TOK_EQUAL, TOK_EQEQ, TOK_PRINT, TOK_GIVE,
    TOK_IF, TOK_ELSE, TOK_FUNCTION, TOK_CLASS, TOK_IMPORT,
    TOK_LBRACE, TOK_RBRACE, TOK_LPAREN, TOK_RPAREN, TOK_COLONCOLON, TOK_EXIT,
    TOK_COMMA
} TokenType;

typedef struct {
    TokenType type;
    char value[256];
} Token;

// Прототипы
void code(const char* fmt, ...);
void next_token();
void expect(TokenType t);
void execute_block(int compile_mode);
void execute_statement(int compile_mode);
void process(const char* filename, int compile_mode, int debug_mode);

// === Генерация C-кода ===
char generated_c[MAX_CODE] = {0};
int indent_level = 0;

void code(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char temp[8192];
    vsnprintf(temp, sizeof(temp), fmt, args);
    va_end(args);

    for (int i = 0; i < indent_level; i++) {
        strncat(generated_c, "    ", MAX_CODE - strlen(generated_c) - 1);
    }
    strncat(generated_c, temp, MAX_CODE - strlen(generated_c) - 1);
    size_t len = strlen(generated_c);
    if (len < MAX_CODE - 1) {
        generated_c[len] = '\n';
        generated_c[len + 1] = '\0';
    }
}

// === Переменные для run ===
typedef struct {
    char* key;
    char* str_val;
} Value;

Value* variables = NULL;
int var_count = 0;
int var_capacity = 0;

Value* find_var(const char* name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(variables[i].key, name) == 0) return &variables[i];
    }
    return NULL;
}

void set_var(const char* name, const char* value) {
    Value* v = find_var(name);
    if (v) { free(v->str_val); v->str_val = strdup(value); }
    else {
        if (var_count >= var_capacity) {
            var_capacity = var_capacity ? var_capacity * 2 : 100;
            variables = realloc(variables, var_capacity * sizeof(Value));
            if (!variables) exit(1);
        }
        variables[var_count].key = strdup(name);
        variables[var_count++].str_val = strdup(value);
    }
}

void print_var(const char* name) {
    Value* v = find_var(name);
    printf("%s\n", v && v->str_val ? v->str_val : "(undefined)");
}

void free_vars() {
    for (int i = 0; i < var_count; i++) {
        free(variables[i].key);
        free(variables[i].str_val);
    }
    free(variables);
}

// === Лексер ===
char *source, *pos;
Token current;

void next_token() {
    while (isspace(*pos)) pos++;
    if (*pos == '\0') { current.type = TOK_EOF; return; }

    if (strncmp(pos, "//", 2) == 0) {
        while (*pos && *pos != '\n') pos++;
        next_token();
        return;
    }

    if (*pos == '"') {
        pos++;
        int i = 0;
        while (*pos && *pos != '"') {
            current.value[i++] = *pos++;
        }
        if (*pos == '"') pos++;
        current.value[i] = '\0';
        current.type = TOK_STRING;
        return;
    }

    if (strncmp(pos, "print", 5) == 0 && !isalnum(pos[5])) { current.type = TOK_PRINT; pos += 5; return; }
    if (strncmp(pos, "give", 4) == 0 && !isalnum(pos[4])) { current.type = TOK_GIVE; pos += 4; return; }
    if (strncmp(pos, "function", 8) == 0 && !isalnum(pos[8])) { current.type = TOK_FUNCTION; pos += 8; return; }
    if (strncmp(pos, "Class", 5) == 0 && !isalnum(pos[5])) { current.type = TOK_CLASS; pos += 5; return; }
    if (strncmp(pos, "import", 6) == 0 && !isalnum(pos[6])) { current.type = TOK_IMPORT; pos += 6; return; }
    if (strncmp(pos, "exit::();", 9) == 0) { current.type = TOK_EXIT; pos += 9; return; }

    if (*pos == '{') { current.type = TOK_LBRACE; pos++; return; }
    if (*pos == '}') { current.type = TOK_RBRACE; pos++; return; }
    if (*pos == '(') { current.type = TOK_LPAREN; pos++; return; }
    if (*pos == ')') { current.type = TOK_RPAREN; pos++; return; }
    if (*pos == ',') { current.type = TOK_COMMA; pos++; return; }
    if (*pos == '=') { pos++; current.type = (*pos == '=') ? (pos++, TOK_EQEQ) : TOK_EQUAL; return; }
    if (strncmp(pos, "::", 2) == 0) { current.type = TOK_COLONCOLON; pos += 2; return; }

    int i = 0;
    while (isalnum(*pos) || *pos == '_') current.value[i++] = *pos++;
    current.value[i] = '\0';
    if (i > 0) { current.type = TOK_ID; return; }

    pos++;
}

void expect(TokenType t) {
    if (current.type != t) {
        fprintf(stderr, "Ошибка: ожидался токен %d, получен %d ('%s')\n", t, current.type, current.value);
        exit(1);
    }
    next_token();
}

// Функция для экранирования строк в C
char* escape_c_string(const char* input) {
    static char escaped[1024];
    int j = 0;
    for (int i = 0; input[i] && j < 1022; i++) {
        switch (input[i]) {
            case '"': escaped[j++] = '\\'; escaped[j++] = '"'; break;
            case '\\': escaped[j++] = '\\'; escaped[j++] = '\\'; break;
            case '\n': escaped[j++] = '\\'; escaped[j++] = 'n'; break;
            case '\t': escaped[j++] = '\\'; escaped[j++] = 't'; break;
            default: escaped[j++] = input[i]; break;
        }
    }
    escaped[j] = '\0';
    return escaped;
}

// === Парсер ===
void execute_print(int compile_mode) {
    expect(TOK_PRINT);
    expect(TOK_LPAREN);
    if (current.type == TOK_STRING) {
        if (compile_mode) code("printf(\"%s\\n\");", escape_c_string(current.value));
        else printf("%s\n", current.value);
        next_token();
    } else if (current.type == TOK_ID) {
        if (compile_mode) code("printf(\"%%s\\n\", %s);", current.value);
        else print_var(current.value);
        next_token();
    }
    expect(TOK_RPAREN);
}

void execute_file_call(const char* func_name, const char* assign_var, int compile_mode) {
    expect(TOK_LPAREN);

    char arg1[256] = "";
    char arg2[256] = "r";  // значение по умолчанию
    
    // Парсим первый аргумент
    if (current.type == TOK_STRING) {
        strcpy(arg1, current.value);
        next_token();
    } else if (current.type == TOK_ID) {
        strcpy(arg1, current.value);
        next_token();
    }

    // Парсим второй аргумент (если есть)
    if (current.type == TOK_COMMA) {
        expect(TOK_COMMA);
        if (current.type == TOK_STRING) {
            strcpy(arg2, current.value);
            next_token();
        } else if (current.type == TOK_ID) {
            strcpy(arg2, current.value);
            next_token();
        }
    }

    expect(TOK_RPAREN);

    // Отладочный вывод для проверки аргументов
    if (compile_mode) {
        printf("// DEBUG: func=%s, arg1='%s', arg2='%s'\n", func_name, arg1, arg2);
    }

    if (strcmp(func_name, "file_open") == 0) {
        if (compile_mode) {
            char* escaped_arg1 = escape_c_string(arg1);
            char* escaped_arg2 = escape_c_string(arg2);
            code("FILE* %s = fopen(\"%s\", \"%s\");", assign_var ? assign_var : "handle", escaped_arg1, escaped_arg2);
            code("if (%s == NULL) { perror(\"fopen\"); exit(1); }", assign_var ? assign_var : "handle");
        } else {
            const char* path = arg1[0] == '"' ? arg1 + 1 : arg1;
            path = path[strlen(path)-1] == '"' ? path : path;
            const char* mode = arg2[0] == '"' ? arg2 + 1 : "r";
            mode = mode[strlen(mode)-1] == '"' ? mode : mode;
            FILE* f = fopen(path, mode);
            if (!f) { perror("fopen"); exit(1); }
            char handle_str[32];
            sprintf(handle_str, "%p", (void*)f);
            if (assign_var) set_var(assign_var, handle_str);
            else set_var("handle", handle_str);
        }
    } else if (strcmp(func_name, "file_write") == 0) {
        if (compile_mode) {
            code("fprintf(%s, \"%s\");", assign_var ? assign_var : "handle", escape_c_string(arg2));
        } else {
            Value* v = find_var(assign_var ? assign_var : "handle");
            if (!v) { fprintf(stderr, "Handle не найден\n"); exit(1); }
            FILE* f = (FILE*)strtoul(v->str_val, NULL, 16);
            const char* text = arg2[0] == '"' ? arg2 + 1 : arg2;
            if (text[strlen(text)-1] == '"') {
                char *clean = strndup(text, strlen(text)-1);
                fprintf(f, "%s", clean);
                free(clean);
            } else {
                fprintf(f, "%s", text);
            }
        }
    } else if (strcmp(func_name, "file_close") == 0) {
        if (compile_mode) {
            code("fclose(%s);", assign_var ? assign_var : "handle");
        } else {
            Value* v = find_var(assign_var ? assign_var : "handle");
            if (!v) return;
            FILE* f = (FILE*)strtoul(v->str_val, NULL, 16);
            fclose(f);
        }
    } else if (strcmp(func_name, "create_file") == 0) {
        // create_file(mode) - создает временный файл с автоматическим именем
        if (compile_mode) {
            code("char temp_filename[] = \"/tmp/pgt_temp_XXXXXX\";");
            code("int fd = mkstemp(temp_filename);");
            code("if (fd == -1) { perror(\"mkstemp\"); exit(1); }");
            code("close(fd);");
            code("FILE* %s = fopen(temp_filename, \"%s\");", assign_var ? assign_var : "handle", escape_c_string(arg1));
            code("if (%s == NULL) { perror(\"fopen\"); exit(1); }", assign_var ? assign_var : "handle");
        } else {
            char temp_filename[] = "/tmp/pgt_temp_XXXXXX";
            int fd = mkstemp(temp_filename);
            if (fd == -1) { perror("mkstemp"); exit(1); }
            close(fd);
            
            const char* mode = arg1[0] == '"' ? arg1 + 1 : arg1;
            mode = mode[strlen(mode)-1] == '"' ? mode : mode;
            FILE* f = fopen(temp_filename, mode);
            if (!f) { perror("fopen"); exit(1); }
            
            char handle_str[32];
            sprintf(handle_str, "%p", (void*)f);
            if (assign_var) set_var(assign_var, handle_str);
            else set_var("handle", handle_str);
        }
    }
}

void execute_statement(int compile_mode) {
    if (current.type == TOK_PRINT) {
        execute_print(compile_mode);
    } else if (current.type == TOK_ID) {
        char name[256];
        strcpy(name, current.value);
        next_token();

        if (current.type == TOK_EQUAL) {
            next_token();  // =
            if (current.type == TOK_ID) {
                char func_name[256];
                strcpy(func_name, current.value);
                next_token();
                execute_file_call(func_name, name, compile_mode);
            }
        } else if (current.type == TOK_LPAREN) {
            execute_file_call(name, NULL, compile_mode);
        }
    } else if (current.type == TOK_GIVE) {
        expect(TOK_GIVE);
        expect(TOK_LPAREN); expect(TOK_RPAREN);
        expect(TOK_COLONCOLON);
        expect(TOK_LBRACE);

        if (compile_mode) {
            code("int main() {");
            indent_level++;
        }

        execute_block(compile_mode);

        if (compile_mode) {
            indent_level--;
            code("    return 0;");
            code("}");
        }
        expect(TOK_RBRACE);
    } else if (current.type == TOK_FUNCTION || current.type == TOK_CLASS) {
        next_token();
        expect(TOK_LPAREN);
        next_token();
        expect(TOK_RPAREN);
        expect(TOK_LBRACE);
        execute_block(compile_mode);
    } else if (current.type == TOK_IMPORT || current.type == TOK_EXIT) {
        next_token();
    } else {
        next_token();
    }
}

void execute_block(int compile_mode) {
    while (current.type != TOK_RBRACE && current.type != TOK_EOF) {
        execute_statement(compile_mode);
    }
}

void process(const char* filename, int compile_mode, int debug_mode) {
    FILE* f = fopen(filename, "r");
    if (!f) { perror("Файл не найден"); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    source = malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);

    pos = source;
    next_token();

    if (compile_mode) {
        generated_c[0] = '\0';
        code("#include <stdio.h>");
        code("#include <stdlib.h>");
        code("#include <unistd.h>");
        if (debug_mode) {
            code("#define DEBUG_MODE 1");
            code("#define DEBUG_PRINT(fmt, ...) printf(\"[DEBUG] \" fmt \"\\n\", ##__VA_ARGS__)");
        } else {
            code("#define DEBUG_MODE 0");
            code("#define DEBUG_PRINT(fmt, ...) do {} while(0)");
        }
        code("");
    }

    while (current.type != TOK_EOF) {
        execute_statement(compile_mode);
    }

    if (compile_mode) {
        if (strstr(generated_c, "int main()") == NULL) {
            code("int main() { return 0; }");
        }

        // Создаем структуру папок build/src и build/bin
        system("mkdir -p build/src");
        system("mkdir -p build/bin");

        // Получаем имя файла без пути
        const char* basename = strrchr(filename, '/');
        if (!basename) basename = filename;
        else basename++; // пропускаем '/'

        char cfile[256], bin[256];
        snprintf(cfile, sizeof(cfile), "build/src/%s.c", basename);
        snprintf(bin, sizeof(bin), "build/bin/%s.bin", basename);

        FILE* out = fopen(cfile, "w");
        fprintf(out, "%s", generated_c);
        fclose(out);

        char cmd[512];
        snprintf(cmd, sizeof(cmd), "gcc \"%s\" -o \"%s\"", cfile, bin);
        printf("Генерация: %s\nКомпилируем: %s\n", cfile, cmd);
        int ret = system(cmd);
        if (ret == 0) printf("Готово! Бинарник: %s\n", bin);
        else printf("Ошибка gcc\n");
    }

    free(source);
    if (!compile_mode) free_vars();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Использование: %s [run|build] <file.pgt> [--debug]\n", argv[0]);
        return 1;
    }

    static struct option long_options[] = {
        {"version", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {"debug", no_argument, 0, 'd'},
        {0, 0, 0, 0}
    };

    int debug_mode = 0;
    int opt;
    while ((opt = getopt_long(argc, argv, "vhd", long_options, NULL)) != -1) {
        switch (opt) {
            case 'v':
                printf("PGT Toolchain v0.5.0\n");
                return 0;
            case 'h':
                printf("run <file> - интерпретировать\nbuild <file> - компилировать в бинарник\n--debug - компиляция с дебаг версией\nhelp не правельно правельно так - h и да\nhelp - показиваеть список доступных команд\nversion не правельно правельно так -v и да\nversion ");
                return 0;
            case 'd':
                debug_mode = 1;
                break;
            default:
                return 1;
        }
    }

    const char* command = argv[optind];
    const char* filename = (optind + 1 < argc) ? argv[optind + 1] : NULL;

    if (!filename) {
        fprintf(stderr, "Ожидался файл\n");
        return 1;
    }

    if (strcmp(command, "run") == 0) {
        process(filename, 0, debug_mode);
    } else if (strcmp(command, "build") == 0) {
        process(filename, 1, debug_mode);
    } else {
        fprintf(stderr, "Неизвестная команда\n");
        return 1;
    }

    return 0;
}