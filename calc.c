// Rostyslav Hrabenko 260ADB166
// https://github.com/mintyross/GradedLab-C
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: <every AI tool, website and book you used; details in comments where you used them>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>    // Character classification functions (isdigit, isalpha, etc.)
#include <sys/stat.h> // File system attributes, sizing, and directory creation verification
#include <dirent.h>   // Directory stream opening and reading utilities (DIR, readdir)

#ifdef _WIN32
#include <direct.h>
#define mkdir(A, B) _mkdir(A)
#endif

#define STUDENT_SUFFIX "anna_221234"              // used for folder name
#define STUDENT_FULL_SUFFIX "anna_berzina_221234" // used for file name
#define MAX_PATH 1024                             // maximum destination path length
#define LINE_LEN 256                              // maximum line read length used for capping the reading buffer

typedef enum
{
    TOKEN_EOF,
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_MUL,
    TOKEN_DIV,
    TOKEN_POW,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_EOL
} TokenType;

// Evaluation Val Types
typedef enum
{
    VAL_INT,
    VAL_FLOAT
} ValType;

typedef struct
{
    ValType type;
    union
    {
        long long i_val;
        double f_val;
    };
} Value;

typedef struct
{
    TokenType type;
    Value val;
    long long pos;     // 1-based file position where token starts
    long long end_pos; // position immediately after token
} Token;

// Global parsing/tracking states
const char *file_buf = NULL;
long long file_len = 0;
long long global_idx = 0; // 0-based offset in file_buf

// Output buffering structures to satisfy "All-or-nothing" error requirement
typedef struct
{
    Value val;
} OutputLine;

OutputLine *recorded_outputs = NULL;
int output_count = 0;
int output_capacity = 0;

void record_output(Value v)
{
    if (output_count >= output_capacity)
    {
        output_capacity = output_capacity == 0 ? 1024 : output_capacity * 2;
        recorded_outputs = realloc(recorded_outputs, output_capacity * sizeof(OutputLine));
    }
    recorded_outputs[output_count++].val = v;
}

// Forward Declarations for Recursive Descent Parser
Token get_next_token(void);
Value parse_expression(int min_prec);
Value parse_primary(void);

// Global token lookahead
Token current_token;
int has_error = 0;
long long error_pos = 0;

void trigger_error(long long pos)
{
    if (!has_error)
    {
        has_error = 1;
        error_pos = pos;
    }
}

