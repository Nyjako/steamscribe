#define GOBLIN_ARGV_IMPLEMENTATION
#include <goblin/argv.h>

#include <stdio.h>

#ifndef STEAMSCRIBE_VERSION
#define STEAMSCRIBE_VERSION "0.0.0"
#endif

static void print_version(void)
{
    printf("steamscribe %s\n", STEAMSCRIBE_VERSION);
}

static void print_help(void)
{
    printf("steamscribe %s\n", STEAMSCRIBE_VERSION);
    printf("Turn a game's Steam page and player reviews into a review draft you write yourself.\n\n");
    printf("Usage:\n");
    printf("  steamscribe                  interactive: asks for a game name\n");
    printf("  steamscribe <name>            start with a search for <name>\n");
    printf("  steamscribe --appid <id>       skip search, use an app id directly\n\n");
    printf("  steamscribe search <name>     print search matches\n");
    printf("  steamscribe info <appid>      print game info\n");
    printf("  steamscribe themes <appid>    print extracted likes/dislikes as JSON\n\n");
    printf("Options:\n");
    printf("  -h, --help                    show this help and exit\n");
    printf("      --version                show the version and exit\n");
}

int main(int argc, char **argv)
{
    if (goblin_argv_has_flag(argc, argv, "--version"))
    {
        print_version();
        return 0;
    }

    if (goblin_argv_has_flag(argc, argv, "--help") || goblin_argv_has_flag(argc, argv, "-h"))
    {
        print_help();
        return 0;
    }

    print_help();
    return 0;
}
