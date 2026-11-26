#define GOBLIN_ARGV_IMPLEMENTATION
#include <goblin/argv.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "core/followup.h"
#include "core/pipeline.h"
#include "core/review.h"
#include "core/themes.h"
#include "llm/llm.h"
#include "net/http.h"
#include "steam/cache.h"
#include "steam/steam.h"
#include "tui/app.h"
#include "util/arena.h"
#include "util/debug_log.h"
#include "util/error.h"

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
    printf("  steamscribe                  interactive TUI: asks for a game name\n");
    printf("  steamscribe <name>            TUI, starting with a search for <name>\n");
    printf("  steamscribe --appid <id>       TUI, skipping search, using an app id directly\n\n");
    printf("  steamscribe search <name>     print search matches\n");
    printf("  steamscribe info <appid>      print game info\n");
    printf("  steamscribe themes <appid>    print extracted likes/dislikes as JSON\n\n");
    printf("Options:\n");
    printf("  -h, --help                    show this help and exit\n");
    printf("      --version                show the version and exit\n");
    printf("      --verbose                log HTTP/LLM activity to debug.log\n");
    printf("      --plain                  use the old line-based prompts instead of the TUI\n");
}

/* Reads one line from stdin into buf, stripping the trailing newline.
 * buf is always left NUL-terminated, empty on EOF/error. */
static void read_line(char *buf, size_t size)
{
    if (size == 0) return;

    if (!fgets(buf, (int)size, stdin))
    {
        buf[0] = '\0';
        return;
    }

    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
}

static int cmd_search(arena_t *arena, const config_t *cfg, const char *query)
{
    steam_search_results_t results;
    err_t err;
    err_clear(&err);

    debug_logf("search: query=\"%s\" language=%s", query, cfg->steam_language);

    if (steam_search(arena, query, cfg->steam_language, &results, &err) != 0)
    {
        fprintf(stderr, "search failed: %s\n", err.msg);
        return 1;
    }

    if (results.count == 0)
    {
        printf("No matches for \"%s\"\n", query);
        return 0;
    }

    printf("Found %zu match(es) for \"%s\":\n", results.count, query);
    for (size_t i = 0; i < results.count; i++)
    {
        printf("  %-10u %s\n", results.items[i].appid, results.items[i].name);
    }

    return 0;
}

static int cmd_info(arena_t *arena, const config_t *cfg, uint32_t appid)
{
    err_t err;
    err_clear(&err);

    game_info_t info;
    if (pipeline_fetch_game(arena, cfg, appid, &info, &err) != 0)
    {
        fprintf(stderr, "info failed: %s\n", err.msg);
        return 1;
    }

    review_set_t reviews;
    int reviews_rc = steam_reviews_summary(arena, appid, cfg->steam_language, &reviews, &err);

    printf("%s (%u)\n", info.name, info.appid);
    printf("Genres: %s\n", info.genres);
    printf("Developer: %s\n", info.developers);
    printf("Released: %s\n", info.release_date);
    printf("\n%s\n", info.short_desc);

    if (reviews_rc == 0)
    {
        printf("\nReviews: %s (%d positive / %d negative)\n", reviews.score_desc, reviews.total_positive, reviews.total_negative);
    }
    else
    {
        fprintf(stderr, "note: could not fetch review score: %s\n", err.msg);
    }

    return 0;
}

static int cmd_themes(arena_t *arena, const config_t *cfg, uint32_t appid)
{
    err_t err;
    err_clear(&err);

    game_info_t game;
    if (pipeline_fetch_game(arena, cfg, appid, &game, &err) != 0)
    {
        fprintf(stderr, "themes failed: %s\n", err.msg);
        return 1;
    }

    theme_set_t themes;
    if (pipeline_fetch_themes(arena, cfg, appid, &game, &themes, &err) != 0)
    {
        fprintf(stderr, "themes failed: %s\n", err.msg);
        return 1;
    }

    char *output = themes_to_json(&themes);
    if (!output)
    {
        fprintf(stderr, "themes failed: out of memory\n");
        return 1;
    }

    printf("%s\n", output);
    free(output);
    return 0;
}

/* Resolves a game name to a single app id: searches, then if there's more
 * than one match, asks the user to pick one from a numbered list. */