// Lexer / Tokenizer Implementation
Token get_next_token(void)
{
    Token t;
    t.type = TOKEN_EOF;
    t.pos = global_idx + 1;

    // Handle spaces and tabs, skip comments
    while (global_idx < file_len)
    {
        char c = file_buf[global_idx];
        if (c == ' ' || c == '\t')
        {
            global_idx++;
            continue;
        }
        if (c == '#')
        {
            // Comment: check if it's the first non-blank char of the line
            long int scan = global_idx - 1;
            int first_on_line = 1;
            while (scan >= 0 && file_buf[scan] != '\n')
            {
                if (file_buf[scan] != ' ' && file_buf[scan] != '\t')
                {
                    first_on_line = 0;
                    break;
                }
                scan--;
            }
            if (!first_on_line)
            {
                trigger_error(global_idx + 1); // # anywhere else is invalid
                global_idx++;
                t.type = TOKEN_EOF;
                return t;
            }
            // Skip until EOL or EOF
            while (global_idx < file_len && file_buf[global_idx] != '\n')
            {
                global_idx++;
            }
            continue;
        }
        break;
    }

    t.pos = global_idx + 1;

    if (global_idx >= file_len)
    {
        t.type = TOKEN_EOF;
        t.end_pos = t.pos;
        return t;
    }

    char c = file_buf[global_idx];

    if (c == '\n')
    {
        t.type = TOKEN_EOL;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }
    if (c == '+')
    {
        t.type = TOKEN_PLUS;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }
    if (c == '-')
    {
        t.type = TOKEN_MINUS;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }
    if (c == '*')
    {
        global_idx++;
        if (global_idx < file_len && file_buf[global_idx] == '*')
        {
            t.type = TOKEN_POW;
            global_idx++;
        }
        else
        {
            t.type = TOKEN_MUL;
        }
        t.end_pos = global_idx + 1;
        return t;
    }

    if (c == '/')
    {
        t.type = TOKEN_DIV;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }
    if (c == '(')
    {
        t.type = TOKEN_LPAREN;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }
    if (c == ')')
    {
        t.type = TOKEN_RPAREN;
        global_idx++;
        t.end_pos = global_idx + 1;
        return t;
    }

    // Parse Numbers (Ints and Floats)
    if (isdigit((unsigned char)c) || c == '.')
    {
        long long start = global_idx;
        int has_dot = 0;

        if (c == '.')
        {
            // Floats cannot start with a dot (.5 is an error)
            trigger_error(global_idx + 1);
            global_idx++;
            t.end_pos = global_idx + 1;
            return t;
        }

        while (global_idx < file_len && isdigit((unsigned char)file_buf[global_idx]))
        {
            global_idx++;
        }

        if (global_idx < file_len && file_buf[global_idx] == '.')
        {
            has_dot = 1;
            global_idx++;
            // Floats cannot end with a dot (5. is an error)
            if (global_idx >= file_len || !isdigit((unsigned char)file_buf[global_idx]))
            {
                trigger_error(global_idx); // points to the '.'
                t.end_pos = global_idx + 1;
                return t;
            }
            while (global_idx < file_len && isdigit((unsigned char)file_buf[global_idx]))
            {
                global_idx++;
            }
        }

        // Catch exponential formatting error (e.g. 1.5e3)
        if (global_idx < file_len && (file_buf[global_idx] == 'e' || file_buf[global_idx] == 'E'))
        {
            trigger_error(global_idx + 1); // error at 'e'
            global_idx++;
            t.end_pos = global_idx + 1;
            return t;
        }

        // Extra sanity check for invalid letters glued to a number
        if (global_idx < file_len && isalpha((unsigned char)file_buf[global_idx]))
        {
            trigger_error(global_idx + 1);
            global_idx++;
            t.end_pos = global_idx + 1;
            return t;
        }

        long long len = global_idx - start;
        char *num_str = malloc(len + 1);
        memcpy(num_str, &file_buf[start], len);
        num_str[len] = '\0';

        if (has_dot)
        {
            t.type = TOKEN_FLOAT;
            t.val.type = VAL_FLOAT;
            t.val.f_val = strtod(num_str, NULL);
        }
        else
        {
            t.type = TOKEN_INT;
            t.val.type = VAL_INT;
            t.val.i_val = strtoll(num_str, NULL, 10);
        }
        free(num_str);
        t.end_pos = global_idx + 1;
        return t;
    }

    // Default: Invalid character
    trigger_error(global_idx + 1);
    global_idx++;
    t.end_pos = global_idx + 1;
    return t;
}

// Parser Precedence Bindings
int get_tok_precedence(TokenType type)
{
    switch (type)
    {
    case TOKEN_PLUS:
    case TOKEN_MINUS:
        return 1;
    case TOKEN_MUL:
    case TOKEN_DIV:
        return 2;
    case TOKEN_POW:
        return 3;
    default:
        return -1;
    }
}

// Pratt Parser / Recursive Descent Engine
Value parse_primary()
{
    Value res = {VAL_INT, {0}};
    if (has_error)
        return res;

    if (current_token.type == TOKEN_INT || current_token.type == TOKEN_FLOAT)
    {
        res = current_token.val;
        current_token = get_next_token();
        return res;
    }

    if (current_token.type == TOKEN_LPAREN)
    {
        current_token = get_next_token();
        res = parse_expression(0);
        if (current_token.type == TOKEN_RPAREN)
        {
            current_token = get_next_token();
        }
        else
        {
            // Missing right parenthesis error
            trigger_error(current_token.pos);
        }
        return res;
    }

    if (current_token.type == TOKEN_RPAREN)
    {
        trigger_error(current_token.pos); // Extra right parenthesis
        return res;
    }

    // If an operator is encountered here, it means no left operand was supplied (Unary error)
    trigger_error(current_token.pos);
    return res;
}

