/* Discord rich presence over the local pipe, without the SDK. Fails
   quietly. The application id comes from the build (DISCORD_APP_ID);
   a build without one has no presence. */
#ifndef XERABORA_DISCORD_H
#define XERABORA_DISCORD_H

/* What the profile shows. Any field may be NULL or empty. */
struct discord_presence
{
    const char *title;       /* the game */
    const char *state;       /* the rich presence line, or the progress */
    const char *game_url;    /* the game's page */
    const char *icon_url;    /* the game's icon */
    const char *icon_text;   /* shown over the icon */
    const char *badge_url;   /* the last unlock's badge, the small icon */
    const char *badge_text;  /* the last unlock's title */
    const char *profile_url; /* behind the button */
};

/* Whether this build carries an application id. */
int discord_available(void);

/* The switch on SETTINGS. Off clears the presence and closes the pipe. */
void discord_set_enabled(int on);
int discord_enabled(void);

void discord_shutdown(void);

/* NULL clears the presence. The same presence twice sends nothing. */
void discord_set(const struct discord_presence *p);

/* Called from the main loop: connects when Discord appears, reads its
   replies, sends a changed presence. Costs nothing when there is no
   Discord. */
void discord_tick(void);

#endif