static int resolve_appid_from_query(arena_t *arena, const config_t *cfg, const char *query, uint32_t *out_appid)
{
    steam_search_results_t results;
    err_t err;
    err_clear(&err);

    if (steam_search(arena, query, cfg->steam_language, &results, &err) != 0)
    {
        fprintf(stderr, "search failed: %s\n", err.msg);
        return 1;
    }

    if (results.count == 0)
    {
        fprintf(stderr, "No matches for \"%s\"\n", query);
        return 1;
    }

    if (results.count == 1)
    {
        *out_appid = results.items[0].appid;
        return 0;
    }

    printf("Found %zu matches for \"%s\":\n", results.count, query);
    for (size_t i = 0; i < results.count; i++)
    {
        printf("  %zu) %s (%u)\n", i + 1, results.items[i].name, results.items[i].appid);
    }

    printf("Pick one > ");
    fflush(stdout);

    char line[32];
    read_line(line, sizeof(line));

    size_t choice = (size_t)strtoul(line, NULL, 10);
    if (choice < 1 || choice > results.count)
    {
        fprintf(stderr, "invalid selection\n");
        return 1;
    }

    *out_appid = results.items[choice - 1].appid;
    return 0;
}

/* Prints *items as a numbered list under `label` and lets the user check
 * any number of them by typing space-separated numbers, then lets them
 * type their own additional items (comma-separated), appended to *items
 * via themes_add_custom_item and checked by default, then offers to move
 * any of the originally-listed items over to *other_items (e.g. something
 * filed under "disliked" that's actually a positive for this player). */
static void prompt_checklist(arena_t *arena, const char *label, const char *other_label, theme_t **items, size_t *count, theme_t **other_items, size_t *other_count)
{
    printf("\n%s\n", label);

    size_t original_count = *count;
    if (original_count > 0)
    {
        for (size_t i = 0; i < original_count; i++) printf("  %zu) %s\n", i + 1, (*items)[i].label);

        printf("Type the numbers that apply (space-separated), or leave blank > ");
        fflush(stdout);

        char line[256];
        read_line(line, sizeof(line));

        char *tok = strtok(line, " \t");
        while (tok)
        {
            size_t idx = (size_t)strtoul(tok, NULL, 10);
            if (idx >= 1 && idx <= original_count) (*items)[idx - 1].checked = true;
            tok = strtok(NULL, " \t");
        }
    }
    else
    {
        printf("(no suggestions from the reviews)\n");
    }

    printf("Add your own (comma-separated), or leave blank > ");
    fflush(stdout);

    char custom_line[512];
    read_line(custom_line, sizeof(custom_line));

    char *tok = strtok(custom_line, ",");
    while (tok)
    {
        while (*tok == ' ') tok++;
        size_t len = strlen(tok);
        while (len > 0 && tok[len - 1] == ' ') tok[--len] = '\0';
        if (len > 0) themes_add_custom_item(arena, items, count, tok);
        tok = strtok(NULL, ",");
    }

    if (original_count == 0) return;

    printf("Any of those actually belong under \"%s\" instead? (numbers, space-separated), or leave blank > ", other_label);
    fflush(stdout);

    char move_line[256];
    read_line(move_line, sizeof(move_line));

    size_t move_idx[64];
    size_t n_move = 0;
    tok = strtok(move_line, " \t");
    while (tok && n_move < 64)
    {
        size_t idx = (size_t)strtoul(tok, NULL, 10);
        if (idx >= 1 && idx <= original_count) move_idx[n_move++] = idx - 1;
        tok = strtok(NULL, " \t");
    }

    /* Descending order so removing one doesn't shift the index of another
     * still-pending move. */
    for (size_t i = 0; i < n_move; i++)
        for (size_t j = i + 1; j < n_move; j++)
            if (move_idx[j] > move_idx[i])
            {
                size_t tmp = move_idx[i];
                move_idx[i] = move_idx[j];
                move_idx[j] = tmp;
            }

    for (size_t i = 0; i < n_move; i++) themes_move_item(arena, items, count, other_items, other_count, move_idx[i]);
}