Value parse_expression(int min_prec)
{
    Value lhs = parse_primary();
    if (has_error)
        return lhs;

    while (1)
    {
        int prec = get_tok_precedence(current_token.type);
        if (prec < min_prec)
            break;

        TokenType op = current_token.type;
        long long op_pos = current_token.pos;
        current_token = get_next_token();

        // Check if two operators are side by side
        if (op == TOKEN_POW || op == TOKEN_MUL || op == TOKEN_DIV || op == TOKEN_PLUS || op == TOKEN_MINUS)
        {
            if (current_token.type == TOKEN_PLUS || current_token.type == TOKEN_MINUS ||
                current_token.type == TOKEN_MUL || current_token.type == TOKEN_DIV || current_token.type == TOKEN_POW)
            {
                trigger_error(current_token.pos);
                return lhs;
            }
        }

        long long rhs_pos = current_token.pos;
        // Right-associative handling for power exponent calculations
        int next_min_prec = (op == TOKEN_POW) ? prec : prec + 1;
        Value rhs = parse_expression(next_min_prec);
        if (has_error)
            return lhs;

        // Perform Math Operations & Validate Constraints
        if (lhs.type == VAL_INT && rhs.type == VAL_INT)
        {
            if (op == TOKEN_PLUS)
            {
                lhs.i_val += rhs.i_val;
            }
            else if (op == TOKEN_MINUS)
            {
                lhs.i_val -= rhs.i_val;
            }
            else if (op == TOKEN_MUL)
            {
                lhs.i_val *= rhs.i_val;
            }
            else if (op == TOKEN_DIV)
            {
                if (rhs.i_val == 0)
                {
                    trigger_error(rhs_pos);
                    return lhs;
                }
                lhs.i_val /= rhs.i_val;
            }
            else if (op == TOKEN_POW)
            {
                if (rhs.i_val < 0)
                {
                    trigger_error(rhs_pos);
                    return lhs;
                }
                long long base = lhs.i_val;
                long long exp = rhs.i_val;
                long long p_res = 1;
                while (exp > 0)
                {
                    if (exp & 1)
                        p_res *= base;
                    base *= base;
                    exp >>= 1;
                }
                lhs.i_val = p_res;
            }
        }
        else
        { // At least one float operand: upgrade everything to Double Float processing
            double l_val = (lhs.type == VAL_INT) ? (double)lhs.i_val : lhs.f_val;
            double r_val = (rhs.type == VAL_INT) ? (double)rhs.i_val : rhs.f_val;
            double f_res = 0.0;
            if (op == TOKEN_PLUS)
                f_res = l_val + r_val;
            else if (op == TOKEN_MINUS)
                f_res = l_val - r_val;
            else if (op == TOKEN_MUL)
                f_res = l_val * r_val;
            else if (op == TOKEN_DIV)
            {
                if (r_val == 0.0)
                {
                    trigger_error(rhs_pos);
                    return lhs;
                }
                f_res = l_val / r_val;
            }
            else if (op == TOKEN_POW)
            {
                // Float power calculations
                f_res = pow(l_val, r_val);
            }
            if (isinf(f_res) || isnan(f_res))
            {
                trigger_error(op_pos);
                return lhs;
            }
            lhs.type = VAL_FLOAT;
            lhs.f_val = f_res;
        }
    }
    return lhs;
}
// Master File Evaluation Routine
void read_file(const char *input_path, const char *output_path)
{
    FILE *fp = fopen(input_path, "r");
    if (!fp)
    {
        fprintf(stderr, "ERROR: file cannot be opened.\n");
        return;
    }
    // Read full file context instantly to perfectly protect arbitrary lengths
    fseek(fp, 0, SEEK_END);
    file_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc(file_len + 1);
    long long read_bytes = fread(buf, 1, file_len, fp);
    buf[read_bytes] = '\0';
    file_len = read_bytes;
    fclose(fp);
    file_buf = buf;
    global_idx = 0;
    has_error = 0;
    current_token = get_next_token();
    while (current_token.type != TOKEN_EOF && !has_error)
    {
        // Skip blank lines
        if (current_token.type == TOKEN_EOL)
        {
            current_token = get_next_token();
            continue;
        }
        Value res = parse_expression(0);
        if (has_error)
            break;
        if (current_token.type == TOKEN_EOL || current_token.type == TOKEN_EOF)
        {
            record_output(res);
            if (current_token.type == TOKEN_EOL)
            {
                current_token = get_next_token();
            }
        }
        else
        {
            trigger_error(current_token.pos);
            break;
        }
    }
    // Determine final output targets based on execution success state
    FILE *out_fp = fopen(output_path, "w");
    if (!out_fp)
    {
        free(buf);
        free(recorded_outputs);
        return;
    }
    if (has_error)
    {
        fprintf(out_fp, "ERROR:%lld\n", error_pos);
    }
    else
    {
        for (int i = 0; i < output_count; i++)
        {
            if (recorded_outputs[i].val.type == VAL_INT)
            {
                fprintf(out_fp, "%lld\n", recorded_outputs[i].val.i_val);
            }
            else
            {
                fprintf(out_fp, "%.15g\n", recorded_outputs[i].val.f_val);
            }
        }
    }
    fclose(out_fp);
    free(buf);
    free(recorded_outputs);
    recorded_outputs = NULL;
    output_count = 0;
    output_capacity = 0;
}

