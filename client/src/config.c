#include "config.h"

#include <stdio.h>
#include <string.h>

#include "platform.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif

int config_credentials_path(char *out, size_t size)
{
    char dir[512];

    if (platform_config_dir(dir, sizeof(dir)) != 0)
        return -1;
    snprintf(out, size, "%s" SEP "credentials", dir);
    return 0;
}

/* File format: two lines, username then token. */
int config_load_credentials(char *user, size_t user_size, char *token, size_t token_size)
{
    char path[600];
    FILE *f;

    if (config_credentials_path(path, sizeof(path)) != 0)
        return 0;

    f = fopen(path, "r");
    if (f == NULL)
        return 0;

    user[0] = token[0] = '\0';
    if (fgets(user, (int)user_size, f) == NULL || fgets(token, (int)token_size, f) == NULL) {
        fclose(f);
        return 0;
    }
    fclose(f);

    user[strcspn(user, "\r\n")] = '\0';
    token[strcspn(token, "\r\n")] = '\0';
    return user[0] != '\0' && token[0] != '\0';
}

int config_save_credentials(const char *user, const char *token)
{
    char path[600];
    FILE *f;

    if (config_credentials_path(path, sizeof(path)) != 0)
        return -1;

    f = fopen(path, "w");
    if (f == NULL)
        return -1;
    fprintf(f, "%s\n%s\n", user, token);
    fclose(f);
#ifndef _WIN32
    /* The token logs in as the user: keep the file private. */
    chmod(path, 0600);
#endif
    return 0;
}

static int config_apikey_path(char *out, size_t size)
{
    char dir[512];

    if (platform_config_dir(dir, sizeof(dir)) != 0)
        return -1;
    snprintf(out, size, "%s" SEP "apikey", dir);
    return 0;
}

int config_load_apikey(char *key, size_t size)
{
    char path[600];
    FILE *f;

    if (config_apikey_path(path, sizeof(path)) != 0)
        return 0;
    f = fopen(path, "r");
    if (f == NULL)
        return 0;
    if (fgets(key, (int)size, f) == NULL) {
        fclose(f);
        return 0;
    }
    fclose(f);
    key[strcspn(key, "\r\n")] = '\0';
    return key[0] != '\0';
}

int config_save_apikey(const char *key)
{
    char path[600];
    FILE *f;

    if (key == NULL || key[0] == '\0')
        return -1;
    if (config_apikey_path(path, sizeof(path)) != 0)
        return -1;
    f = fopen(path, "w");
    if (f == NULL)
        return -1;
    fprintf(f, "%s\n", key);
    fclose(f);
#ifndef _WIN32
    chmod(path, 0600);
#endif
    return 0;
}

static int config_lan_path(char *out, size_t size)
{
    char dir[512];

    if (platform_config_dir(dir, sizeof(dir)) != 0)
        return -1;
    snprintf(out, size, "%s" SEP "lan", dir);
    return 0;
}

int config_load_lan(void)
{
    char path[600], line[8] = "";
    FILE *f;

    if (config_lan_path(path, sizeof(path)) != 0)
        return 0;
    f = fopen(path, "r");
    if (f == NULL)
        return 0;
    if (fgets(line, (int)sizeof(line), f) == NULL)
        line[0] = '\0';
    fclose(f);
    return line[0] == '1';
}

int config_save_lan(int on)
{
    char path[600];
    FILE *f;

    if (config_lan_path(path, sizeof(path)) != 0)
        return -1;
    f = fopen(path, "w");
    if (f == NULL)
        return -1;
    fprintf(f, "%d\n", on ? 1 : 0);
    fclose(f);
    return 0;
}

static int config_follow_path(char *out, size_t size)
{
    char dir[512];

    if (platform_config_dir(dir, sizeof(dir)) != 0)
        return -1;
    snprintf(out, size, "%s" SEP "follow", dir);
    return 0;
}

int config_load_follow(void)
{
    char path[600], line[8] = "";
    FILE *f;

    if (config_follow_path(path, sizeof(path)) != 0)
        return 1;
    f = fopen(path, "r");
    if (f == NULL)
        return 1; /* no file: on */
    if (fgets(line, (int)sizeof(line), f) == NULL)
        line[0] = '\0';
    fclose(f);
    return line[0] != '0';
}

int config_save_follow(int on)
{
    char path[600];
    FILE *f;

    if (config_follow_path(path, sizeof(path)) != 0)
        return -1;
    f = fopen(path, "w");
    if (f == NULL)
        return -1;
    fprintf(f, "%d\n", on ? 1 : 0);
    fclose(f);
    return 0;
}

int config_games_path(char *out, size_t size)
{
    char dir[512];

    if (platform_config_dir(dir, sizeof(dir)) != 0)
        return -1;
    snprintf(out, size, "%s" SEP "games", dir);
    return 0;
}

void config_forget_credentials(void)
{
    char path[600];

    if (config_credentials_path(path, sizeof(path)) == 0)
        remove(path);
}