static int cmd_interactive(arena_t *arena, const config_t *cfg, uint32_t appid)
{
    err_t err;
    err_clear(&err);

    game_info_t game;
    if (pipeline_fetch_game(arena, cfg, appid, &game, &err) != 0)
    {
        fprintf(stderr, "could not load game info: %s\n", err.msg);
        return 1;
    }

    printf("\nFound: %s (%u)\n", game.name, game.appid);
    printf("Reading %d player reviews...\n", cfg->steam_review_count);

    theme_set_t themes;
    if (pipeline_fetch_themes(arena, cfg, appid, &game, &themes, &err) != 0)
    {
        fprintf(stderr, "could not read player reviews: %s\n", err.msg);
        return 1;
    }

    prompt_checklist(arena, "What did YOU like?", "What bothered you?", &themes.liked, &themes.n_liked, &themes.disliked, &themes.n_disliked);
    prompt_checklist(arena, "What bothered you?", "What did YOU like?", &themes.disliked, &themes.n_disliked, &themes.liked, &themes.n_liked);

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    if (pipeline_resolve_llm(cfg, &provider, &llm_cfg, &err) != 0)
    {
        fprintf(stderr, "could not generate review: %s\n", err.msg);
        return 1;
    }

    char **followup_questions = NULL;
    char **followup_answers = NULL;
    size_t n_followup = 0;
    err_t followup_err;
    err_clear(&followup_err);
    if (followup_generate(arena, provider, &llm_cfg, &game, &themes, &followup_questions, &n_followup, &followup_err) != 0)
    {
        /* Best-effort and optional: skip silently rather than block the
         * flow over something that was never required to begin with. */
        n_followup = 0;
    }

    if (n_followup > 0)
    {
        followup_answers = (char **)arena_alloc(arena, n_followup * sizeof(char *));
        printf("\nA few optional questions (leave blank to skip):\n");
        for (size_t i = 0; i < n_followup; i++)
        {
            printf("%s > ", followup_questions[i]);
            fflush(stdout);
            char answer_line[512];
            read_line(answer_line, sizeof(answer_line));
            followup_answers[i] = arena_strdup(arena, answer_line);
        }
    }

    printf("\nAnything else? > ");
    fflush(stdout);
    char notes[512];
    read_line(notes, sizeof(notes));

    printf("Recommend it? (y/n) > ");
    fflush(stdout);
    char recommend_line[16];
    read_line(recommend_line, sizeof(recommend_line));
    bool recommend = (recommend_line[0] == 'y' || recommend_line[0] == 'Y');

    printf("Rating 1-10, optional (blank skips) > ");
    fflush(stdout);
    char rating_line[16];
    read_line(rating_line, sizeof(rating_line));
    int rating = rating_line[0] ? atoi(rating_line) : 0;
    if (rating < 0) rating = 0;
    if (rating > 10) rating = 10;

    printf("Length: (s)hort / (m)edium / (l)ong > ");
    fflush(stdout);
    char length_line[16];
    read_line(length_line, sizeof(length_line));

    char length_choice = length_line[0] ? length_line[0] : cfg->review_default_length[0];
    review_len_t length = REVIEW_LEN_SHORT;
    if (length_choice == 'm' || length_choice == 'M') length = REVIEW_LEN_MEDIUM;
    else if (length_choice == 'l' || length_choice == 'L') length = REVIEW_LEN_LONG;

    printf("Words: (n)ormal / (s)imple > ");
    fflush(stdout);
    char vocab_line[16];
    read_line(vocab_line, sizeof(vocab_line));

    char vocab_choice = vocab_line[0] ? vocab_line[0] : cfg->review_default_vocab[0];
    review_vocab_t vocab = (vocab_choice == 's' || vocab_choice == 'S') ? REVIEW_VOCAB_SIMPLE : REVIEW_VOCAB_NORMAL;

    printf("Format: (p)lain / (b)bcode > ");
    fflush(stdout);
    char format_line[16];
    read_line(format_line, sizeof(format_line));
    bool bbcode = (format_line[0] == 'b' || format_line[0] == 'B');

    printf("Spoiler details, optional (blank skips) > ");
    fflush(stdout);
    char spoiler_notes[512];
    read_line(spoiler_notes, sizeof(spoiler_notes));

    review_tone_t tone = REVIEW_TONE_CASUAL;
    if (strcmp(cfg->review_default_tone, "neutral") == 0) tone = REVIEW_TONE_NEUTRAL;
    else if (strcmp(cfg->review_default_tone, "enthusiastic") == 0) tone = REVIEW_TONE_ENTHUSIASTIC;

    review_request_t req;
    req.game = &game;
    req.themes = &themes;
    req.notes = notes;
    req.recommend = recommend;
    req.rating = rating;
    req.length = length;
    req.tone = tone;
    req.vocab = vocab;
    req.followup_questions = followup_questions;
    req.followup_answers = followup_answers;
    req.n_followup = n_followup;
    req.bbcode = bbcode;
    req.spoiler_notes = spoiler_notes;

    char *review_text = NULL;
    if (review_generate(arena, provider, &llm_cfg, &req, &review_text, &err) != 0)
    {
        fprintf(stderr, "could not generate review: %s\n", err.msg);
        return 1;
    }

    printf("\n-- Your review ------------------------------\n");
    printf("%s\n", review_text);
    printf("----------------------------------------------\n");

    return 0;
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

    if (goblin_argv_has_flag(argc, argv, "--verbose")) debug_log_enable();

    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg;
    err_t err;
    err_clear(&err);
    if (config_load(&arena, &cfg, &err) != 0)
    {
        fprintf(stderr, "could not load config: %s\n", err.msg);
        arena_free(&arena);
        return 1;
    }

    int rc = 0;

    /* The subcommand/game-name argument is the first non-flag argument,
     * wherever a flag like --verbose happens to sit relative to it. */
    int cmd_i = -1;
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-')
        {
            cmd_i = i;
            break;
        }
    }

    bool have_arg = cmd_i > 0 && cmd_i + 1 < argc;
    bool is_known_subcommand = cmd_i > 0 && (strcmp(argv[cmd_i], "search") == 0 || strcmp(argv[cmd_i], "info") == 0 || strcmp(argv[cmd_i], "themes") == 0);
    const char *appid_flag_value = goblin_argv_find_value(argc, argv, "--appid");
    bool plain = goblin_argv_has_flag(argc, argv, "--plain");

    if (have_arg && strcmp(argv[cmd_i], "search") == 0)
    {
        rc = cmd_search(&arena, &cfg, argv[cmd_i + 1]);
    }
    else if (have_arg && strcmp(argv[cmd_i], "info") == 0)
    {
        uint32_t appid = (uint32_t)strtoul(argv[cmd_i + 1], NULL, 10);
        if (appid == 0)
        {
            fprintf(stderr, "info: \"%s\" is not a valid app id\n", argv[cmd_i + 1]);
            rc = 1;
        }
        else
        {
            rc = cmd_info(&arena, &cfg, appid);
        }
    }
    else if (have_arg && strcmp(argv[cmd_i], "themes") == 0)
    {
        uint32_t appid = (uint32_t)strtoul(argv[cmd_i + 1], NULL, 10);
        if (appid == 0)
        {
            fprintf(stderr, "themes: \"%s\" is not a valid app id\n", argv[cmd_i + 1]);
            rc = 1;
        }
        else
        {
            rc = cmd_themes(&arena, &cfg, appid);
        }
    }
    else if (is_known_subcommand)
    {
        fprintf(stderr, "%s: missing argument\n", argv[cmd_i]);
        rc = 1;
    }
    else if (appid_flag_value)
    {
        uint32_t appid = (uint32_t)strtoul(appid_flag_value, NULL, 10);
        if (appid == 0)
        {
            fprintf(stderr, "--appid: \"%s\" is not a valid app id\n", appid_flag_value);
            rc = 1;
        }
        else if (plain)
        {
            rc = cmd_interactive(&arena, &cfg, appid);
        }
        else
        {
            tui_options_t options;
            options.initial_query = NULL;
            options.initial_appid = appid;

            err_t tui_err;
            err_clear(&tui_err);
            if (tui_run(&arena, &cfg, &options, &tui_err) != 0)
            {
                fprintf(stderr, "could not start the TUI: %s\n", tui_err.msg);
                rc = 1;
            }
        }
    }
    else if (plain)
    {
        const char *query = (cmd_i > 0) ? argv[cmd_i] : NULL;

        char query_buf[256];
        if (!query)
        {
            printf("Enter a game name > ");
            fflush(stdout);
            read_line(query_buf, sizeof(query_buf));

            if (query_buf[0] == '\0')
            {
                print_help();
                arena_free(&arena);
                return 0;
            }

            query = query_buf;
        }

        uint32_t appid = 0;
        rc = resolve_appid_from_query(&arena, &cfg, query, &appid);
        if (rc == 0) rc = cmd_interactive(&arena, &cfg, appid);
    }
    else
    {
        tui_options_t options;
        options.initial_query = (cmd_i > 0) ? argv[cmd_i] : NULL;
        options.initial_appid = 0;

        err_t tui_err;
        err_clear(&tui_err);
        if (tui_run(&arena, &cfg, &options, &tui_err) != 0)
        {
            fprintf(stderr, "could not start the TUI: %s\n", tui_err.msg);
            rc = 1;
        }
    }

    arena_free(&arena);
    return rc;
}