/*
    FILE OPERATION LOGIC HEADERS
*/
// Creates output folder if it doesn't exist
int create_directory(const char *path);

// Extracts base name from path (handles directory or file extensions)
void get_base_name(const char *path, char *buffer, int is_dir);

// Builds the final required naming path structures and tracks processing parameters
int process_file(const char *input_file, const char *out_dir);

/*
    EVALUATION LOGIC HEADERS
*/
// Source - https://stackoverflow.com/a/66104290
// Posted by Steve Summit
// Retrieved 2026-10-22, License - CC BY-SA 4.0
int evalexpr(const char **); // looks for a series of "terms" being added or subtracted
int eval(const char *expr);  // takes a string containing an expression, and returns the int result of evaluating it.
int evalterm(const char **); //
int gettok(const char **, int *);
void ungettok(int, const char **);
int evalpri(const char **p);
void skipwhite(const char **);

int validate_line(const char *line);

/*
    MAIN FUNCTION
*/
int main(int argc, char *argv[])
{
    char *dir_path = NULL;
    char *out_dir_arg = NULL;
    char *input_file = NULL;

    if (argc < 2)
    {
        fprintf(stderr, "ERROR: No arguments provided.\n");
        return 1;
    }

    // Loop parse CLI arguments to support flexible parameter ordering
    /*
        Starting from the calc command, this loop iterates through all arguments passed by the user and:
        > Checks if the argument matches -d or --dir (-o or --output-dir).
        > It checks if another argument exists after it.
        > If it exists, it increments i to grab that next argument as the dir_path string
          and skips it on the next loop cycle.
        > If no value follows, it prints an error message and exits the program with an error code.
        > If the argument starts with "-" but didn't match the valid flags above,
          the program flags it as an invalid option, prints an error, and terminates.
        > If the argument doesn't start with "-", it is treated as a file name.
        > If input_file is empty, it assigns this argument to input_file.
        > If input_file was already assigned previously,
          it prevents the user from providing multiple target files, throwing an error and exiting.
    */
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0)
        {
            if (i + 1 < argc)
            {
                dir_path = argv[++i];
            }
            else
            {
                fprintf(stderr, "ERROR: Missing folder value for option %s.\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output-dir") == 0)
        {
            if (i + 1 < argc)
            {
                out_dir_arg = argv[++i];
            }
            else
            {
                fprintf(stderr, "ERROR: Missing directory value for option %s.\n", argv[i]);
                return 1;
            }
        }
        else if (argv[i][0] == '-')
        {
            fprintf(stderr, "ERROR: Unknown option %s.\n", argv[i]);
            return 1;
        }
        else
        {
            if (input_file == NULL)
            {
                input_file = argv[i];
            }
            else
            {
                fprintf(stderr, "ERROR: Multiple input targets specified.\n");
                return 1;
            }
        }
    }

    // Mutually exclusive context validations
    if (dir_path && input_file)
    {
        fprintf(stderr, "ERROR: Cannot process both a directory (-d) and an individual file together.\n");
        return 1;
    }
    if (!dir_path && !input_file)
    {
        fprintf(stderr, "ERROR: Missing input file target or input directory option.\n");
        return 1;
    }

    // Resolve final destination directory name
    // 1. Make this larger (2 * MAX_PATH) to account for suffix/formatting overhead
    char final_out_dir[MAX_PATH * 2];

    if (out_dir_arg)
    {
        strncpy(final_out_dir, out_dir_arg, sizeof(final_out_dir) - 1);
        final_out_dir[sizeof(final_out_dir) - 1] = '\0';
    }
    else
    {
        char input_base[MAX_PATH];
        get_base_name(dir_path ? dir_path : input_file, input_base, dir_path ? 1 : 0);

        // 2. This is now 100% safe because final_out_dir is 2048 bytes, while input_base is 1024 bytes.
        snprintf(final_out_dir, sizeof(final_out_dir), "%s_%s", input_base, STUDENT_SUFFIX);
    }

    // Create target output destination folder if not present
    if (create_directory(final_out_dir) != 0)
    {
        fprintf(stderr, "ERROR: Output directory '%s' cannot be created or accessed.\n", final_out_dir);
        return 1;
    }

    // Execution routing based on modes
    if (input_file)
    {
        struct stat st;
        if (stat(input_file, &st) != 0 || S_ISDIR(st.st_mode))
        {
            fprintf(stderr, "ERROR: Input file '%s' does not exist or is invalid.\n", input_file);
            return 1;
        }
        if (process_file(input_file, final_out_dir) != 0)
        {
            return 1;
        }
    }
    else if (dir_path)
    {
        DIR *dir = opendir(dir_path);
        if (!dir)
        {
            fprintf(stderr, "ERROR: Input directory folder source '%s' does not exist.\n", dir_path);
            return 1;
        }

        // scans a directory to find and process all regular files that end with a .txt extension.
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            char *dot = strrchr(entry->d_name, '.');
            if (dot && strcmp(dot, ".txt") == 0)
            {
                char full_input_path[MAX_PATH * 2];
                snprintf(full_input_path, sizeof(full_input_path), "%s/%s", dir_path, entry->d_name);

                struct stat entry_stat;
                if (stat(full_input_path, &entry_stat) == 0 && S_ISREG(entry_stat.st_mode))
                {
                    if (process_file(full_input_path, final_out_dir) != 0)
                    {
                        closedir(dir);
                        return 1;
                    }
                }
            }
        }
        closedir(dir);
    }

    return 0;
}

/*void read_file(char arr[], const FILE *input_file)
{

    char line[LINE_LEN]; // Use a shared standard buffer size

    FILE *fp = fopen(input_file, "r"); // open the file with writing permissions

    if (fp == NULL)
    { // throw an error if the file cannot be opened
        fprintf(stderr, "ERROR: file cannot be opened.\n");
        return -1; // failure
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {*/
/* TODO:
1. Check a line for ambigous characters
2. Evaluate the line
3. Write the result to an output file
*/
/*if (validate_line(line) != 0)
{
    // TODO: write ERROR:5 to the file.
    fprintf(stderr, "ERROR: invalid character.\n");
    return -1;
}
eval(line);
}
}*/

/*
    EVALUATION LOGIC
*/

int evalexpr(const char **p)
{
    int r = evalterm(p);
    while (1)
    {
        int op = gettok(p, NULL);
        switch (op)
        {
        case '+':
            r += evalterm(p);
            break;
        case '-':
            r -= evalterm(p);
            break;
        default:
            ungettok(op, p);
            return r;
        }
    }
}

int evalterm(const char **p)
{
    int r = evalpri(p);
    while (1)
    {
        int op = gettok(p, NULL);
        switch (op)
        {
        case '*':
            r *= evalpri(p);
            break;
        case '/':
            r /= evalpri(p);
            break;
        default:
            ungettok(op, p);
            return r;
        }
    }
}

int evalpri(const char **p)
{
    int v;
    int op = gettok(p, &v);
    switch (op)
    {
    case '1':
        return v;
    case '-':
        return -evalpri(p);
    case '(':
        v = evalexpr(p);
        op = gettok(p, NULL);
        if (op != ')')
        {
            fprintf(stderr, "missing ')'\n");
            ungettok(op, p);
        }
        return v;
    }
}

int gettok(const char **p, int *vp)
{
    skipwhite(p);
    char c = **p;
    if (isdigit(c))
    {
        char *p2;
        int v = strtoul(*p, &p2, 0);
        *p = p2;
        if (vp)
            *vp = v;
        return '1';
    }
    (*p)++;
    return c;
}

void skipwhite(const char **p)
{
    while (isspace(**p))
        (*p)++;
}

void ungettok(int op, const char **p)
{
    (*p)--;
}

int eval(const char *expr) // takes a string containing an expression, and returns the int result of evaluating it.
{
    return evalexpr(&expr);
}

/*
    FILE OPERATION LOGIC
*/
int create_directory(const char *path)
{
    struct stat st = {0};
    if (stat(path, &st) == -1)
    {
        if (mkdir(path, 0777) != 0)
        { // creates directory with specified path and read, write and execute permissions
            return -1;
        }
    }
    return 0;
}

void get_base_name(const char *path, char *buffer, int is_dir)
{
    const char *base = strrchr(path, '/');
#ifdef _WIN32
    const char *base_win = strrchr(path, '\\');
    if (!base || (base_win && base_win > base))
        base = base_win;
#endif
    base = (base == NULL) ? path : (base + 1);

    if (is_dir)
    {
        strcpy(buffer, base);
    }
    else
    {
        const char *dot = strrchr(base, '.');
        if (dot && strcmp(dot, ".txt") == 0)
        {
            size_t len = dot - base;
            strncpy(buffer, base, len);
            buffer[len] = '\0';
        }
        else
        {
            strcpy(buffer, base);
        }
    }
}

int process_file(const char *input_file, const char *out_dir)
{
    char base_name[MAX_PATH];
    get_base_name(input_file, base_name, 0);

    // 1. Construct the exact output target filepath
    char out_file_path[MAX_PATH * 2];
    snprintf(out_file_path, sizeof(out_file_path), "%s/%s_%s.txt", out_dir, base_name, STUDENT_FULL_SUFFIX);

    // 2. Delegate the core engine execution pass to your transaction-buffered parser
    // This systematically processes math lines and enforces the "all-or-nothing" rule
    read_file(input_file, out_file_path);

    // 3. AddressSanitizer/grading check tracking
    // If the parser tripped a critical evaluation error, exit with -1 to halt batch processes
    if (has_error)
    {
        // The error structure has already printed its string coordinates to out_file_path cleanly.
        // Return 1 or -1 here depending on whether your main requires a full pipeline break.
        return 1;
    }

    return 0;
}

/*
    ----
*/

// AI (Claude Haiku 4.5): prompt "how to efficiently check if a char is arithmetic (0-9, +, -, /, *) in C? Intermediate user is trying to validate expressions from a file, on success, write an answer to that expression and on error, write an error code to another file."
// Link: https://duck.ai/share/17adb4cc-e97d-4d85-a6e3-680497212a24#ZFlq4VX5J11Gp2hJt1vgEDlLn70pIZKZpAHHL-KmA0s
//   Took the line validation loop.

// Helper to separate numbers from operators
bool is_operator(char c)
{
    return (c == '+' || c == '-' || c == '*' || c == '/');
}

int validate_line(const char *line)
{
    bool expecting_number = true; // True if we need a number next, false if we expect an operator
    bool found_any_content = false;

    for (int i = 0; line[i] != '\0'; i++)
    {
        char c = line[i];

        // skip spaces but don't break the structural logic
        if (isspace((unsigned char)c))
        {
            continue;
        }

        // enforce allowed characters
        if (!isdigit((unsigned char)c) && !is_operator(c))
        {
            // TODO: Write error codes to the files
            return -1; // Invalid character found
        }

        found_any_content = true;

        // Structural Syntax Check
        if (isdigit((unsigned char)c))
        {
            if (!expecting_number)
            {
                // Caught something like "5 6" (two adjacent numbers separated by space)
                return -2; // Structural Error: Missing operator
            }
            // Skip the rest of this multi-digit number so '4' and '2' in "42" are not treated as separate things
            while (isdigit((unsigned char)line[i + 1]))
            {
                i++;
            }
            expecting_number = false; // Next, we must see an operator
        }
        else if (is_operator(c))
        {
            if (expecting_number)
            {
                // Caught something like "+ 5" or "5 * + 3"
                return -3; // Structural Error: Missing operand / double operator
            }
            expecting_number = true; // Next, we must see a number
        }
    }

    // A valid line cannot end on an operator
    if (!found_any_content || expecting_number)
    {
        return -4; // Structural Error: Ends with trailing operator or line is empty
    }

    return 0; // Completely valid arithmetic syntax
}
